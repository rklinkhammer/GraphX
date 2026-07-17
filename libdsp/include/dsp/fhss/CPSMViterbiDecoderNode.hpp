#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace dsp::fhss {

class CPSMViterbiDecoderNode : public graph::NamedInteriorNode<
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
    OutputTokenType output{};
    output.token_id = input.token_id;
    const auto &metrics =
        input.sidecar.pulse_metrics.empty()
            ? std::vector<FHSSCpsmPulseBranchMetric>{FHSSCpsmPulseBranchMetric{
                  .candidate = input.sidecar.candidate}}
            : input.sidecar.pulse_metrics;

    output.sidecar.pulse_decisions.reserve(metrics.size());
    for (const auto &metric : metrics) {
      const auto &candidate = metric.candidate;
      if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
        return std::nullopt;
      }
      auto evidence_samples =
          FHSSGraphXComplexEvidenceSamples(candidate.complex_evidence);
      if (evidence_samples.empty()) {
        return std::nullopt;
      }
      evidence_samples = FHSSExpandDecimatedCpsmEvidence(
          evidence_samples,
          candidate.pulse.timing.sample_time_map.decimation_factor);
      if (evidence_samples.empty()) {
        return std::nullopt;
      }
      auto decoded =
          CPSMViterbiDecoderKernel::Decode(evidence_samples, config_);
      if (!decoded) {
        return std::nullopt;
      }

      FHSSCpsmPulseSymbolDecision decision{};
      decision.pulse = candidate.pulse;
      decision.symbols = std::move(decoded->symbols);
      decision.phase_states = std::move(decoded->phase_states);
      decision.best_path_metric = decoded->best_path_metric;
      decision.confidence = decoded->confidence;
      decision.status = FHSSGraphXDecodeStatus::Ok;
      decision.status_message = "Ok";
      if (output.sidecar.pulse_decisions.empty()) {
        output.sidecar.pulse = decision.pulse;
        output.sidecar.symbols = decision.symbols;
        output.sidecar.phase_states = decision.phase_states;
        output.sidecar.best_path_metric = decision.best_path_metric;
        output.sidecar.confidence = decision.confidence;
        output.sidecar.status = decision.status;
        output.sidecar.status_message = decision.status_message;
      }
      output.sidecar.pulse_decisions.push_back(std::move(decision));
    }
    last_decision_pulse_count_ = output.sidecar.pulse_decisions.size();
    return output;
  }

  [[nodiscard]] std::size_t LastDecisionPulseCount() const noexcept {
    return last_decision_pulse_count_;
  }

private:
  CPSMDecoderConfig config_{};
  std::size_t last_decision_pulse_count_ = 0;
};

} // namespace dsp::fhss
