#include "sar/AzimuthTileSplitNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>

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

AzimuthTileSplitNode::AzimuthTileSplitNode(AzimuthTileSplitConfig config)
    : config_(config) {}

std::optional<SarRangeTileMessage> AzimuthTileSplitNode::Transfer(
    const SarPulseBlockMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (input.envelope.marker == SarFrameMarker::EndOfStream) {
        return BuildEndOfStreamTile(input);
    }

    return BuildDataTile(input);
}

void AzimuthTileSplitNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("tile_count")) {
        auto value = cfg.TryGetInt("tile_count");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("tile_count must be > 0");
        }
        config.tile_count = static_cast<std::uint32_t>(value.value());
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

    if (cfg.Contains("tile_id_offset")) {
        auto value = cfg.TryGetInt("tile_id_offset");
        if (!value) {
            throw value.error();
        }
        if (value.value() < 0) {
            throw graph::ConfigError("tile_id_offset must be >= 0");
        }
        config.tile_id_offset = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("fixed_tile_id")) {
        auto value = cfg.TryGetInt("fixed_tile_id");
        if (!value) {
            throw value.error();
        }
        if (value.value() < -1) {
            throw graph::ConfigError("fixed_tile_id must be >= -1");
        }
        config.fixed_tile_id = value.value();
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

graph::JsonView AzimuthTileSplitNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["tile_count"] = config_.tile_count;
    parameters_cache_["tile_id_offset"] = config_.tile_id_offset;
    parameters_cache_["fixed_tile_id"] = config_.fixed_tile_id;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView AzimuthTileSplitNode::GetParameterDescription(
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

std::vector<std::string> AzimuthTileSplitNode::GetParameterNames() const {
    return {
        "tile_count",
        "tile_id_offset",
        "fixed_tile_id",
        "backend_id",
        "backend",
    };
}

void AzimuthTileSplitNode::SetConfig(const AzimuthTileSplitConfig& config) {
    config_ = config;
}

const AzimuthTileSplitConfig& AzimuthTileSplitNode::GetConfig() const noexcept {
    return config_;
}

std::uint32_t AzimuthTileSplitNode::ResolveTileId(const SarPulseBlockMessage& input) const {
    const std::uint32_t tile_count = std::max<std::uint32_t>(1u, config_.tile_count);
    if (config_.fixed_tile_id >= 0) {
        return static_cast<std::uint32_t>(config_.fixed_tile_id) % tile_count;
    }

    const auto sequence_tile =
        static_cast<std::uint32_t>(input.envelope.sequence_id % tile_count);
    return (sequence_tile + (config_.tile_id_offset % tile_count)) % tile_count;
}

SarRangeTileMessage AzimuthTileSplitNode::BuildDataTile(const SarPulseBlockMessage& input) const {
    SarRangeTileMessage out{};
    const std::uint32_t tile_count = std::max<std::uint32_t>(1u, config_.tile_count);
    const std::uint32_t tile_id = ResolveTileId(input);

    out.envelope = input.envelope;
    out.envelope.tile_id = tile_id;
    out.envelope.tile_count = tile_count;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::Data;
    out.envelope.synthetic = input.envelope.synthetic;

    out.buffer.buffer_id = input.buffer.buffer_id;
    out.buffer.byte_count = input.iq_samples.size() * sizeof(float);
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;

    out.range_bins.reserve(input.iq_samples.size());
    for (const SarIqSample& sample : input.iq_samples) {
        out.range_bins.push_back(sample.real());
    }

    return out;
}

SarRangeTileMessage AzimuthTileSplitNode::BuildEndOfStreamTile(const SarPulseBlockMessage& input) const {
    SarRangeTileMessage out{};
    const std::uint32_t tile_count = std::max<std::uint32_t>(1u, config_.tile_count);

    out.envelope = input.envelope;
    out.envelope.tile_id = ResolveTileId(input);
    out.envelope.tile_count = tile_count;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::EndOfStream;
    out.envelope.synthetic = input.envelope.synthetic;

    out.buffer.buffer_id = input.buffer.buffer_id;
    out.buffer.byte_count = 0;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;

    return out;
}

} // namespace sar
