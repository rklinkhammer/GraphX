#include "sar/SarBackprojectionTransformAccelNode.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

namespace sar {

namespace {

SarBackendKind ParseBackendKind(int raw_backend) {
    if (raw_backend < static_cast<int>(SarBackendKind::Host) ||
        raw_backend > static_cast<int>(SarBackendKind::NativeDevice)) {
        throw graph::ConfigError("backend must be in range [0,2]");
    }
    return static_cast<SarBackendKind>(raw_backend);
}

void* MakeSyntheticDevicePointer(const graph::gpu::accel::DeviceBufferView& input,
                                 std::uint64_t kernel_sequence) noexcept {
    const auto token =
        ((static_cast<std::uint64_t>(input.bytes) + 1u) << 8u) |
        ((kernel_sequence + 1u) & 0xFFu);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}

} // namespace

SarBackprojectionTransformAccelNode::SarBackprojectionTransformAccelNode(
    SarBackprojectionTransformAccelConfig config)
    : config_(config) {}

std::optional<graph::gpu::accel::DeviceBufferView> SarBackprojectionTransformAccelNode::Transfer(
    const graph::gpu::accel::DeviceBufferView& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!graph::gpu::accel::IsValidView(input)) {
        return std::nullopt;
    }

    const auto accel_backend = ToAccelBackendKind(config_.backend);
    if (accel_backend == graph::gpu::accel::BackendKind::Unknown) {
        return std::nullopt;
    }

    ++kernel_sequence_;

    graph::gpu::accel::DeviceBufferView output{};
    output.backend = accel_backend;
    output.device_ptr = MakeSyntheticDevicePointer(input, kernel_sequence_);
    output.bytes = input.bytes;
    output.dtype = input.dtype;
    output.layout = input.layout;
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
    last_kernel_ticket_.arg_count = 1;
    last_kernel_ticket_.execution_queue_id = output.execution_queue_id;
    last_kernel_ticket_.completion_event = kernel_sequence_;

    output.ready_event = input.ready_event;

    if (!graph::gpu::accel::IsValidView(output) ||
        !graph::gpu::accel::IsValidKernelTicket(last_kernel_ticket_)) {
        return std::nullopt;
    }

    return output;
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
        "backend",
    };
}

void SarBackprojectionTransformAccelNode::SetConfig(
    const SarBackprojectionTransformAccelConfig& config) {
    config_ = config;
}

const SarBackprojectionTransformAccelConfig& SarBackprojectionTransformAccelNode::GetConfig() const noexcept {
    return config_;
}

const graph::gpu::accel::KernelTicket& SarBackprojectionTransformAccelNode::last_kernel_ticket() const noexcept {
    return last_kernel_ticket_;
}

} // namespace sar
