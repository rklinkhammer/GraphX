#pragma once

#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>

namespace dsp::fhss {

class FHSSCorrelatorBankDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSSyntheticIqToken>,
          graph::TypeList<FHSSDetectedPulseToken>,
          FHSSCorrelatorBankDetectorNode> {
public:
  using InputTokenType = FHSSSyntheticIqToken;
  using OutputTokenType = FHSSDetectedPulseToken;

  FHSSCorrelatorBankDetectorNode() = default;
  explicit FHSSCorrelatorBankDetectorNode(
      FHSSCorrelatorBankDetectorConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSCorrelatorBankDetectorConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return std::nullopt;
    }

    auto result = FHSSCorrelatorBankDetectorKernel::Detect(
        *input.sidecar.iq.host_complex64_samples, config_);
    if (!result) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.source_iq = input.sidecar.iq;
    output.sidecar.detected_pulses.reserve(result->local_detections.size());
    output.sidecar.pulse_evidence.reserve(result->local_detections.size());
    for (const auto &local : result->local_detections) {
      auto normalized = NormalizeLocalDetection(local);
      if (!normalized) {
        return std::nullopt;
      }
      const auto map =
          FHSSGraphXSampleTimeMapFromMergeMap(local.sample_time_map);
      output.sidecar.detected_pulses.push_back(
          FHSSGraphXPulseMetadataFromDetectedPulse(
              normalized->candidate.detected_pulse, map));
      output.sidecar.pulse_evidence.push_back(
          FHSSGraphXComplexEvidenceFromMergeEvidence(local.complex_evidence,
                                                     map));
    }
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  FHSSCorrelatorBankDetectorConfig config_{};
};

} // namespace dsp::fhss
