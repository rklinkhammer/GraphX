#include "sar/D2HAsyncAccelNode.hpp"

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

} // namespace

std::optional<graph::gpu::accel::HostPinnedBufferView> D2HAsyncAccelNode::Transfer(
    const graph::gpu::accel::DeviceBufferView& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!graph::gpu::accel::IsValidView(input)) {
        return std::nullopt;
    }

    const auto accel_backend =
        config_.override_backend ? ToAccelBackendKind(config_.backend) : input.backend;
    if (accel_backend == graph::gpu::accel::BackendKind::Unknown) {
        return std::nullopt;
    }

    ++transfer_sequence_;

    graph::gpu::accel::HostPinnedBufferView output{};
    output.backend = accel_backend;
    output.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(input.ready_event));
    output.bytes = input.bytes;
    output.dtype = input.dtype;
    output.layout = input.layout;
    output.allocator_id = 2;

    if (!graph::gpu::accel::IsValidView(output)) {
        return std::nullopt;
    }

    const auto effective_queue =
        (config_.queue_id == 0u) ? (static_cast<std::uint64_t>(config_.backend_id) + 1u)
                                 : config_.queue_id;

    last_lease_ = {};
    last_lease_.pool_id = 2;
    last_lease_.allocation_id = transfer_sequence_;
    last_lease_.release_policy = graph::gpu::accel::ReleasePolicy::AutoOnGraphCompletion;
    last_lease_.device_view = input;
    last_lease_.host_view = output;

    last_transfer_ticket_ = {};
    last_transfer_ticket_.backend = accel_backend;
    last_transfer_ticket_.transfer_id = transfer_sequence_;
    last_transfer_ticket_.execution_queue_id = effective_queue;
    last_transfer_ticket_.completion_event = transfer_sequence_;
    last_transfer_ticket_.src_device = input;
    last_transfer_ticket_.dst_host = output;

    return output;
}

void D2HAsyncAccelNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("override_backend")) {
        auto value = cfg.TryGetBool("override_backend");
        if (!value) {
            throw value.error();
        }
        config.override_backend = value.value();
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

    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    SetConfig(config);
}

graph::JsonView D2HAsyncAccelNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["override_backend"] = config_.override_backend;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["queue_id"] = config_.queue_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView D2HAsyncAccelNode::GetParameterDescription(const std::string& param_name) const {
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

std::vector<std::string> D2HAsyncAccelNode::GetParameterNames() const {
    return {
        "override_backend",
        "backend_id",
        "queue_id",
        "backend",
    };
}

void D2HAsyncAccelNode::SetConfig(const D2HAsyncAccelConfig& config) {
    config_ = config;
}

const D2HAsyncAccelConfig& D2HAsyncAccelNode::GetConfig() const noexcept {
    return config_;
}

const graph::gpu::accel::TransferTicket& D2HAsyncAccelNode::last_transfer_ticket() const noexcept {
    return last_transfer_ticket_;
}

const graph::gpu::accel::BufferLease& D2HAsyncAccelNode::last_lease() const noexcept {
    return last_lease_;
}

} // namespace sar
