#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSPulseWordDecoder.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSPulseWordDecoderNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSCpsmSymbolDecisionToken>,
          graph::TypeList<FHSSDecodedPulseWordsToken>,
          FHSSPulseWordDecoderNode> {
public:
  using InputTokenType = FHSSCpsmSymbolDecisionToken;
  using OutputTokenType = FHSSDecodedPulseWordsToken;

  FHSSPulseWordDecoderNode() = default;
  explicit FHSSPulseWordDecoderNode(FHSSPulseWordDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseWordDecoderConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    OutputTokenType output{};
    output.token_id = input.token_id;
    const auto &decisions = input.sidecar.pulse_decisions.empty()
                                ? std::vector<FHSSCpsmPulseSymbolDecision>{
                                      FHSSCpsmPulseSymbolDecision{
                                          .pulse = input.sidecar.pulse,
                                          .symbols = input.sidecar.symbols,
                                          .phase_states =
                                              input.sidecar.phase_states,
                                          .best_path_metric =
                                              input.sidecar.best_path_metric,
                                          .confidence =
                                              input.sidecar.confidence,
                                          .status = input.sidecar.status,
                                          .status_message =
                                              input.sidecar.status_message}}
                                : input.sidecar.pulse_decisions;
    output.sidecar.decoded_pulses.reserve(decisions.size());
    for (const auto &decision : decisions) {
      CPSMViterbiResult viterbi{};
      viterbi.symbols = decision.symbols;
      viterbi.phase_states = decision.phase_states;
      viterbi.best_path_metric = decision.best_path_metric;
      viterbi.confidence = decision.confidence;

      FHSSPulseCandidate candidate{};
      candidate.detected_pulse = FHSSDetectedPulseFromGraphX(decision.pulse);
      auto decoded =
          FHSSPulseWordDecoderKernel::Decode(candidate, viterbi, config_);
      auto packet = FHSSGraphXDecodedPulseWordFromKernel(decoded);
      packet.pulse = decision.pulse;
      output.sidecar.decoded_pulses.push_back(std::move(packet));
    }
    output.sidecar.globally_ordered = true;
    return output;
  }

private:
  FHSSPulseWordDecoderConfig config_{};
};

} // namespace dsp::fhss
