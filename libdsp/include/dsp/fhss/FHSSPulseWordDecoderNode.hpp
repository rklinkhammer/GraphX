#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSPulseWordDecoder.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace dsp::fhss {

class FHSSPulseWordDecoderNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSCpsmSymbolDecisionToken>,
          graph::TypeList<FHSSDecodedPulseWordToken>,
          FHSSPulseWordDecoderNode> {
public:
  using InputTokenType = FHSSCpsmSymbolDecisionToken;
  using OutputTokenType = FHSSDecodedPulseWordToken;

  FHSSPulseWordDecoderNode() = default;
  explicit FHSSPulseWordDecoderNode(FHSSPulseWordDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseWordDecoderConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    CPSMViterbiResult viterbi{};
    viterbi.symbols = input.sidecar.symbols;
    viterbi.phase_states = input.sidecar.phase_states;
    viterbi.best_path_metric = input.sidecar.best_path_metric;
    viterbi.confidence = input.sidecar.confidence;

    FHSSPulseCandidate candidate{};
    candidate.detected_pulse = FHSSDetectedPulseFromGraphX(input.sidecar.pulse);
    auto decoded =
        FHSSPulseWordDecoderKernel::Decode(candidate, viterbi, config_);

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = FHSSGraphXDecodedPulseWordFromKernel(decoded);
    return output;
  }

private:
  FHSSPulseWordDecoderConfig config_{};
};

} // namespace dsp::fhss
