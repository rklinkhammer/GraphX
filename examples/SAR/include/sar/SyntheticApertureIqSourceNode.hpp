#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sar {

struct SyntheticApertureIqSourceConfig {
    std::uint32_t stream_id{0};
    std::uint32_t total_pulses{32};
    std::uint32_t samples_per_pulse{256};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class SyntheticApertureIqSourceNode
    : public graph::NamedSourceNode<SyntheticApertureIqSourceNode, SarPulseBlockMessage> {
public:
    SyntheticApertureIqSourceNode() = default;
    explicit SyntheticApertureIqSourceNode(SyntheticApertureIqSourceConfig config);

    std::optional<SarPulseBlockMessage> Produce(
        std::integral_constant<std::size_t, 0>) override;

    void Reset();
    void SetConfig(const SyntheticApertureIqSourceConfig& config);
    const SyntheticApertureIqSourceConfig& GetConfig() const noexcept;

private:
    static SarIqSample MakeSample(std::uint64_t sequence_id, std::uint32_t sample_index);

    SarPulseBlockMessage MakeDataMessage() const;
    SarPulseBlockMessage MakeEndOfStreamMessage() const;

    SyntheticApertureIqSourceConfig config_{};
    std::uint64_t next_sequence_id_{0};
    bool eos_emitted_{false};
};

} // namespace sar
