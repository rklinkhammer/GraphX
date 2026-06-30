#pragma once

#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "graph/TypedFixedFanNode.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

using FHSSPulseMergeInputList =
    graph::PrependTypeList_t<FHSSDetectedPulseToken,
                             graph::RepeatType_t<
                                 FHSSPerChannelPulseEvidenceToken,
                                 FHSSProtocolConstants::kFrequencyCount>>;

using FHSSPulseMergeOutputList =
    graph::TypeList<FHSSPulseCandidateToken, FHSSPulseCandidateToken>;

class FHSSPulseMergeNode
    : public graph::TypedFixedFanNode<FHSSPulseMergeNode,
                                           FHSSPulseMergeInputList,
                                           FHSSPulseMergeOutputList> {
public:
  using Base = graph::TypedFixedFanNode<FHSSPulseMergeNode,
                                             FHSSPulseMergeInputList,
                                             FHSSPulseMergeOutputList>;
  using InputTokenType = FHSSDetectedPulseToken;
  using PerChannelInputTokenType = FHSSPerChannelPulseEvidenceToken;
  using OutputTokenType = FHSSPulseCandidateToken;

  static constexpr std::size_t kPerChannelInputCount =
      FHSSProtocolConstants::kFrequencyCount;
  static constexpr std::size_t NInputs = Base::NInputs;
  static constexpr std::size_t NOutputs = Base::NOutputs;

  template <std::size_t PortID>
  using InputType = typename Base::template InputType<PortID>;
  template <std::size_t PortID>
  using InputPortType = typename Base::template InputPortType<PortID>;
  template <std::size_t PortID>
  using OutputType = typename Base::template OutputType<PortID>;
  template <std::size_t PortID>
  using OutputPortType = typename Base::template OutputPortType<PortID>;

  FHSSPulseMergeNode() { SetInputRequired(0, false); }
  explicit FHSSPulseMergeNode(FHSSPulseMergeConfig config)
      : config_(std::move(config)) {
    SetInputRequired(0, false);
  }

  void SetConfig(FHSSPulseMergeConfig config) { config_ = std::move(config); }

  [[nodiscard]] std::string GetNodeTypeName() const {
    return "FHSSPulseMergeNode";
  }

  [[nodiscard]] std::size_t LastMergedPulseCount() const noexcept {
    return last_merged_pulse_count_;
  }

  std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
    std::vector<graph::PortMetadata> out;
    out.reserve(NInputs);
    out.push_back(graph::PortMetadata{
        .port_index = 0,
        .payload_type = std::string(::TypeName<InputTokenType>()),
        .direction = "input",
        .port_name = "DetectedPulseInput"});
    const auto per_channel_type =
        std::string(::TypeName<PerChannelInputTokenType>());
    for (std::size_t port = 1; port < NInputs; ++port) {
      out.push_back(graph::PortMetadata{
          .port_index = port,
          .payload_type = per_channel_type,
          .direction = "input",
          .port_name = "PerChannelInput" + std::to_string(port - 1)});
    }
    return out;
  }

  std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
    const auto payload_type = std::string(::TypeName<OutputTokenType>());
    return {
        graph::PortMetadata{.port_index = 0,
                            .payload_type = payload_type,
                            .direction = "output",
                            .port_name = "DetectedPulseCandidates"},
        graph::PortMetadata{.port_index = 1,
                            .payload_type = payload_type,
                            .direction = "output",
                            .port_name = "PerChannelCandidates"},
    };
  }

  template <std::size_t Port>
  bool ConsumeInput(const typename Base::template InputType<Port> &input) {
    if constexpr (Port == 0) {
      auto output = Transfer(input, std::integral_constant<std::size_t, 0>{},
                             std::integral_constant<std::size_t, 0>{});
      if (!output) {
        return false;
      }
      return EnqueueOutput<0>(*output);
    } else {
      static_assert(Port <= kPerChannelInputCount);
      std::lock_guard<std::mutex> lock(per_channel_mutex_);
      const bool has_payload = !input.sidecar.detected_pulses.empty() ||
                               !input.sidecar.pulse_evidence.empty();
      if (per_channel_output_emitted_) {
        return !has_payload && ObserveInputControl<Port>(input.edge_control);
      }
      if (input.sidecar.detected_pulses.size() !=
          input.sidecar.pulse_evidence.size()) {
        return false;
      }
      if (batch_token_id_ && *batch_token_id_ != input.token_id) {
        ObserveInputControl<Port>(graph::EdgeFailure{
            "FHSS merge inputs must share one deterministic token_id"});
        pending_per_channel_detections_.clear();
        return false;
      }
      if (!batch_token_id_) {
        batch_token_id_ = input.token_id;
      }
      for (std::size_t i = 0; i < input.sidecar.detected_pulses.size(); ++i) {
        pending_per_channel_detections_.push_back(
            FHSSLocalPulseDetectionFromGraphX(
                input.sidecar.detected_pulses[i],
                input.sidecar.pulse_evidence[i]));
      }
      if (!ObserveInputControl<Port>(input.edge_control)) {
        pending_per_channel_detections_.clear();
        return false;
      }

      const auto status = InputCompletionStatus();
      if (status.outcome == graph::RequiredInputOutcome::Failed ||
          status.outcome == graph::RequiredInputOutcome::Cancelled) {
        OutputTokenType terminal{};
        terminal.token_id = *batch_token_id_;
        terminal.edge_control = input.edge_control;
        pending_per_channel_detections_.clear();
        per_channel_output_emitted_ = true;
        return EnqueueOutput<1>(terminal);
      }
      if (!status.IsComplete()) {
        return true;
      }

      auto output = BuildOutput(*batch_token_id_,
                                pending_per_channel_detections_);
      if (!output) {
        return false;
      }
      output->edge_control = graph::EdgeEndOfStream{};
      pending_per_channel_detections_.clear();
      per_channel_output_emitted_ = true;
      return EnqueueOutput<1>(*output);
    }
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) {
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

    return BuildOutput(input.token_id, local_detections);
  }

  template <std::size_t InputPort, std::size_t OutputPort>
  std::optional<OutputTokenType>
  TransferInputToOutput(
      const typename Base::template InputType<InputPort> &input) {
    if constexpr (InputPort == 0 && OutputPort == 0) {
      return Transfer(input, std::integral_constant<std::size_t, 0>{},
                      std::integral_constant<std::size_t, 0>{});
    } else {
      return std::nullopt;
    }
  }

private:
  std::optional<OutputTokenType>
  BuildOutput(std::uint64_t token_id,
              const std::vector<FHSSLocalPulseDetection> &detections) {
    auto merged = FHSSPulseMergeKernel::Merge(detections, config_);
    OutputTokenType output{};
    output.token_id = token_id;
    output.sidecar.ordered_candidates.reserve(merged.ordered_candidates.size());
    for (const auto &candidate : merged.ordered_candidates) {
      output.sidecar.ordered_candidates.push_back(
          FHSSGraphXPulseCandidateFromMergeCandidate(candidate));
    }
    output.sidecar.globally_ordered = true;
    output.sidecar.unsupported_overlap_rejected = true;
    last_merged_pulse_count_ = output.sidecar.ordered_candidates.size();
    return output;
  }

  FHSSPulseMergeConfig config_{};
  std::mutex per_channel_mutex_;
  std::vector<FHSSLocalPulseDetection> pending_per_channel_detections_;
  std::optional<std::uint64_t> batch_token_id_;
  bool per_channel_output_emitted_ = false;
  std::size_t last_merged_pulse_count_{0};
};

} // namespace dsp::fhss
