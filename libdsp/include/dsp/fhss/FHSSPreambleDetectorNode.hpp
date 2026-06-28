#pragma once

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "graph/IConfigurable.hpp"
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
          FHSSPreambleDetectorNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using InputTokenType = FHSSDecodedPulseWordsToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSPreambleDetectorNode() = default;
  explicit FHSSPreambleDetectorNode(std::vector<FHSSPreamblePulseSpec> preamble)
      : preamble_(std::move(preamble)) {}

  void SetPreamble(std::vector<FHSSPreamblePulseSpec> preamble) {
    preamble_ = std::move(preamble);
  }

  void Configure(const graph::JsonView &cfg) override {
    SetPreamble(FHSSPreamblePulseSpecsFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    return FHSSFixtureParametersJson();
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &param_name) const override {
    return FHSSFixtureParameterDescription(param_name);
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return FHSSFixtureParameterNames();
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
    output.sidecar.diagnostics.unsupported_overlap_rejected = true;
    output.sidecar.diagnostics.unsupported_impairments_rejected = true;
    output.sidecar.diagnostics.synchronization_assumption =
        "known message_start_sample = 0";
    if (!input.sidecar.decoded_pulses.empty()) {
      const auto &first = input.sidecar.decoded_pulses.front();
      output.sidecar.diagnostics.global_start_sample =
          first.pulse.timing.global_start_sample;
      output.sidecar.diagnostics.frequency_index =
          first.pulse.frequency.frequency_index;
      output.sidecar.diagnostics.confidence = first.confidence;
      output.sidecar.diagnostics.viterbi_path_metric =
          first.viterbi_path_metric;
      output.sidecar.diagnostics.decoded_value = first.decoded_value;
    }
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
