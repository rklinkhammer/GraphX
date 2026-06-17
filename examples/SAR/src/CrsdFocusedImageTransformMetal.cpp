// SPDX-License-Identifier: MIT

/**
 * @file CrsdFocusedImageTransformMetal.cpp
 * @brief GraphX source file.
 */

#include "sar/CrsdFocusedImageTransformMetal.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace sar {

namespace {

constexpr double kSpeedOfLight = 299792458.0;
constexpr double kDefaultWavelengthM = 0.03;

std::string ToLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

CrsdFocusedImageTransformConfig ToCpuConfig(
    const CrsdFocusedImageTransformMetalConfig& config) {
    CrsdFocusedImageTransformConfig cpu{};
    cpu.image_width = config.image_width;
    cpu.image_height = config.image_height;
    cpu.pixel_spacing_m = config.pixel_spacing_m;
    cpu.scene_center_x_m = config.scene_center_x_m;
    cpu.scene_center_y_m = config.scene_center_y_m;
    return cpu;
}

graph::gpu::metal::capabilities::MetalKernelDescriptor MakeFocusedImageDescriptor(
    const CrsdFocusedImageTransformMetalConfig& config) {
    graph::gpu::metal::capabilities::MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = config.kernel_id;
    descriptor.function_name = "graphx_crsd_focused_image_metal";
    descriptor.source_kind = graph::gpu::metal::capabilities::MetalKernelSourceKind::InlineSource;
    descriptor.source_payload =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "kernel void graphx_crsd_focused_image_metal(\n"
        "  const device float* phase_history [[buffer(0)]],\n"
        "  device float* image_out [[buffer(1)]],\n"
        "  uint gid [[thread_position_in_grid]]) {\n"
        "  image_out[gid] = phase_history[gid % 1024u] * 0.5f + image_out[gid] * 0.5f;\n"
        "}\n";
    descriptor.arg_layout = {
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadOnly},
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadWrite},
    };
    descriptor.dispatch.default_grid_x = std::max(1u, config.image_width * config.image_height);
    descriptor.dispatch.default_grid_y = 1;
    descriptor.dispatch.default_grid_z = 1;
    descriptor.dispatch.default_block_x = 1;
    descriptor.dispatch.default_block_y = 1;
    descriptor.dispatch.default_block_z = 1;
    return descriptor;
}

} // namespace

CrsdFocusedImageTransformMetalNode::CrsdFocusedImageTransformMetalNode()
    : cpu_transform_(ToCpuConfig(config_)),
      last_diagnostic_("unconfigured") {}

CrsdFocusedImageTransformMetalNode::CrsdFocusedImageTransformMetalNode(
    CrsdFocusedImageTransformMetalConfig config)
    : config_(std::move(config)),
      cpu_transform_(ToCpuConfig(config_)),
      last_diagnostic_("configured") {}

bool CrsdFocusedImageTransformMetalNode::BindGpuCapabilities(
    graph::CapabilityBus& capability_bus) {
    context_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    shared_queue_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    memory_pool_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    transfer_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();
    kernel_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalKernelCapability>();
    kernel_descriptor_ = std::dynamic_pointer_cast<
        graph::gpu::metal::capabilities::IMetalKernelDescriptorCapability>(kernel_);
    telemetry_ = capability_bus.Get<graph::gpu::metal::capabilities::IMetalTelemetryCapability>();

    capabilities_bound_ =
        (context_ != nullptr && memory_pool_ != nullptr && transfer_ != nullptr &&
         kernel_ != nullptr && telemetry_ != nullptr);
    native_kernel_registered_ = false;
    return capabilities_bound_;
}

std::optional<FocusedImageResult> CrsdFocusedImageTransformMetalNode::Transfer(
    const SarPhaseHistoryControlMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {

    const std::string backend = ToLower(config_.execution_backend);
    const bool wants_metal = (backend == "metal");

    if (wants_metal) {
        if (!IsNativeMetalAvailable()) {
            if (!config_.allow_fallback) {
                last_diagnostic_ = "focused_image_metal:native_unavailable_strict";
                return std::nullopt;
            }
            auto fallback = cpu_transform_.Transfer(
                input,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!fallback) {
                last_diagnostic_ = "focused_image_metal:fallback_cpu_rejected";
                return std::nullopt;
            }
            last_diagnostic_ = "focused_image_metal:cpu_fallback_explicit";
            return fallback;
        }

        return RunNativeMetal(input);
    }

    auto cpu = cpu_transform_.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    if (!cpu) {
        last_diagnostic_ = "focused_image_metal:cpu_path_rejected";
        return std::nullopt;
    }
    last_diagnostic_ = "ok:cpu_focused_image_produced";
    return cpu;
}

std::optional<FocusedImageResult> CrsdFocusedImageTransformMetalNode::RunNativeMetal(
    const SarPhaseHistoryControlMessage& input) {
    const auto& frame = input.frame;

    if (frame.control_marker == SarPhaseHistoryControlMarker::Watermark) {
        FocusedImageResult out{};
        out.control = input.control;
        last_diagnostic_ = "focused_image_metal:watermark_forwarded";
        return out;
    }
    if (frame.control_marker != SarPhaseHistoryControlMarker::EndOfStream) {
        last_diagnostic_ = "focused_image_metal:unsupported_control_marker";
        return std::nullopt;
    }
    if (frame.segments.empty() || frame.total_vector_count == 0u || frame.samples_per_vector == 0u) {
        last_diagnostic_ = "focused_image_metal:empty_payload_rejected";
        return std::nullopt;
    }

    if (!context_->SelectDevice(config_.backend_id)) {
        last_diagnostic_ = "focused_image_metal:device_select_failed";
        return std::nullopt;
    }

    if (!native_kernel_registered_) {
        const auto descriptor = MakeFocusedImageDescriptor(config_);
        bool registered = false;
        if (kernel_descriptor_) {
            registered = kernel_descriptor_->RegisterKernelDescriptor(descriptor);
        } else {
            registered = kernel_->RegisterKernel(config_.kernel_id, descriptor.function_name);
        }
        if (!registered) {
            last_diagnostic_ = "focused_image_metal:kernel_register_failed";
            return std::nullopt;
        }
        native_kernel_registered_ = true;
    }

    std::vector<float> phase_history;
    phase_history.reserve(static_cast<std::size_t>(frame.total_vector_count) *
                          static_cast<std::size_t>(frame.samples_per_vector));
    for (const auto& seg : frame.segments) {
        for (const auto& vec : seg.vectors) {
            for (const auto& sample : vec.samples) {
                phase_history.push_back(std::hypot(sample.real(), sample.imag()));
            }
        }
    }

    auto seed_image = BuildNativeSeedImage(frame);
    const std::uint64_t input_bytes = static_cast<std::uint64_t>(phase_history.size() * sizeof(float));
    const std::uint64_t output_bytes = static_cast<std::uint64_t>(seed_image.size() * sizeof(float));

    auto telemetry_before = telemetry_->Snapshot();

    auto t0 = std::chrono::steady_clock::now();
    graph::gpu::accel::BufferLease host_input{};
    if (!memory_pool_->AllocateHost(input_bytes, host_input) || !host_input.host_view.host_ptr) {
        last_diagnostic_ = "focused_image_metal:host_input_alloc_failed";
        return std::nullopt;
    }
    std::memcpy(host_input.host_view.host_ptr, phase_history.data(), static_cast<std::size_t>(input_bytes));

    graph::gpu::accel::BufferLease device_input{};
    if (!memory_pool_->AllocateDevice(input_bytes, config_.backend_id, device_input)) {
        last_diagnostic_ = "focused_image_metal:device_input_alloc_failed";
        return std::nullopt;
    }
    graph::gpu::accel::TransferTicket h2d_input{};
    if (!transfer_->EnqueueH2D(host_input.host_view, device_input.device_view, config_.h2d_queue_id, h2d_input)) {
        last_diagnostic_ = "focused_image_metal:h2d_input_failed";
        return std::nullopt;
    }
    telemetry_->RecordTransfer(h2d_input, 1);

    graph::gpu::accel::BufferLease host_seed{};
    if (!memory_pool_->AllocateHost(output_bytes, host_seed) || !host_seed.host_view.host_ptr) {
        last_diagnostic_ = "focused_image_metal:host_seed_alloc_failed";
        return std::nullopt;
    }
    std::memcpy(host_seed.host_view.host_ptr, seed_image.data(), static_cast<std::size_t>(output_bytes));

    graph::gpu::accel::BufferLease device_output{};
    if (!memory_pool_->AllocateDevice(output_bytes, config_.backend_id, device_output)) {
        last_diagnostic_ = "focused_image_metal:device_output_alloc_failed";
        return std::nullopt;
    }
    graph::gpu::accel::TransferTicket h2d_seed{};
    if (!transfer_->EnqueueH2D(host_seed.host_view, device_output.device_view, config_.h2d_queue_id, h2d_seed)) {
        last_diagnostic_ = "focused_image_metal:h2d_seed_failed";
        return std::nullopt;
    }
    telemetry_->RecordTransfer(h2d_seed, 1);

    if (config_.force_forward_only_guardrail && config_.require_kernel_execution) {
        last_diagnostic_ = "focused_image_metal:guardrail_forward_only_rejected";
        return std::nullopt;
    }

    graph::gpu::accel::KernelTicket kernel_ticket{};
    kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
    kernel_ticket.kernel_id = config_.kernel_id;
    kernel_ticket.arg_count = 2u;
    kernel_ticket.execution_queue_id = config_.kernel_queue_id;
    kernel_ticket.launch.grid_x = std::max(1u, config_.image_width * config_.image_height);
    kernel_ticket.launch.grid_y = 1;
    kernel_ticket.launch.grid_z = 1;
    kernel_ticket.launch.block_x = 1;
    kernel_ticket.launch.block_y = 1;
    kernel_ticket.launch.block_z = 1;
    kernel_ticket.completion_event = runtime::NextOpaqueEventId();

    auto in_arg = device_input.device_view;
    auto out_arg = device_output.device_view;
    graph::gpu::accel::DeviceBufferView* arg0 = &in_arg;
    graph::gpu::accel::DeviceBufferView* arg1 = &out_arg;
    void* const args[] = {arg0, arg1};
    if (!kernel_->Launch(kernel_ticket, args, 2)) {
        last_diagnostic_ = "focused_image_metal:kernel_launch_failed";
        return std::nullopt;
    }
    telemetry_->RecordKernel(kernel_ticket, 1);

    graph::gpu::accel::BufferLease host_output{};
    if (!memory_pool_->AllocateHost(output_bytes, host_output) || !host_output.host_view.host_ptr) {
        last_diagnostic_ = "focused_image_metal:host_output_alloc_failed";
        return std::nullopt;
    }
    graph::gpu::accel::TransferTicket d2h_output{};
    if (!transfer_->EnqueueD2H(device_output.device_view, host_output.host_view, config_.d2h_queue_id, d2h_output)) {
        last_diagnostic_ = "focused_image_metal:d2h_output_failed";
        return std::nullopt;
    }
    telemetry_->RecordTransfer(d2h_output, 1);

    auto t1 = std::chrono::steady_clock::now();

    auto telemetry_after = telemetry_->Snapshot();
    const auto kernel_dispatch_delta =
        telemetry_after.kernel_samples - telemetry_before.kernel_samples;
    if (config_.require_kernel_execution && kernel_dispatch_delta == 0u) {
        last_diagnostic_ = "focused_image_metal:kernel_dispatches_zero";
        return std::nullopt;
    }

    FocusedImageResult result{};
    result.control = input.control;
    result.pixels.resize(seed_image.size(), 0.0f);
    std::memcpy(result.pixels.data(), host_output.host_view.host_ptr, static_cast<std::size_t>(output_bytes));

    const std::size_t vector_count = static_cast<std::size_t>(frame.total_vector_count);
    const auto* first_vec = &frame.segments.front().vectors.front();
    const auto* last_vec = &frame.segments.back().vectors.back();
    double y_sum = 0.0;
    for (const auto& seg : frame.segments) {
        for (const auto& vec : seg.vectors) {
            y_sum += vec.platform_position_m[1];
        }
    }

    const double wavelength_m =
        (frame.carrier_hz > 0.0) ? (kSpeedOfLight / frame.carrier_hz) : kDefaultWavelengthM;
    const double range_spacing_m =
        (frame.sample_rate_hz > 0.0)
            ? (kSpeedOfLight / (2.0 * frame.sample_rate_hz))
            : (kSpeedOfLight / (2.0 * 10.0e6));

    result.grid.width = config_.image_width;
    result.grid.height = config_.image_height;
    result.grid.pixel_spacing_m = (config_.pixel_spacing_m > 0.0) ? config_.pixel_spacing_m : range_spacing_m;
    result.grid.scene_center_x_m = config_.scene_center_x_m;
    result.grid.scene_center_y_m = config_.scene_center_y_m;
    result.grid.range_origin_m = 0.0;
    result.grid.range_spacing_m = range_spacing_m;
    result.grid.wavelength_m = wavelength_m;
    result.grid.platform_x_start_m = first_vec->platform_position_m[0];
    result.grid.platform_x_end_m = last_vec->platform_position_m[0];
    result.grid.platform_y_m = y_sum / static_cast<double>(std::max<std::size_t>(vector_count, 1));

    std::uint64_t hash = 14695981039346656037ull;
    for (float p : result.pixels) {
        const auto q = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(std::llround(static_cast<double>(p) * 1.0e6)));
        hash ^= q;
        hash *= 1099511628211ull;
    }

    result.output_hash = hash;
    result.input_ordered_set_hash = frame.ordered_set_payload_hash;
    result.total_pulses = static_cast<std::uint32_t>(vector_count);
    result.samples_per_pulse = frame.samples_per_vector;
    result.ordered_crsd_segment_indices.reserve(frame.segments.size());
    result.per_segment_input_hashes.reserve(frame.segments.size());
    for (const auto& seg : frame.segments) {
        result.ordered_crsd_segment_indices.push_back(seg.segment_index);
        result.per_segment_input_hashes.push_back(seg.payload_hash);
    }
    result.lineage_complete_aperture = true;

    auto& sidecar = result.control.sidecar;
    sidecar.backend = SarBackendKind::NativeDevice;
    sidecar.backend_id = config_.backend_id;
    sidecar.h2d_queue_id = config_.h2d_queue_id;
    sidecar.kernel_queue_id = config_.kernel_queue_id;
    sidecar.d2h_queue_id = config_.d2h_queue_id;
    sidecar.bytes_h2d = h2d_input.src_host.bytes + h2d_seed.src_host.bytes;
    sidecar.bytes_d2h = d2h_output.dst_host.bytes;
    sidecar.kernel_dispatches = kernel_dispatch_delta;
    sidecar.transfer_h2d_time_us = 1u;
    sidecar.kernel_exec_time_us = 1u;
    sidecar.transfer_d2h_time_us = 1u;
    sidecar.stage_timings.backprojection_stage_time_us +=
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    result.control.transfer_ticket = d2h_output;
    result.control.has_transfer_ticket = true;
    result.control.kernel_ticket = kernel_ticket;
    result.control.has_kernel_ticket = true;

    last_diagnostic_ = "warning:metal_focused_image_algorithm_incomplete";
    return result;
}

std::vector<float> CrsdFocusedImageTransformMetalNode::BuildNativeSeedImage(
    const SarPhaseHistoryApertureFrame& frame) const {
    const std::size_t width = config_.image_width;
    const std::size_t height = config_.image_height;
    std::vector<float> pixels(width * height, 0.0f);

    std::vector<const SarPhaseHistoryVector*> vectors;
    vectors.reserve(static_cast<std::size_t>(frame.total_vector_count));
    for (const auto& seg : frame.segments) {
        for (const auto& vec : seg.vectors) {
            vectors.push_back(&vec);
        }
    }
    if (vectors.empty() || frame.samples_per_vector == 0u) {
        return pixels;
    }

    const std::size_t samples_per_vec = frame.samples_per_vector;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const auto* vec = vectors[i % vectors.size()];
        const auto& s = vec->samples[i % samples_per_vec];
        const float mag = std::hypot(s.real(), s.imag());
        const double geom =
            1.0 + 1.0e-4 * (vec->platform_position_m[0] + vec->platform_position_m[1]) +
            1.0e-6 * vec->rcv_time_s;
        pixels[i] = static_cast<float>(mag * static_cast<float>(geom));
    }

    return pixels;
}

void CrsdFocusedImageTransformMetalNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("image_width")) {
        auto val = cfg.TryGetInt("image_width");
        if (!val) { throw val.error(); }
        config.image_width = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("image_height")) {
        auto val = cfg.TryGetInt("image_height");
        if (!val) { throw val.error(); }
        config.image_height = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("pixel_spacing_m")) {
        auto val = cfg.TryGetFloat("pixel_spacing_m");
        if (!val) { throw val.error(); }
        config.pixel_spacing_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("scene_center_x_m")) {
        auto val = cfg.TryGetFloat("scene_center_x_m");
        if (!val) { throw val.error(); }
        config.scene_center_x_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("scene_center_y_m")) {
        auto val = cfg.TryGetFloat("scene_center_y_m");
        if (!val) { throw val.error(); }
        config.scene_center_y_m = static_cast<double>(val.value());
    }
    if (cfg.Contains("execution_backend")) {
        auto val = cfg.TryGetString("execution_backend");
        if (!val) { throw val.error(); }
        config.execution_backend = val.value();
    }
    if (cfg.Contains("allow_fallback")) {
        auto val = cfg.TryGetBool("allow_fallback");
        if (!val) { throw val.error(); }
        config.allow_fallback = val.value();
    }
    if (cfg.Contains("require_kernel_execution")) {
        auto val = cfg.TryGetBool("require_kernel_execution");
        if (!val) { throw val.error(); }
        config.require_kernel_execution = val.value();
    }
    if (cfg.Contains("force_forward_only_guardrail")) {
        auto val = cfg.TryGetBool("force_forward_only_guardrail");
        if (!val) { throw val.error(); }
        config.force_forward_only_guardrail = val.value();
    }
    if (cfg.Contains("backend_id")) {
        auto val = cfg.TryGetInt("backend_id");
        if (!val) { throw val.error(); }
        config.backend_id = static_cast<std::uint32_t>(val.value());
    }
    if (cfg.Contains("h2d_queue_id")) {
        auto val = cfg.TryGetInt("h2d_queue_id");
        if (!val) { throw val.error(); }
        config.h2d_queue_id = static_cast<std::uint64_t>(val.value());
    }
    if (cfg.Contains("kernel_queue_id")) {
        auto val = cfg.TryGetInt("kernel_queue_id");
        if (!val) { throw val.error(); }
        config.kernel_queue_id = static_cast<std::uint64_t>(val.value());
    }
    if (cfg.Contains("d2h_queue_id")) {
        auto val = cfg.TryGetInt("d2h_queue_id");
        if (!val) { throw val.error(); }
        config.d2h_queue_id = static_cast<std::uint64_t>(val.value());
    }
    if (cfg.Contains("kernel_id")) {
        auto val = cfg.TryGetInt("kernel_id");
        if (!val) { throw val.error(); }
        config.kernel_id = static_cast<std::uint64_t>(val.value());
    }

    if (config.image_width == 0u || config.image_height == 0u) {
        throw graph::ConfigError("image_width_or_height_must_be_positive");
    }

    const std::string backend = ToLower(config.execution_backend);
    if (!(backend == "auto" || backend == "metal")) {
        throw graph::ConfigError("execution_backend_must_be_auto_or_metal");
    }

    config_ = std::move(config);
    const auto cpu_cfg = ToCpuConfig(config_);
    nlohmann::json cpu_cfg_json = nlohmann::json::object();
    cpu_cfg_json["image_width"] = cpu_cfg.image_width;
    cpu_cfg_json["image_height"] = cpu_cfg.image_height;
    cpu_cfg_json["pixel_spacing_m"] = cpu_cfg.pixel_spacing_m;
    cpu_cfg_json["scene_center_x_m"] = cpu_cfg.scene_center_x_m;
    cpu_cfg_json["scene_center_y_m"] = cpu_cfg.scene_center_y_m;
    cpu_transform_.Configure(graph::JsonView(cpu_cfg_json));
    last_diagnostic_ = "configured";
}

graph::JsonView CrsdFocusedImageTransformMetalNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["image_width"] = config_.image_width;
    parameters_cache_["image_height"] = config_.image_height;
    parameters_cache_["pixel_spacing_m"] = config_.pixel_spacing_m;
    parameters_cache_["scene_center_x_m"] = config_.scene_center_x_m;
    parameters_cache_["scene_center_y_m"] = config_.scene_center_y_m;
    parameters_cache_["execution_backend"] = config_.execution_backend;
    parameters_cache_["allow_fallback"] = config_.allow_fallback;
    parameters_cache_["require_kernel_execution"] = config_.require_kernel_execution;
    parameters_cache_["force_forward_only_guardrail"] = config_.force_forward_only_guardrail;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["h2d_queue_id"] = config_.h2d_queue_id;
    parameters_cache_["kernel_queue_id"] = config_.kernel_queue_id;
    parameters_cache_["d2h_queue_id"] = config_.d2h_queue_id;
    parameters_cache_["kernel_id"] = config_.kernel_id;
    parameters_cache_["capabilities_bound"] = capabilities_bound_;
    parameters_cache_["algorithm_status"] = kAlgorithmStatus;
    parameters_cache_["claims_complete_native_algorithm"] = false;
    parameters_cache_["last_diagnostic"] = last_diagnostic_;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView CrsdFocusedImageTransformMetalNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            switch (field.type) {
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

std::vector<std::string> CrsdFocusedImageTransformMetalNode::GetParameterNames() const {
    return {
        "image_width",
        "image_height",
        "pixel_spacing_m",
        "scene_center_x_m",
        "scene_center_y_m",
        "execution_backend",
        "allow_fallback",
        "require_kernel_execution",
        "force_forward_only_guardrail",
        "backend_id",
        "h2d_queue_id",
        "kernel_queue_id",
        "d2h_queue_id",
        "kernel_id",
    };
}

const CrsdFocusedImageTransformMetalConfig&
CrsdFocusedImageTransformMetalNode::GetConfig() const noexcept {
    return config_;
}

const std::string&
CrsdFocusedImageTransformMetalNode::GetLastDiagnostic() const noexcept {
    return last_diagnostic_;
}

const char*
CrsdFocusedImageTransformMetalNode::GetAlgorithmStatus() const noexcept {
    return kAlgorithmStatus;
}

bool CrsdFocusedImageTransformMetalNode::IsNativeMetalAvailable() const {
    return capabilities_bound_ && context_ != nullptr && memory_pool_ != nullptr &&
           transfer_ != nullptr && kernel_ != nullptr && telemetry_ != nullptr;
}

std::uint64_t CrsdFocusedImageTransformMetalNode::ComputePayloadBytes(
    const SarPhaseHistoryApertureFrame& frame) noexcept {
    return static_cast<std::uint64_t>(frame.total_vector_count) *
           static_cast<std::uint64_t>(frame.samples_per_vector) *
           static_cast<std::uint64_t>(sizeof(std::complex<float>));
}

} // namespace sar
