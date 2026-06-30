// SPDX-License-Identifier: MIT

/**
 * @file H2DAsyncAccelNode.cpp
 * @brief GraphX source file.
 */

#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <chrono>

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
            return SarBackendKind::SimulatedDevice;
        default:
            return SarBackendKind::Host;
    }
}

} // namespace

std::optional<SarControlToken> H2DAsyncAccelNode::Transfer(
    const SarControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = runtime::SteadyClock::now();

    if (!input.has_host_view) {
        return std::nullopt;
    }

    const auto& host_view = input.host_view;
    const auto accel_backend =
        config_.override_backend ? ToAccelBackendKind(config_.backend) : host_view.backend;
    if (accel_backend == graph::gpu::accel::BackendKind::Unknown ||
        !graph::gpu::accel::IsValidView(host_view)) {
        return std::nullopt;
    }

    ++transfer_sequence_;

    graph::gpu::accel::DeviceBufferView output{};
    output.backend = accel_backend;
    output.bytes = host_view.bytes;
    output.dtype = host_view.dtype;
    output.layout = host_view.layout;
    output.device_id = config_.backend_id;
    output.execution_queue_id =
        (config_.queue_id == 0u) ? (static_cast<std::uint64_t>(config_.backend_id) + 1u)
                                 : config_.queue_id;
    output = runtime::MakeSyntheticDeviceView(output, transfer_sequence_);

    if (!graph::gpu::accel::IsValidView(output)) {
        return std::nullopt;
    }

    last_lease_ = {};
    last_lease_.pool_id = 1;
    last_lease_.allocation_id = transfer_sequence_;
    last_lease_.release_policy = graph::gpu::accel::ReleasePolicy::AutoOnGraphCompletion;
    last_lease_.host_view = host_view;
    last_lease_.device_view = output;

    last_transfer_ticket_ = {};
    last_transfer_ticket_.backend = accel_backend;
    last_transfer_ticket_.transfer_id = transfer_sequence_;
    last_transfer_ticket_.execution_queue_id = output.execution_queue_id;
    last_transfer_ticket_.src_host = host_view;
    last_transfer_ticket_.dst_device = output;
    last_transfer_ticket_ = runtime::MakeSyntheticTransferTicket(last_transfer_ticket_);

    auto token = input;
    token.device_view = output;
    token.has_device_view = true;
    token.lease = last_lease_;
    token.has_lease = true;
    token.transfer_ticket = last_transfer_ticket_;
    token.has_transfer_ticket = true;
    token.sidecar.backend_id = output.device_id;
    token.sidecar.backend = ToSarBackendKind(output.backend);
    token.sidecar.h2d_queue_id = output.execution_queue_id;
    token.sidecar.stage_timings.h2d_stage_time_us += runtime::ElapsedUs(stage_start);

    return token;
}

void H2DAsyncAccelNode::Configure(const graph::JsonView& cfg) {
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

graph::JsonView H2DAsyncAccelNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["override_backend"] = config_.override_backend;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["queue_id"] = config_.queue_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView H2DAsyncAccelNode::GetParameterDescription(const std::string& param_name) const {
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

std::vector<std::string> H2DAsyncAccelNode::GetParameterNames() const {
    return {
        "override_backend",
        "backend_id",
        "queue_id",
        "backend",
    };
}

void H2DAsyncAccelNode::SetConfig(const H2DAsyncAccelConfig& config) {
    config_ = config;
}

const H2DAsyncAccelConfig& H2DAsyncAccelNode::GetConfig() const noexcept {
    return config_;
}

const graph::gpu::accel::TransferTicket& H2DAsyncAccelNode::last_transfer_ticket() const noexcept {
    return last_transfer_ticket_;
}

const graph::gpu::accel::BufferLease& H2DAsyncAccelNode::last_lease() const noexcept {
    return last_lease_;
}

} // namespace sar
