#include "sar/ImageTileMergeNode.hpp"

#include <algorithm>

namespace sar {

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
        const std::uint32_t missing_tiles =
            (received_tiles_ >= expected_tiles) ? 0u : (expected_tiles - received_tiles_);
        const bool complete =
            (missing_tiles == 0u) &&
            (!config_.require_watermark_before_complete || watermark_seen_);

        completion_emitted_ = true;
        return BuildStatusMessage(input, SarFrameMarker::EndOfStream, complete);
    }

    if (!has_first_data_sequence_) {
        first_data_sequence_ = input.envelope.sequence_id;
        has_first_data_sequence_ = true;
    }

    bytes_d2h_ += static_cast<std::uint64_t>(input.buffer.byte_count);
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

void ImageTileMergeNode::Reset() {
    seen_tiles_.clear();
    received_tiles_ = 0;
    duplicate_tiles_ = 0;
    out_of_order_tiles_ = 0;
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
