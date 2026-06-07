#include "sar/AzimuthTileSplitNode.hpp"

#include <algorithm>

namespace sar {

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

void AzimuthTileSplitNode::SetConfig(const AzimuthTileSplitConfig& config) {
    config_ = config;
}

const AzimuthTileSplitConfig& AzimuthTileSplitNode::GetConfig() const noexcept {
    return config_;
}

SarRangeTileMessage AzimuthTileSplitNode::BuildDataTile(const SarPulseBlockMessage& input) const {
    SarRangeTileMessage out{};
    const std::uint32_t tile_count = std::max<std::uint32_t>(1u, config_.tile_count);
    const std::uint32_t tile_id = static_cast<std::uint32_t>(input.envelope.sequence_id % tile_count);

    out.envelope.sequence_id = input.envelope.sequence_id;
    out.envelope.stream_id = input.envelope.stream_id;
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

    out.envelope.sequence_id = input.envelope.sequence_id;
    out.envelope.stream_id = input.envelope.stream_id;
    out.envelope.tile_id = static_cast<std::uint32_t>(input.envelope.sequence_id % tile_count);
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
