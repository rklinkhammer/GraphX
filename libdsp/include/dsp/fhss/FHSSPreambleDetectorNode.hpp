#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSPreambleDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDecodedPulseWordsToken>,
          graph::TypeList<FHSSAssembledMessageToken>,
          FHSSPreambleDetectorNode> {
public:
  using InputTokenType = FHSSDecodedPulseWordsToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSPreambleDetectorNode() = default;
  explicit FHSSPreambleDetectorNode(std::vector<FHSSPreamblePulseSpec> preamble)
      : preamble_(std::move(preamble)) {}

  void SetPreamble(std::vector<FHSSPreamblePulseSpec> preamble) {
    preamble_ = std::move(preamble);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    std::vector<FHSSDecodedPulseWord> decoded;
    decoded.reserve(input.sidecar.decoded_pulses.size());
    for (const auto &packet : input.sidecar.decoded_pulses) {
      decoded.push_back(FHSSDecodedPulseWordFromGraphX(packet));
    }
    decoded = GloballyOrderDecodedPulses(std::move(decoded));

    auto lock = FHSSPreambleDetectorKernel::Detect(decoded, preamble_);
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.ordered_pulses = input.sidecar.decoded_pulses;
    output.sidecar.active_frequency_indices = lock.active_frequency_indices;
    output.sidecar.preamble_lock = lock.preamble_lock;
    output.sidecar.diagnostics.pulse_count = input.sidecar.decoded_pulses.size();
    output.sidecar.diagnostics.preamble_lock = lock.preamble_lock;
    output.sidecar.status = lock.status == FHSSMessageAssemblyStatus::Ok
                                ? FHSSGraphXDecodeStatus::Ok
                                : FHSSGraphXDecodeStatus::InvalidEvidence;
    output.sidecar.status_message = lock.status_message;
    return output;
  }

private:
  std::vector<FHSSPreamblePulseSpec> preamble_{};
};

} // namespace dsp::fhss
