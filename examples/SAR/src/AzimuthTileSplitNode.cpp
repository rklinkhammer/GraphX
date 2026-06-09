#include "sar/AzimuthTileSplitNode.hpp"

#include "sar/SarAccelTokenSidecarStore.hpp"

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

std::uint64_t EncodeAccelToken(const SarPulseBlockMessage& input,
                               std::uint32_t tile_id,
                               std::size_t byte_count,
                               SarFrameMarker marker) {
    const auto marker_bits = static_cast<std::uint64_t>(marker) & 0x3u;
    const auto tile_bits = static_cast<std::uint64_t>(tile_id) & 0xFFFu;
    const auto sequence_bits = static_cast<std::uint64_t>(input.envelope.sequence_id) & 0xFFFFFFu;
    const auto byte_bits = static_cast<std::uint64_t>(byte_count) & 0xFFFFu;
    const auto stream_bits = static_cast<std::uint64_t>(input.envelope.stream_id) & 0x3FFu;

    return marker_bits |
           (tile_bits << 2u) |
           (sequence_bits << 14u) |
           (byte_bits << 38u) |
           (stream_bits << 54u);
}

graph::gpu::accel::HostPinnedBufferView BuildHostView(const SarPulseBlockMessage& input,
                                                      std::uint32_t tile_id,
                                                      std::size_t token_byte_count,
                                                      std::size_t view_byte_count,
                                                      SarFrameMarker marker,
                                                      const AzimuthTileSplitConfig& config) {
    const auto token = EncodeAccelToken(input, tile_id, token_byte_count, marker);

    detail::AccelTokenSidecar sidecar{};
    sidecar.sequence_id = input.envelope.sequence_id;
    sidecar.batch_id = input.envelope.batch_id;
    sidecar.aperture_id = input.envelope.aperture_id;
    sidecar.pulse_range_start = input.envelope.pulse_range_start;
    sidecar.pulse_range_count = input.envelope.pulse_range_count;
    sidecar.stream_id = input.envelope.stream_id;
    sidecar.tile_id = tile_id;
    sidecar.tile_count = std::max<std::uint32_t>(1u, config.tile_count);
    sidecar.backend_id = input.envelope.backend_id;
    sidecar.backend = input.envelope.backend;
    sidecar.marker = marker;
    sidecar.synthetic = input.envelope.synthetic;
    sidecar.payload_byte_count = token_byte_count;
    detail::StoreAccelTokenSidecar(token, sidecar);

    graph::gpu::accel::HostPinnedBufferView out{};
    const auto backend = ToAccelBackendKind(config.backend);
    out.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    out.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(token));
    out.bytes = static_cast<std::uint64_t>(view_byte_count);
    out.dtype = graph::gpu::accel::DataType::Float32;
    out.layout = MakeAccelVectorLayout(static_cast<std::uint64_t>(
        std::max<std::size_t>(1u, view_byte_count / sizeof(float))));
    out.allocator_id = static_cast<std::uint64_t>(config.backend_id) + 1u;
    return out;
}

} // namespace

AzimuthTileSplitNode::AzimuthTileSplitNode(AzimuthTileSplitConfig config)
    : config_(config) {}

std::optional<graph::gpu::accel::HostPinnedBufferView> AzimuthTileSplitNode::Transfer(
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

graph::gpu::accel::HostPinnedBufferView AzimuthTileSplitNode::BuildDataTile(
    const SarPulseBlockMessage& input) const {
    const std::uint32_t tile_id = ResolveTileId(input);
    const std::size_t byte_count =
        (input.iq_samples.empty() ? 0u : input.iq_samples.size() * sizeof(float));

    return BuildHostView(input, tile_id, byte_count, byte_count, SarFrameMarker::Data, config_);
}

graph::gpu::accel::HostPinnedBufferView AzimuthTileSplitNode::BuildEndOfStreamTile(
    const SarPulseBlockMessage& input) const {
    const auto tile_id = ResolveTileId(input);
    return BuildHostView(
        input,
        tile_id,
        0u,
        sizeof(float),
        SarFrameMarker::EndOfStream,
        config_);
}

} // namespace sar
