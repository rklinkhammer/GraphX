#include "sar/SarBackprojectionTransformNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
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

} // namespace

SarBackprojectionTransformNode::SarBackprojectionTransformNode(
    SarBackprojectionTransformConfig config)
    : config_(config) {}

std::optional<SarImageTileMessage> SarBackprojectionTransformNode::Transfer(
    const SarRangeTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (input.envelope.marker == SarFrameMarker::EndOfStream) {
        return BuildEndOfStreamTile(input);
    }
    const auto stage_start = std::chrono::steady_clock::now();
    auto out = BuildDataTile(input);
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - stage_start)
                                .count();
    out.kernel_exec_time_us += static_cast<std::uint64_t>((elapsed_us <= 0) ? 1 : elapsed_us);
    return out;
}

void SarBackprojectionTransformNode::Configure(const graph::JsonView& cfg) {
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
        config.queue_id = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("kernel_id")) {
        auto value = cfg.TryGetInt("kernel_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("kernel_id must be >= 0");
        }
        config.kernel_id = static_cast<std::uint32_t>(value.value());
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

graph::JsonView SarBackprojectionTransformNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["image_width"] = config_.image_width;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["queue_id"] = config_.queue_id;
    parameters_cache_["kernel_id"] = config_.kernel_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SarBackprojectionTransformNode::GetParameterDescription(
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

std::vector<std::string> SarBackprojectionTransformNode::GetParameterNames() const {
    return {
        "image_width",
        "backend_id",
        "queue_id",
        "kernel_id",
        "backend",
    };
}

void SarBackprojectionTransformNode::SetConfig(
    const SarBackprojectionTransformConfig& config) {
    config_ = config;
}

const SarBackprojectionTransformConfig& SarBackprojectionTransformNode::GetConfig() const noexcept {
    return config_;
}

std::uint32_t SarBackprojectionTransformNode::ResolveImageWidth() const noexcept {
    return std::max<std::uint32_t>(1u, config_.image_width);
}

SarImageTileMessage SarBackprojectionTransformNode::BuildDataTile(
    const SarRangeTileMessage& input) const {
    SarImageTileMessage out{};
    const std::uint32_t width = ResolveImageWidth();
    const std::uint32_t sample_count = static_cast<std::uint32_t>(input.range_bins.size());
    const std::uint32_t height = (sample_count == 0u) ? 1u : ((sample_count + width - 1u) / width);

    out.envelope = input.envelope;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::Data;
    out.envelope.synthetic = input.envelope.synthetic;

    out.buffer.buffer_id = input.buffer.buffer_id;
    out.buffer.byte_count = input.range_bins.size() * sizeof(float);
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::DeviceToHost;

    out.dispatch.queue_id = config_.queue_id;
    out.dispatch.kernel_id = config_.kernel_id;
    out.dispatch.dispatch_width = width;
    out.dispatch.dispatch_height = height;
    out.dispatch.dispatch_depth = 1;
    out.gpu = input.gpu;
    out.transfer_h2d_time_us = input.transfer_h2d_time_us;
    out.kernel_exec_time_us = input.kernel_exec_time_us;
    out.transfer_d2h_time_us = input.transfer_d2h_time_us;
    out.gpu.kernel_ticket.backend = ToAccelBackendKind(config_.backend);
    out.gpu.kernel_ticket.kernel_id = config_.kernel_id;
    out.gpu.kernel_ticket.launch.grid_x = width;
    out.gpu.kernel_ticket.launch.grid_y = height;
    out.gpu.kernel_ticket.launch.grid_z = 1;
    out.gpu.kernel_ticket.launch.block_x = 1;
    out.gpu.kernel_ticket.launch.block_y = 1;
    out.gpu.kernel_ticket.launch.block_z = 1;
    out.gpu.kernel_ticket.arg_count = input.gpu.has_device_view ? 1u : 0u;
    out.gpu.kernel_ticket.execution_queue_id =
        static_cast<std::uint64_t>(config_.queue_id) + 1u;
    out.gpu.kernel_ticket.completion_event =
        ((input.envelope.sequence_id + 1u) * 100u) + input.envelope.tile_id + 1u;
    out.gpu.has_kernel_ticket =
        out.gpu.kernel_ticket.backend != graph::gpu::accel::BackendKind::Unknown;

    out.width = width;
    out.height = height;
    out.pixels.reserve(input.range_bins.size());

    const float tile_scale = 1.0f + (static_cast<float>(input.envelope.tile_id) * 0.01f);
    const float seq_scale = 1.0f + (static_cast<float>(input.envelope.sequence_id) * 0.001f);
    for (std::size_t i = 0; i < input.range_bins.size(); ++i) {
        const float idx_scale = 1.0f + (static_cast<float>(i) * 0.0001f);
        out.pixels.push_back(input.range_bins[i] * tile_scale * seq_scale * idx_scale);
    }

    return out;
}

SarImageTileMessage SarBackprojectionTransformNode::BuildEndOfStreamTile(
    const SarRangeTileMessage& input) const {
    SarImageTileMessage out{};

    out.envelope = input.envelope;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::EndOfStream;
    out.envelope.synthetic = input.envelope.synthetic;

    out.buffer.buffer_id = input.buffer.buffer_id;
    out.buffer.byte_count = 0;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::DeviceToHost;

    out.dispatch.queue_id = config_.queue_id;
    out.dispatch.kernel_id = config_.kernel_id;
    out.dispatch.dispatch_width = ResolveImageWidth();
    out.dispatch.dispatch_height = 1;
    out.dispatch.dispatch_depth = 1;
    out.gpu = input.gpu;
    out.transfer_h2d_time_us = input.transfer_h2d_time_us;
    out.kernel_exec_time_us = input.kernel_exec_time_us;
    out.transfer_d2h_time_us = input.transfer_d2h_time_us;

    out.width = ResolveImageWidth();
    out.height = 1;

    return out;
}

} // namespace sar
