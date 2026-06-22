// SPDX-License-Identifier: MIT

#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "graph/FixedFanInOutNode.hpp"
#include "graph/IConfigurable.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

using FHSSPulseMergeInteriorInputList =
    graph::PrependTypeList_t<FHSSDetectedPulseToken,
                             graph::RepeatType_t<
                                 FHSSPerChannelPulseEvidenceToken,
                                 FHSSProtocolConstants::kFrequencyCount>>;

using FHSSPulseMergeInteriorOutputList =
    graph::TypeList<FHSSPulseCandidateToken, FHSSPulseCandidateToken>;

class FHSSPulseMergeInteriorNode
    : public graph::NamedFixedFanInOutNode<
          FHSSPulseMergeInteriorNode, FHSSPulseMergeInteriorInputList,
          FHSSPulseMergeInteriorOutputList>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using Base = graph::NamedFixedFanInOutNode<
      FHSSPulseMergeInteriorNode, FHSSPulseMergeInteriorInputList,
      FHSSPulseMergeInteriorOutputList>;
  using InputTokenType = FHSSDetectedPulseToken;
  using PerChannelInputTokenType = FHSSPerChannelPulseEvidenceToken;
  using OutputTokenType = FHSSPulseCandidateToken;

  static constexpr std::size_t kPerChannelInputCount =
      FHSSProtocolConstants::kFrequencyCount;

  FHSSPulseMergeInteriorNode() = default;
  explicit FHSSPulseMergeInteriorNode(FHSSPulseMergeConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseMergeConfig config) { config_ = std::move(config); }

  void Configure(const graph::JsonView &cfg) override {
    const auto &json = cfg.Raw();
    config_.expected_per_channel_packet_count = static_cast<std::uint32_t>(
        FHSSJsonUint64(json, "expected_per_channel_packet_count",
                       config_.expected_per_channel_packet_count));
    if (config_.expected_per_channel_packet_count == 0 ||
        config_.expected_per_channel_packet_count > kPerChannelInputCount) {
      throw graph::ConfigError(
          "expected_per_channel_packet_count must be in [1, 64]");
    }
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params;
    params["expected_per_channel_packet_count"] =
        config_.expected_per_channel_packet_count;
    return FHSSStableParameterJsonView(std::move(params));
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"expected_per_channel_packet_count"};
  }

  [[nodiscard]] std::string GetNodeTypeName() const {
    return "FHSSPulseMergeInteriorNode";
  }

  std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
    std::vector<graph::PortMetadata> out;
    out.reserve(Base::NInputs);
    out.push_back(graph::PortMetadata{
        .port_index = 0,
        .payload_type = std::string(::TypeName<InputTokenType>()),
        .direction = "input",
        .port_name = "DetectedPulseInput"});
    const auto per_channel_type =
        std::string(::TypeName<PerChannelInputTokenType>());
    for (std::size_t port = 1; port < Base::NInputs; ++port) {
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
      auto output = AccumulatePerChannel(input);
      if (!output) {
        return true;
      }
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

  template <std::size_t Port>
  std::optional<OutputTokenType>
  Transfer(const PerChannelInputTokenType &input,
           std::integral_constant<std::size_t, Port>,
           std::integral_constant<std::size_t, 1>) {
    static_assert(Port >= 1 && Port <= kPerChannelInputCount);
    const auto saved_count = config_.expected_per_channel_packet_count;
    config_.expected_per_channel_packet_count = 1;
    auto output = AccumulatePerChannel(input);
    config_.expected_per_channel_packet_count = saved_count;
    return output;
  }

private:
  std::optional<OutputTokenType>
  AccumulatePerChannel(const PerChannelInputTokenType &input) {
    std::lock_guard<std::mutex> lock(per_channel_mutex_);
    if (input.sidecar.detected_pulses.size() !=
        input.sidecar.pulse_evidence.size()) {
      return std::nullopt;
    }

    pending_per_channel_detections_.reserve(
        pending_per_channel_detections_.size() +
        input.sidecar.detected_pulses.size());
    for (std::size_t i = 0; i < input.sidecar.detected_pulses.size(); ++i) {
      pending_per_channel_detections_.push_back(FHSSLocalPulseDetectionFromGraphX(
          input.sidecar.detected_pulses[i], input.sidecar.pulse_evidence[i]));
    }
    ++pending_per_channel_packet_count_;
    if (pending_per_channel_packet_count_ <
        config_.expected_per_channel_packet_count) {
      return std::nullopt;
    }

    auto output = BuildOutput(input.token_id, pending_per_channel_detections_);
    pending_per_channel_detections_.clear();
    pending_per_channel_packet_count_ = 0;
    return output;
  }

  std::optional<OutputTokenType>
  BuildOutput(std::uint64_t token_id,
              const std::vector<FHSSLocalPulseDetection> &detections) const {
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
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

  FHSSPulseMergeConfig config_{};
  std::mutex per_channel_mutex_;
  std::vector<FHSSLocalPulseDetection> pending_per_channel_detections_;
  std::uint32_t pending_per_channel_packet_count_ = 0;
};

} // namespace dsp::fhss
