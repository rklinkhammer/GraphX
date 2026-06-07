#include "sar/H2DAsyncNode.hpp"

#include "config/ConfigError.hpp"

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

void* MakeSyntheticDevicePointer(const SarRangeTileMessage& msg) noexcept {
    const auto token =
        ((msg.buffer.buffer_id + 1u) << 8u) |
        (static_cast<std::uint64_t>(msg.envelope.tile_id) + 1u);
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
}

} // namespace

std::optional<SarRangeTileMessage> H2DAsyncNode::Transfer(
    const SarRangeTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = std::chrono::steady_clock::now();
    SarRangeTileMessage out = input;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    if (config_.override_backend) {
        out.envelope.backend_id = config_.backend_id;
        out.envelope.backend = config_.backend;
        out.buffer.device_index = config_.backend_id;
        out.buffer.backend = config_.backend;
    }

    const auto accel_backend = ToAccelBackendKind(out.envelope.backend);
    if (accel_backend != graph::gpu::accel::BackendKind::Unknown && !out.range_bins.empty()) {
        const auto element_count = static_cast<std::uint64_t>(out.range_bins.size());
        const auto byte_count = static_cast<std::uint64_t>(out.buffer.byte_count);
        const auto layout = MakeAccelVectorLayout(element_count);

        out.gpu.host_view.backend = accel_backend;
        out.gpu.host_view.host_ptr = out.range_bins.data();
        out.gpu.host_view.bytes = byte_count;
        out.gpu.host_view.dtype = graph::gpu::accel::DataType::Float32;
        out.gpu.host_view.layout = layout;
        out.gpu.host_view.allocator_id = 1;
        out.gpu.has_host_view = true;

        out.gpu.device_view.backend = accel_backend;
        out.gpu.device_view.device_ptr = MakeSyntheticDevicePointer(out);
        out.gpu.device_view.bytes = byte_count;
        out.gpu.device_view.dtype = graph::gpu::accel::DataType::Float32;
        out.gpu.device_view.layout = layout;
        out.gpu.device_view.device_id = out.envelope.backend_id;
        out.gpu.device_view.execution_queue_id = static_cast<std::uint64_t>(out.envelope.backend_id) + 1u;
        out.gpu.device_view.ready_event =
            ((out.envelope.sequence_id + 1u) * 10u) + out.envelope.tile_id + 1u;
        out.gpu.has_device_view = true;

        out.gpu.lease.pool_id = 1;
        out.gpu.lease.allocation_id = out.buffer.buffer_id + 1u;
        out.gpu.lease.release_policy = graph::gpu::accel::ReleasePolicy::AutoOnGraphCompletion;
        out.gpu.lease.device_view = out.gpu.device_view;
        out.gpu.lease.host_view = out.gpu.host_view;
        out.gpu.has_lease = true;

        out.gpu.transfer_ticket.backend = accel_backend;
        out.gpu.transfer_ticket.transfer_id =
            ((out.envelope.sequence_id + 1u) * 1000u) + out.envelope.tile_id + 1u;
        out.gpu.transfer_ticket.execution_queue_id = out.gpu.device_view.execution_queue_id;
        out.gpu.transfer_ticket.completion_event = out.gpu.device_view.ready_event;
        out.gpu.transfer_ticket.src_host = out.gpu.host_view;
        out.gpu.transfer_ticket.dst_device = out.gpu.device_view;
        out.gpu.has_transfer_ticket = true;
    }

    if (input.envelope.marker == SarFrameMarker::Data) {
        const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - stage_start)
                                    .count();
        out.transfer_h2d_time_us += static_cast<std::uint64_t>(
            (elapsed_us <= 0) ? 1 : elapsed_us);
    }
    return out;
}

void H2DAsyncNode::Configure(const graph::JsonView& cfg) {
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

    if (cfg.Contains("backend")) {
        auto value = cfg.TryGetInt("backend");
        if (!value) {
            throw value.error();
        }
        config.backend = ParseBackendKind(value.value());
    }

    SetConfig(config);
}

graph::JsonView H2DAsyncNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["override_backend"] = config_.override_backend;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView H2DAsyncNode::GetParameterDescription(
    const std::string& param_name) const {
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

std::vector<std::string> H2DAsyncNode::GetParameterNames() const {
    return {
        "override_backend",
        "backend_id",
        "backend",
    };
}

void H2DAsyncNode::SetConfig(const H2DAsyncConfig& config) {
    config_ = config;
}

const H2DAsyncConfig& H2DAsyncNode::GetConfig() const noexcept {
    return config_;
}

} // namespace sar
