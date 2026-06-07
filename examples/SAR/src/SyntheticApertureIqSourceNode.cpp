#include "sar/SyntheticApertureIqSourceNode.hpp"

namespace sar {

SyntheticApertureIqSourceNode::SyntheticApertureIqSourceNode(
    SyntheticApertureIqSourceConfig config)
    : config_(config) {}

std::optional<SarPulseBlockMessage> SyntheticApertureIqSourceNode::Produce(
    std::integral_constant<std::size_t, 0>) {
    if (eos_emitted_) {
        return std::nullopt;
    }

    if (next_sequence_id_ >= config_.total_pulses) {
        eos_emitted_ = true;
        return MakeEndOfStreamMessage();
    }

    SarPulseBlockMessage out = MakeDataMessage();
    ++next_sequence_id_;
    return out;
}

void SyntheticApertureIqSourceNode::Reset() {
    next_sequence_id_ = 0;
    eos_emitted_ = false;
}

void SyntheticApertureIqSourceNode::SetConfig(const SyntheticApertureIqSourceConfig& config) {
    config_ = config;
    Reset();
}

const SyntheticApertureIqSourceConfig& SyntheticApertureIqSourceNode::GetConfig() const noexcept {
    return config_;
}

SarIqSample SyntheticApertureIqSourceNode::MakeSample(
    std::uint64_t sequence_id,
    std::uint32_t sample_index) {
    const float seq = static_cast<float>(sequence_id);
    const float idx = static_cast<float>(sample_index);
    return SarIqSample(seq + (idx * 0.001f), (seq * 0.25f) - (idx * 0.0015f));
}

SarPulseBlockMessage SyntheticApertureIqSourceNode::MakeDataMessage() const {
    SarPulseBlockMessage out{};
    out.envelope.sequence_id = next_sequence_id_;
    out.envelope.stream_id = config_.stream_id;
    out.envelope.tile_id = 0;
    out.envelope.tile_count = 1;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::Data;
    out.envelope.synthetic = true;

    out.buffer.buffer_id = next_sequence_id_;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;

    out.iq_samples.reserve(config_.samples_per_pulse);
    for (std::uint32_t i = 0; i < config_.samples_per_pulse; ++i) {
        out.iq_samples.push_back(MakeSample(next_sequence_id_, i));
    }
    out.buffer.byte_count = out.iq_samples.size() * sizeof(SarIqSample);

    return out;
}

SarPulseBlockMessage SyntheticApertureIqSourceNode::MakeEndOfStreamMessage() const {
    SarPulseBlockMessage out{};
    out.envelope.sequence_id = next_sequence_id_;
    out.envelope.stream_id = config_.stream_id;
    out.envelope.tile_id = 0;
    out.envelope.tile_count = 1;
    out.envelope.backend_id = config_.backend_id;
    out.envelope.backend = config_.backend;
    out.envelope.marker = SarFrameMarker::EndOfStream;
    out.envelope.synthetic = true;

    out.buffer.buffer_id = next_sequence_id_;
    out.buffer.device_index = config_.backend_id;
    out.buffer.backend = config_.backend;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    out.buffer.byte_count = 0;

    return out;
}

} // namespace sar
