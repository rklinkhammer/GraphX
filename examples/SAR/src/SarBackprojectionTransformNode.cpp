#include "sar/SarBackprojectionTransformNode.hpp"

#include <algorithm>

namespace sar {

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

    return BuildDataTile(input);
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

    out.envelope.sequence_id = input.envelope.sequence_id;
    out.envelope.stream_id = input.envelope.stream_id;
    out.envelope.tile_id = input.envelope.tile_id;
    out.envelope.tile_count = input.envelope.tile_count;
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

    out.envelope.sequence_id = input.envelope.sequence_id;
    out.envelope.stream_id = input.envelope.stream_id;
    out.envelope.tile_id = input.envelope.tile_id;
    out.envelope.tile_count = input.envelope.tile_count;
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

    out.width = ResolveImageWidth();
    out.height = 1;

    return out;
}

} // namespace sar
