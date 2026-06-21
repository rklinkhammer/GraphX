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

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.pulse_metrics.reserve(
        input.sidecar.ordered_candidates.size());
    for (const auto &candidate : input.sidecar.ordered_candidates) {
      if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
        return std::nullopt;
      }
      auto evidence_samples =
          FHSSGraphXComplexEvidenceSamples(candidate.complex_evidence);
      if (evidence_samples.empty()) {
        return std::nullopt;
      }
      auto metrics = CPSMBranchMetricKernel::Compute(
          evidence_samples, config_);
      if (!metrics) {
        return std::nullopt;
      }

      FHSSCpsmPulseBranchMetric pulse_metric{};
      pulse_metric.candidate = candidate;
      pulse_metric.trellis_state_count =
          CPSMPhaseStateCount(config_.modulation_index);
      pulse_metric.branch_costs.reserve(metrics->size());
      for (const auto &metric : *metrics) {
        pulse_metric.branch_costs.push_back(metric.cost);
      }
      if (output.sidecar.pulse_metrics.empty()) {
        output.sidecar.candidate = pulse_metric.candidate;
        output.sidecar.trellis_state_count = pulse_metric.trellis_state_count;
        output.sidecar.branch_costs = pulse_metric.branch_costs;
      }
      output.sidecar.pulse_metrics.push_back(std::move(pulse_metric));
    }
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  CPSMDecoderConfig config_{};
};

} // namespace dsp::fhss
