#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "config/ConfigError.hpp"

#include <chrono>
#include <algorithm>
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

std::uint64_t NextOpaqueTokenId() {
    static std::atomic<std::uint64_t> next_id{1u};
    return next_id.fetch_add(1u, std::memory_order_relaxed);
}

SarAccelControlToken BuildToken(const SarAccelControlToken& input,
                                std::uint32_t tile_id,
                                std::size_t payload_byte_count,
                                std::size_t view_byte_count,
                                SarFrameMarker marker,
                                const AzimuthTileSplitConfig& config) {
    SarAccelControlToken out = input;
    out.token_id = NextOpaqueTokenId();
    out.sidecar.tile_id = tile_id;
    out.sidecar.tile_count = std::max<std::uint32_t>(1u, config.tile_count);
    out.sidecar.marker = marker;
    out.sidecar.payload_byte_count = payload_byte_count;

    auto& host_view = out.host_view;
    const auto backend = ToAccelBackendKind(config.backend);
    host_view.backend =
        (backend == graph::gpu::accel::BackendKind::Unknown) ? graph::gpu::accel::BackendKind::Metal
                                                              : backend;
    // host_ptr is opaque transport metadata only. SAR identity derives from sidecar.
    host_view.host_ptr = runtime::OpaqueHostPointer();
    host_view.bytes = static_cast<std::uint64_t>(view_byte_count);
    host_view.dtype = graph::gpu::accel::DataType::Float32;
    host_view.layout = MakeAccelVectorLayout(static_cast<std::uint64_t>(
        std::max<std::size_t>(1u, view_byte_count / sizeof(float))));
    host_view.allocator_id = static_cast<std::uint64_t>(config.backend_id) + 1u;
    out.has_host_view = true;
    return out;
}

} // namespace

AzimuthTileSplitNode::AzimuthTileSplitNode(AzimuthTileSplitConfig config)
    : config_(config) {}

std::optional<SarAccelControlToken> AzimuthTileSplitNode::Transfer(
    const SarAccelControlToken& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    const auto stage_start = runtime::SteadyClock::now();

    SarAccelControlToken token{};
    if (input.sidecar.marker == SarFrameMarker::EndOfStream) {
        token = BuildEndOfStreamTile(input);
    } else {
        token = BuildDataTile(input);
    }
    token.sidecar.stage_timings.split_time_us += runtime::ElapsedUs(stage_start);
    return token;
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

std::uint32_t AzimuthTileSplitNode::ResolveTileId(const SarAccelControlToken& input) const {
    const std::uint32_t tile_count = std::max<std::uint32_t>(1u, config_.tile_count);
    if (config_.fixed_tile_id >= 0) {
        return static_cast<std::uint32_t>(config_.fixed_tile_id) % tile_count;
    }

    const auto sequence_tile =
        static_cast<std::uint32_t>(input.sidecar.sequence_id % tile_count);
    return (sequence_tile + (config_.tile_id_offset % tile_count)) % tile_count;
}

SarAccelControlToken AzimuthTileSplitNode::BuildDataTile(
    const SarAccelControlToken& input) const {
    const std::uint32_t tile_id = ResolveTileId(input);
    const std::size_t byte_count =
        static_cast<std::size_t>(input.has_host_view ? input.host_view.bytes : input.sidecar.payload_byte_count);

    return BuildToken(input, tile_id, byte_count, byte_count, SarFrameMarker::Data, config_);
}

SarAccelControlToken AzimuthTileSplitNode::BuildEndOfStreamTile(
    const SarAccelControlToken& input) const {
    const auto tile_id = ResolveTileId(input);
    return BuildToken(
        input,
        tile_id,
        0u,
        sizeof(float),
        SarFrameMarker::EndOfStream,
        config_);
}

} // namespace sar
