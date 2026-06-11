#include "sar/SarBackprojectionTransformAccelNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"

#include <chrono>
#include <algorithm>
#include <atomic>
#include <sstream>

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

SarBackendKind ToSarBackendKind(graph::gpu::accel::BackendKind backend) noexcept {
    switch (backend) {
        case graph::gpu::accel::BackendKind::Metal:
            return SarBackendKind::NativeDevice;
        default:
            return SarBackendKind::Host;
    }
}

void* MakeSyntheticDevicePointer(const graph::gpu::accel::DeviceBufferView& input,
                                 std::uint64_t kernel_sequence) noexcept {
    const auto token =
        ((static_cast<std::uint64_t>(input.bytes) + 1u) << 8u) |
        ((kernel_sequence + 1u) & 0xFFu);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}

graph::gpu::metal::capabilities::MetalKernelDescriptor MakeBackprojectionDescriptor(
    const SarBackprojectionTransformAccelConfig& config) {
    graph::gpu::metal::capabilities::MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = config.kernel_id;
    descriptor.function_name = "graphx_sar_backprojection_tile_f32";
    descriptor.source_kind = graph::gpu::metal::capabilities::MetalKernelSourceKind::InlineSource;
    const auto tap_count = std::max<std::uint32_t>(1u, config.tap_count);

    std::ostringstream src;
    src << "#include <metal_stdlib>\n"
        << "using namespace metal;\n"
        << "kernel void graphx_sar_backprojection_tile_f32(\n"
        << "    const device float* range_tile [[buffer(0)]],\n"
        << "    device float* image_tile [[buffer(1)]],\n"
        << "    uint gid [[thread_position_in_grid]]) {\n"
        << "    constexpr uint kTapCount = " << tap_count << "u;\n"
        << "    constexpr uint kSampleCount = " << std::max(1u, config.image_width) << "u;\n"
        << "    constexpr float kDelayStep = " << config.delay_step << "f;\n"
        << "    constexpr float kPhaseTapScale = " << config.phase_tap_scale << "f;\n"
        << "    constexpr float kPhaseApertureScale = " << config.phase_aperture_scale << "f;\n"
        << "    constexpr float kPi = 3.14159265358979323846f;\n"
        << "    constexpr float kInvTapCount = 1.0f / float(kTapCount);\n"
        << "    const uint sample_count = max(1u, kSampleCount);\n"
        << "    if (gid >= sample_count) {\n"
        << "        return;\n"
        << "    }\n"
        << "    const float aperture_norm = float(gid) / float(sample_count);\n"
        << "    float accum = 0.0f;\n"
        << "    for (uint tap = 0; tap < kTapCount; ++tap) {\n"
        << "        const float delay = float(gid) + kDelayStep * float(tap);\n"
        << "        const float sample_pos = clamp(delay, 0.0f, float(sample_count - 1u));\n"
        << "        const uint idx0 = uint(floor(sample_pos));\n"
        << "        const uint idx1 = min(sample_count - 1u, idx0 + 1u);\n"
        << "        const float frac = sample_pos - float(idx0);\n"
        << "        const float sample = mix(range_tile[idx0], range_tile[idx1], frac);\n"
        << "        const float aperture_weight = 0.5f - 0.5f * cos(2.0f * kPi * ((float(tap) + 0.5f) * kInvTapCount));\n"
        << "        const float phase = (kPhaseTapScale * float(tap) + kPhaseApertureScale * aperture_norm) * kPi;\n"
        << "        const float phasor = cos(phase) + 0.5f * sin(phase);\n"
        << "        const float weight = aperture_weight * phasor;\n"
        << "        accum += sample * weight;\n"
        << "    }\n"
        << "    image_tile[gid] = accum * kInvTapCount;\n"
        << "}\n";
    descriptor.source_payload = src.str();
    descriptor.arg_layout = {
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadOnly},
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::WriteOnly},
    };
    descriptor.dispatch.default_grid_x = std::max(1u, config.image_width);
    descriptor.dispatch.default_grid_y = 1;
    descriptor.dispatch.default_grid_z = 1;
    descriptor.dispatch.default_block_x = 1;
    descriptor.dispatch.default_block_y = 1;
    descriptor.dispatch.default_block_z = 1;
    return descriptor;
}

std::uint64_t NextOpaqueEventId() {
    static std::atomic<std::uint64_t> next_event{1u};
    return next_event.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

SarBackprojectionTransformAccelNode::SarBackprojectionTransformAccelNode(
    SarBackprojectionTransformAccelConfig config)
    : config_(config) {}

std::optional<SarAccelControlToken> SarBackprojectionTransformAccelNode::Transfer(
    const SarAccelControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = runtime::SteadyClock::now();

    if (!input.has_device_view || !graph::gpu::accel::IsValidView(input.device_view)) {
        return std::nullopt;
    }

    const auto& in_view = input.device_view;

    const auto accel_backend = ToAccelBackendKind(config_.backend);
    if (accel_backend == graph::gpu::accel::BackendKind::Unknown) {
        return std::nullopt;
    }

    if (config_.backend == SarBackendKind::NativeDevice && native_kernel_bound_) {
        native_kernel_node_.SetOutputBytes(in_view.bytes);
        auto native_output = native_kernel_node_.Transfer(
            in_view,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!native_output) {
            return std::nullopt;
        }

        last_kernel_ticket_ = native_kernel_node_.last_kernel_ticket();

        auto token = input;
        token.device_view = *native_output;
        token.has_device_view = true;
        token.kernel_ticket = last_kernel_ticket_;
        token.has_kernel_ticket = true;
        token.sidecar.backend_id = native_output->device_id;
        token.sidecar.backend = ToSarBackendKind(native_output->backend);
        token.sidecar.kernel_queue_id = last_kernel_ticket_.execution_queue_id;
        token.sidecar.stage_timings.backprojection_stage_time_us += runtime::ElapsedUs(stage_start);

        return token;
    }

    ++kernel_sequence_;

    graph::gpu::accel::DeviceBufferView output{};
    output.backend = accel_backend;
    output.device_ptr = MakeSyntheticDevicePointer(in_view, kernel_sequence_);
    output.bytes = in_view.bytes;
    output.dtype = in_view.dtype;
    output.layout = in_view.layout;
    output.device_id = config_.backend_id;
    output.execution_queue_id =
        (config_.queue_id == 0u) ? (static_cast<std::uint64_t>(config_.backend_id) + 1u)
                                 : config_.queue_id;

    last_kernel_ticket_ = {};
    last_kernel_ticket_.backend = accel_backend;
    last_kernel_ticket_.kernel_id = config_.kernel_id;
    last_kernel_ticket_.launch.grid_x = config_.image_width;
    last_kernel_ticket_.launch.grid_y = 1;
    last_kernel_ticket_.launch.grid_z = 1;
    last_kernel_ticket_.launch.block_x = 1;
    last_kernel_ticket_.launch.block_y = 1;
    last_kernel_ticket_.launch.block_z = 1;
    last_kernel_ticket_.arg_count = 2;
    last_kernel_ticket_.execution_queue_id = output.execution_queue_id;
    last_kernel_ticket_.completion_event = NextOpaqueEventId();
    // PR2: ready_event is opaque transport metadata only. SAR identity derives from sidecar.
    output.ready_event = 0u;

    if (!graph::gpu::accel::IsValidView(output) ||
        !graph::gpu::accel::IsValidKernelTicket(last_kernel_ticket_)) {
        return std::nullopt;
    }

    auto token = input;
    token.device_view = output;
    token.has_device_view = true;
    token.kernel_ticket = last_kernel_ticket_;
    token.has_kernel_ticket = true;
    token.sidecar.backend_id = output.device_id;
    token.sidecar.backend = ToSarBackendKind(output.backend);
    token.sidecar.kernel_queue_id = output.execution_queue_id;
    token.sidecar.stage_timings.backprojection_stage_time_us += runtime::ElapsedUs(stage_start);

    return token;
}

bool SarBackprojectionTransformAccelNode::BindGpuCapabilities(graph::CapabilityBus& capability_bus) {
    native_kernel_bound_ = native_kernel_node_.BindGpuCapabilities(capability_bus);
    if (native_kernel_bound_) {
        ConfigureNativeKernel();
    }
    return native_kernel_bound_;
}

void SarBackprojectionTransformAccelNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("image_width")) {
        auto value = cfg.TryGetInt("image_width");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("image_width must be > 0");
        }
        config.image_width = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("backend_id")) {
        auto value = cfg.TryGetInt("backend_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("backend_id must be >= 0");
        }
        config.backend_id = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("queue_id")) {
        auto value = cfg.TryGetInt("queue_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("queue_id must be >= 0");
        }
        config.queue_id = static_cast<std::uint64_t>(value.value());
    }

    if (cfg.Contains("kernel_id")) {
        auto value = cfg.TryGetInt("kernel_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("kernel_id must be > 0");
        }
        config.kernel_id = static_cast<std::uint64_t>(value.value());
    }

    if (cfg.Contains("tap_count")) {
        auto value = cfg.TryGetInt("tap_count");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("tap_count must be > 0");
        }
        config.tap_count = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("delay_step")) {
        auto value = cfg.TryGetFloat("delay_step");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("delay_step must be >= 0");
        }
        config.delay_step = value.value();
    }

    if (cfg.Contains("phase_tap_scale")) {
        auto value = cfg.TryGetFloat("phase_tap_scale");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("phase_tap_scale must be >= 0");
        }
        config.phase_tap_scale = value.value();
    }

    if (cfg.Contains("phase_aperture_scale")) {
        auto value = cfg.TryGetFloat("phase_aperture_scale");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0.0f) {
            throw graph::ConfigError("phase_aperture_scale must be >= 0");
        }
        config.phase_aperture_scale = value.value();
    }

    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    SetConfig(config);
}

graph::JsonView SarBackprojectionTransformAccelNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["image_width"] = config_.image_width;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["queue_id"] = config_.queue_id;
    parameters_cache_["kernel_id"] = config_.kernel_id;
    parameters_cache_["tap_count"] = config_.tap_count;
    parameters_cache_["delay_step"] = config_.delay_step;
    parameters_cache_["phase_tap_scale"] = config_.phase_tap_scale;
    parameters_cache_["phase_aperture_scale"] = config_.phase_aperture_scale;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SarBackprojectionTransformAccelNode::GetParameterDescription(const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> SarBackprojectionTransformAccelNode::GetParameterNames() const {
    return {
        "image_width",
        "backend_id",
        "queue_id",
        "kernel_id",
        "tap_count",
        "delay_step",
        "phase_tap_scale",
        "phase_aperture_scale",
        "backend",
    };
}

void SarBackprojectionTransformAccelNode::SetConfig(
    const SarBackprojectionTransformAccelConfig& config) {
    config_ = config;
    if (native_kernel_bound_) {
        ConfigureNativeKernel();
    }
}

const SarBackprojectionTransformAccelConfig& SarBackprojectionTransformAccelNode::GetConfig() const noexcept {
    return config_;
}

const graph::gpu::accel::KernelTicket& SarBackprojectionTransformAccelNode::last_kernel_ticket() const noexcept {
    return last_kernel_ticket_;
}

bool SarBackprojectionTransformAccelNode::native_kernel_bound() const noexcept {
    return native_kernel_bound_;
}

void SarBackprojectionTransformAccelNode::ConfigureNativeKernel() {
    const auto queue_id =
        (config_.queue_id == 0u) ? (static_cast<std::uint64_t>(config_.backend_id) + 1u)
                                 : config_.queue_id;
    native_kernel_node_.ConfigureKernelDescriptor(
        MakeBackprojectionDescriptor(config_),
        config_.backend_id,
        queue_id);
}

} // namespace sar
