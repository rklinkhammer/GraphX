#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace dsp::fhss {

class CPSMBranchMetricNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSPulseCandidateToken>,
          graph::TypeList<FHSSCpsmBranchMetricToken>, CPSMBranchMetricNode> {
public:
  using InputTokenType = FHSSPulseCandidateToken;
  using OutputTokenType = FHSSCpsmBranchMetricToken;

  CPSMBranchMetricNode() = default;
  explicit CPSMBranchMetricNode(CPSMDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(CPSMDecoderConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (input.sidecar.ordered_candidates.empty()) {
      return std::nullopt;
    }
    const auto &candidate = input.sidecar.ordered_candidates.front();
    if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
      return std::nullopt;
    }
    auto metrics = CPSMBranchMetricKernel::Compute(
        *candidate.complex_evidence.host_complex64_samples, config_);
    if (!metrics) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.candidate = candidate;
    output.sidecar.trellis_state_count =
        CPSMPhaseStateCount(config_.modulation_index);
    output.sidecar.branch_costs.reserve(metrics->size());
    for (const auto &metric : *metrics) {
      output.sidecar.branch_costs.push_back(metric.cost);
    }
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  CPSMDecoderConfig config_{};
};

} // namespace dsp::fhss
