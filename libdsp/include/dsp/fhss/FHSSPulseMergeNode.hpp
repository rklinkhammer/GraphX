#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

class FHSSPulseMergeNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDetectedPulseToken>,
          graph::TypeList<FHSSPulseCandidateToken>, FHSSPulseMergeNode> {
public:
  using InputTokenType = FHSSDetectedPulseToken;
  using OutputTokenType = FHSSPulseCandidateToken;

  FHSSPulseMergeNode() = default;
  explicit FHSSPulseMergeNode(FHSSPulseMergeConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseMergeConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (input.sidecar.detected_pulses.size() !=
        input.sidecar.pulse_evidence.size()) {
      return std::nullopt;
    }

    std::vector<FHSSLocalPulseDetection> local_detections;
    local_detections.reserve(input.sidecar.detected_pulses.size());
    for (std::size_t i = 0; i < input.sidecar.detected_pulses.size(); ++i) {
      local_detections.push_back(FHSSLocalPulseDetectionFromGraphX(
          input.sidecar.detected_pulses[i], input.sidecar.pulse_evidence[i]));
    }

    auto merged = FHSSPulseMergeKernel::Merge(local_detections, config_);
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.ordered_candidates.reserve(merged.ordered_candidates.size());
    for (const auto &candidate : merged.ordered_candidates) {
      output.sidecar.ordered_candidates.push_back(
          FHSSGraphXPulseCandidateFromMergeCandidate(candidate));
    }
    output.sidecar.globally_ordered = true;
    output.sidecar.unsupported_overlap_rejected = true;
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  FHSSPulseMergeConfig config_{};
};

} // namespace dsp::fhss
