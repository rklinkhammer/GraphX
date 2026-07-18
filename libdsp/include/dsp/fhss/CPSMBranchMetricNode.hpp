#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <string>
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
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.edge_control = input.edge_control;
    output.sidecar.correlation = input.sidecar.correlation;
    if (input.sidecar.ordered_candidates.empty()) {
      last_metric_pulse_count_ = 0;
      last_rejection_reason_.clear();
      return output;
    }

    output.sidecar.pulse_metrics.reserve(
        input.sidecar.ordered_candidates.size());
    for (const auto &candidate : input.sidecar.ordered_candidates) {
      if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
        last_rejection_reason_ = "candidate has no usable complex evidence";
        return std::nullopt;
      }
      auto evidence_samples =
          FHSSGraphXComplexEvidenceSamples(candidate.complex_evidence);
      if (evidence_samples.empty()) {
        last_rejection_reason_ = "candidate evidence range is empty or invalid";
        return std::nullopt;
      }
      evidence_samples = FHSSExpandDecimatedCpsmEvidence(
          evidence_samples,
          candidate.pulse.timing.sample_time_map.decimation_factor);
      if (evidence_samples.empty()) {
        last_rejection_reason_ =
            "candidate decimation cannot be expanded to canonical evidence";
        return std::nullopt;
      }
      auto metrics = CPSMBranchMetricKernel::Compute(evidence_samples, config_);
      if (!metrics) {
        last_rejection_reason_ = metrics.error().message;
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
    last_metric_pulse_count_ = output.sidecar.pulse_metrics.size();
    last_rejection_reason_.clear();
    return output;
  }

  [[nodiscard]] std::size_t LastMetricPulseCount() const noexcept {
    return last_metric_pulse_count_;
  }
  [[nodiscard]] const std::string &LastRejectionReason() const noexcept {
    return last_rejection_reason_;
  }

private:
  CPSMDecoderConfig config_{};
  std::size_t last_metric_pulse_count_ = 0;
  std::string last_rejection_reason_;
};

} // namespace dsp::fhss
