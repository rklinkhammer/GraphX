#include "sar/D2HAsyncAccelNode.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <chrono>
#include <atomic>

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

void* OpaqueHostPointer() noexcept {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1u));
}

std::uint64_t NextOpaqueEventId() {
    static std::atomic<std::uint64_t> next_event{1u};
    return next_event.fetch_add(1u, std::memory_order_relaxed);
}

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedUs(const Clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start);
    const auto count = static_cast<std::uint64_t>(elapsed.count());
    return (count == 0u) ? 1u : count;
}

} // namespace

std::optional<SarAccelControlToken> D2HAsyncAccelNode::Transfer(
    const SarAccelControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = Clock::now();

    if (!input.has_device_view || !graph::gpu::accel::IsValidView(input.device_view)) {
        return std::nullopt;
    }

    const auto& device_view = input.device_view;

    const auto accel_backend =
        config_.override_backend ? ToAccelBackendKind(config_.backend) : device_view.backend;
    if (accel_backend == graph::gpu::accel::BackendKind::Unknown) {
        return std::nullopt;
    }

    ++transfer_sequence_;

    graph::gpu::accel::HostPinnedBufferView output{};
    output.backend = accel_backend;
    // PR2: host_ptr is opaque transport metadata only. SAR identity derives from sidecar.
    output.host_ptr = OpaqueHostPointer();
    output.bytes = device_view.bytes;
    output.dtype = device_view.dtype;
    output.layout = device_view.layout;
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
    last_lease_.device_view = device_view;
    last_lease_.host_view = output;

    last_transfer_ticket_ = {};
    last_transfer_ticket_.backend = accel_backend;
    last_transfer_ticket_.transfer_id = transfer_sequence_;
    last_transfer_ticket_.execution_queue_id = effective_queue;
    last_transfer_ticket_.completion_event = NextOpaqueEventId();
    last_transfer_ticket_.src_device = device_view;
    last_transfer_ticket_.dst_host = output;

    auto token = input;
    token.host_view = output;
    token.has_host_view = true;
    token.lease = last_lease_;
    token.has_lease = true;
    token.transfer_ticket = last_transfer_ticket_;
    token.has_transfer_ticket = true;
    token.sidecar.backend_id = device_view.device_id;
    token.sidecar.backend = ToSarBackendKind(device_view.backend);
    token.sidecar.d2h_queue_id = effective_queue;
    token.sidecar.stage_timings.d2h_stage_time_us += ElapsedUs(stage_start);

    return token;
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
