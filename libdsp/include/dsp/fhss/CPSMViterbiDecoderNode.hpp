#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace dsp::fhss {

class CPSMViterbiDecoderNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSCpsmBranchMetricToken>,
          graph::TypeList<FHSSCpsmSymbolDecisionToken>,
          CPSMViterbiDecoderNode> {
public:
  using InputTokenType = FHSSCpsmBranchMetricToken;
  using OutputTokenType = FHSSCpsmSymbolDecisionToken;

  CPSMViterbiDecoderNode() = default;
  explicit CPSMViterbiDecoderNode(CPSMDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(CPSMDecoderConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    const auto &candidate = input.sidecar.candidate;
    if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
      return std::nullopt;
    }
    auto decoded = CPSMViterbiDecoderKernel::Decode(
        *candidate.complex_evidence.host_complex64_samples, config_);
    if (!decoded) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.pulse = candidate.pulse;
    output.sidecar.symbols = std::move(decoded->symbols);
    output.sidecar.phase_states = std::move(decoded->phase_states);
    output.sidecar.best_path_metric = decoded->best_path_metric;
    output.sidecar.confidence = decoded->confidence;
    output.sidecar.status = FHSSGraphXDecodeStatus::Ok;
    output.sidecar.status_message = "Ok";
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  CPSMDecoderConfig config_{};
};

} // namespace dsp::fhss
