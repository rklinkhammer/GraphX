#include "sar/ImageTileMergeNode.hpp"

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

ImageTileMergeNode::ImageTileMergeNode(ImageTileMergeConfig config)
    : config_(config) {}

std::optional<SarMergeStatusMessage> ImageTileMergeNode::Transfer(
    const SarImageTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (completion_emitted_) {
        return std::nullopt;
    }

    const std::uint32_t expected_tiles = ResolveExpectedTiles();

    if (input.envelope.marker == SarFrameMarker::Watermark) {
        watermark_seen_ = true;
        return BuildStatusMessage(input, SarFrameMarker::Watermark, false);
    }

    if (input.envelope.marker == SarFrameMarker::EndOfStream) {
        if (config_.require_all_tile_eos_before_complete) {
            eos_tiles_.insert(input.envelope.tile_id);
        }
        const std::uint32_t missing_tiles =
            (received_tiles_ >= expected_tiles) ? 0u : (expected_tiles - received_tiles_);
        const bool all_required_eos_seen =
            !config_.require_all_tile_eos_before_complete ||
            (eos_tiles_.size() >= expected_tiles);
        const bool complete =
            (missing_tiles == 0u) &&
            (!config_.require_watermark_before_complete || watermark_seen_) &&
            all_required_eos_seen;

        if (complete) {
            completion_emitted_ = true;
        }
        return BuildStatusMessage(input, SarFrameMarker::EndOfStream, complete);
    }

    if (!has_first_data_sequence_) {
        first_data_sequence_ = input.envelope.sequence_id;
        has_first_data_sequence_ = true;
    }

    const auto transfer_bytes = static_cast<std::uint64_t>(input.buffer.byte_count);
    bytes_h2d_ += transfer_bytes;
    bytes_d2h_ += transfer_bytes;
    ++kernel_dispatches_;

    const auto [_, inserted] = seen_tiles_.insert(input.envelope.tile_id);
    if (!inserted) {
        ++duplicate_tiles_;
    } else {
        ++received_tiles_;

        if (has_last_tile_ && input.envelope.tile_id < last_tile_id_) {
            ++out_of_order_tiles_;
        }
        last_tile_id_ = input.envelope.tile_id;
        has_last_tile_ = true;
    }

    return BuildStatusMessage(input, SarFrameMarker::Data, false);
}

void ImageTileMergeNode::Configure(const graph::JsonView& cfg) {
    auto config = config_;

    if (cfg.Contains("expected_tiles")) {
        auto value = cfg.TryGetInt("expected_tiles");
        if (!value) {
            throw value.error();
        }
        if (value.value() <= 0) {
            throw graph::ConfigError("expected_tiles must be > 0");
        }
        config.expected_tiles = static_cast<std::uint32_t>(value.value());
    }

    if (cfg.Contains("require_watermark_before_complete")) {
        auto value = cfg.TryGetBool("require_watermark_before_complete");
        if (!value) {
            throw value.error();
        }
        config.require_watermark_before_complete = value.value();
    }

    if (cfg.Contains("require_all_tile_eos_before_complete")) {
        auto value = cfg.TryGetBool("require_all_tile_eos_before_complete");
        if (!value) {
            throw value.error();
        }
        config.require_all_tile_eos_before_complete = value.value();
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

graph::JsonView ImageTileMergeNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["expected_tiles"] = config_.expected_tiles;
    parameters_cache_["require_watermark_before_complete"] =
        config_.require_watermark_before_complete;
    parameters_cache_["require_all_tile_eos_before_complete"] =
        config_.require_all_tile_eos_before_complete;
    parameters_cache_["backend_id"] = config_.backend_id;
    parameters_cache_["backend"] = static_cast<int>(config_.backend);
    return graph::JsonView(parameters_cache_);
}

graph::JsonView ImageTileMergeNode::GetParameterDescription(
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

std::vector<std::string> ImageTileMergeNode::GetParameterNames() const {
    return {
        "expected_tiles",
        "require_watermark_before_complete",
        "require_all_tile_eos_before_complete",
        "backend_id",
        "backend",
    };
}

void ImageTileMergeNode::Reset() {
    seen_tiles_.clear();
    eos_tiles_.clear();
    received_tiles_ = 0;
    duplicate_tiles_ = 0;
    out_of_order_tiles_ = 0;
    bytes_h2d_ = 0;
    bytes_d2h_ = 0;
    kernel_dispatches_ = 0;
    last_tile_id_ = 0;
    has_last_tile_ = false;
    watermark_seen_ = false;
    completion_emitted_ = false;
    first_data_sequence_ = 0;
    has_first_data_sequence_ = false;
}

void ImageTileMergeNode::SetConfig(const ImageTileMergeConfig& config) {
    config_ = config;
    Reset();
}

const ImageTileMergeConfig& ImageTileMergeNode::GetConfig() const noexcept {
    return config_;
}

SarMergeStatusMessage ImageTileMergeNode::BuildStatusMessage(
    const SarImageTileMessage& input,
    SarFrameMarker marker,
    bool complete) const {
    const std::uint32_t expected_tiles = ResolveExpectedTiles();
    const std::uint32_t missing_tiles =
        (received_tiles_ >= expected_tiles) ? 0u : (expected_tiles - received_tiles_);

    SarMergeStatusMessage out{};
    out.envelope.sequence_id = input.envelope.sequence_id;
    out.envelope.stream_id = input.envelope.stream_id;
    out.envelope.tile_id = input.envelope.tile_id;
    out.envelope.tile_count = expected_tiles;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = marker;
    out.envelope.synthetic = input.envelope.synthetic;

    out.expected_tiles = expected_tiles;
    out.received_tiles = received_tiles_;
    out.duplicate_tiles = duplicate_tiles_;
    out.missing_tiles = missing_tiles;
    out.out_of_order_tiles = out_of_order_tiles_;
    out.bytes_h2d = bytes_h2d_;
    out.bytes_d2h = bytes_d2h_;
    out.kernel_dispatches = kernel_dispatches_;
    out.watermark_seen = watermark_seen_;
    out.complete = complete;

    if (has_first_data_sequence_ && input.envelope.sequence_id >= first_data_sequence_) {
        out.fanin_wait_ms = input.envelope.sequence_id - first_data_sequence_;
    }

    return out;
}

std::uint32_t ImageTileMergeNode::ResolveExpectedTiles() const noexcept {
    return std::max<std::uint32_t>(1u, config_.expected_tiles);
}

} // namespace sar
