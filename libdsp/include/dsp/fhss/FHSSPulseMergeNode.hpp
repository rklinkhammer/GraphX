#pragma once

#include "config/ConfigError.hpp"
#include "core/ActiveQueue.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

template <typename First, typename TypeList> struct FHSSPrependTypeList;

template <typename First, typename... Rest>
struct FHSSPrependTypeList<First, graph::TypeList<Rest...>> {
  using type = graph::TypeList<First, Rest...>;
};

template <typename TokenT, typename Sequence>
struct FHSSPulseMergeRepeatedTokenTypeList;

template <typename TokenT, std::size_t... Indices>
struct FHSSPulseMergeRepeatedTokenTypeList<TokenT,
                                           std::index_sequence<Indices...>> {
  template <std::size_t> using TokenForIndex = TokenT;
  using type = graph::TypeList<TokenForIndex<Indices>...>;
};

using FHSSPulseMergePerChannelInputList =
    typename FHSSPulseMergeRepeatedTokenTypeList<
        FHSSPerChannelPulseEvidenceToken,
        std::make_index_sequence<
            FHSSProtocolConstants::kFrequencyCount>>::type;

using FHSSPulseMergeInputList =
    typename FHSSPrependTypeList<FHSSDetectedPulseToken,
                                 FHSSPulseMergePerChannelInputList>::type;

template <typename TypeList> struct FHSSPulseMergeSinkBase;

template <typename... Inputs>
struct FHSSPulseMergeSinkBase<graph::TypeList<Inputs...>> {
  using type = graph::SinkNode<Inputs...>;
};

class FHSSPulseMergeNode
    : public FHSSPulseMergeSinkBase<FHSSPulseMergeInputList>::type,
      public graph::SourceNode<FHSSPulseCandidateToken, FHSSPulseCandidateToken>,
      public graph::NamedType<FHSSPulseMergeNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using SinkBase =
      typename FHSSPulseMergeSinkBase<FHSSPulseMergeInputList>::type;
  using SourceBase =
      graph::SourceNode<FHSSPulseCandidateToken, FHSSPulseCandidateToken>;
  using InputTokenType = FHSSDetectedPulseToken;
  using PerChannelInputTokenType = FHSSPerChannelPulseEvidenceToken;
  using OutputTokenType = FHSSPulseCandidateToken;

  static constexpr std::size_t kPerChannelInputCount =
      FHSSProtocolConstants::kFrequencyCount;
  static constexpr std::size_t NInputs = 1 + kPerChannelInputCount;
  static constexpr std::size_t NOutputs = 2;

  template <std::size_t PortID>
  using InputType = typename SinkBase::template InputType<PortID>;
  template <std::size_t PortID>
  using InputPortType = typename SinkBase::template InputPortType<PortID>;
  template <std::size_t PortID>
  using OutputType = typename SourceBase::template OutputType<PortID>;
  template <std::size_t PortID>
  using OutputPortType = typename SourceBase::template OutputPortType<PortID>;

  FHSSPulseMergeNode() = default;
  explicit FHSSPulseMergeNode(FHSSPulseMergeConfig config)
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
    return "FHSSPulseMergeNode";
  }

  [[nodiscard]] int GetInputPortCount() const {
    return static_cast<int>(NInputs);
  }

  [[nodiscard]] int GetOutputPortCount() const {
    return static_cast<int>(NOutputs);
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

  bool Consume(const InputTokenType &input,
               std::integral_constant<std::size_t, 0>) override {
    auto output = Transfer(input, std::integral_constant<std::size_t, 0>{},
                           std::integral_constant<std::size_t, 0>{});
    if (!output) {
      return false;
    }
    return output_queues_[0].Enqueue(*output);
  }

  template <std::size_t Port>
  bool ConsumePerChannel(const PerChannelInputTokenType &input) {
    static_assert(Port >= 1 && Port <= kPerChannelInputCount);
    auto output = AccumulatePerChannel(input);
    if (!output) {
      return true;
    }
    return output_queues_[1].Enqueue(*output);
  }

#define FHSS_PULSE_MERGE_CONSUME(PORT)                                        \
  bool Consume(const PerChannelInputTokenType &input,                         \
               std::integral_constant<std::size_t, PORT>) override {          \
    return ConsumePerChannel<PORT>(input);                                    \
  }

  FHSS_PULSE_MERGE_CONSUME(1)
  FHSS_PULSE_MERGE_CONSUME(2)
  FHSS_PULSE_MERGE_CONSUME(3)
  FHSS_PULSE_MERGE_CONSUME(4)
  FHSS_PULSE_MERGE_CONSUME(5)
  FHSS_PULSE_MERGE_CONSUME(6)
  FHSS_PULSE_MERGE_CONSUME(7)
  FHSS_PULSE_MERGE_CONSUME(8)
  FHSS_PULSE_MERGE_CONSUME(9)
  FHSS_PULSE_MERGE_CONSUME(10)
  FHSS_PULSE_MERGE_CONSUME(11)
  FHSS_PULSE_MERGE_CONSUME(12)
  FHSS_PULSE_MERGE_CONSUME(13)
  FHSS_PULSE_MERGE_CONSUME(14)
  FHSS_PULSE_MERGE_CONSUME(15)
  FHSS_PULSE_MERGE_CONSUME(16)
  FHSS_PULSE_MERGE_CONSUME(17)
  FHSS_PULSE_MERGE_CONSUME(18)
  FHSS_PULSE_MERGE_CONSUME(19)
  FHSS_PULSE_MERGE_CONSUME(20)
  FHSS_PULSE_MERGE_CONSUME(21)
  FHSS_PULSE_MERGE_CONSUME(22)
  FHSS_PULSE_MERGE_CONSUME(23)
  FHSS_PULSE_MERGE_CONSUME(24)
  FHSS_PULSE_MERGE_CONSUME(25)
  FHSS_PULSE_MERGE_CONSUME(26)
  FHSS_PULSE_MERGE_CONSUME(27)
  FHSS_PULSE_MERGE_CONSUME(28)
  FHSS_PULSE_MERGE_CONSUME(29)
  FHSS_PULSE_MERGE_CONSUME(30)
  FHSS_PULSE_MERGE_CONSUME(31)
  FHSS_PULSE_MERGE_CONSUME(32)
  FHSS_PULSE_MERGE_CONSUME(33)
  FHSS_PULSE_MERGE_CONSUME(34)
  FHSS_PULSE_MERGE_CONSUME(35)
  FHSS_PULSE_MERGE_CONSUME(36)
  FHSS_PULSE_MERGE_CONSUME(37)
  FHSS_PULSE_MERGE_CONSUME(38)
  FHSS_PULSE_MERGE_CONSUME(39)
  FHSS_PULSE_MERGE_CONSUME(40)
  FHSS_PULSE_MERGE_CONSUME(41)
  FHSS_PULSE_MERGE_CONSUME(42)
  FHSS_PULSE_MERGE_CONSUME(43)
  FHSS_PULSE_MERGE_CONSUME(44)
  FHSS_PULSE_MERGE_CONSUME(45)
  FHSS_PULSE_MERGE_CONSUME(46)
  FHSS_PULSE_MERGE_CONSUME(47)
  FHSS_PULSE_MERGE_CONSUME(48)
  FHSS_PULSE_MERGE_CONSUME(49)
  FHSS_PULSE_MERGE_CONSUME(50)
  FHSS_PULSE_MERGE_CONSUME(51)
  FHSS_PULSE_MERGE_CONSUME(52)
  FHSS_PULSE_MERGE_CONSUME(53)
  FHSS_PULSE_MERGE_CONSUME(54)
  FHSS_PULSE_MERGE_CONSUME(55)
  FHSS_PULSE_MERGE_CONSUME(56)
  FHSS_PULSE_MERGE_CONSUME(57)
  FHSS_PULSE_MERGE_CONSUME(58)
  FHSS_PULSE_MERGE_CONSUME(59)
  FHSS_PULSE_MERGE_CONSUME(60)
  FHSS_PULSE_MERGE_CONSUME(61)
  FHSS_PULSE_MERGE_CONSUME(62)
  FHSS_PULSE_MERGE_CONSUME(63)
  FHSS_PULSE_MERGE_CONSUME(64)

#undef FHSS_PULSE_MERGE_CONSUME

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 0>) override {
    OutputTokenType output{};
    if (output_queues_[0].Dequeue(output)) {
      return output;
    }
    return std::nullopt;
  }

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 1>) override {
    OutputTokenType output{};
    if (output_queues_[1].Dequeue(output)) {
      return output;
    }
    return std::nullopt;
  }

  bool Init() override { return SinkBase::Init() && SourceBase::Init(); }

  bool Start() override { return SinkBase::Start() && SourceBase::Start(); }

  void Stop() override {
    SinkBase::Stop();
    SourceBase::Stop();
    for (auto &queue : output_queues_) {
      queue.Disable();
    }
  }

  void Join() override {
    SinkBase::Join();
    SourceBase::Join();
  }

  bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
    return SinkBase::JoinWithTimeout(timeout_ms) &&
           SourceBase::JoinWithTimeout(timeout_ms);
  }

  graph::LifecycleState GetLifecycleState() const override {
    return SinkBase::GetLifecycleState();
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

  std::optional<OutputTokenType>
  Transfer(const PerChannelInputTokenType &input,
           std::integral_constant<std::size_t, 1>,
           std::integral_constant<std::size_t, 1>) {
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
  core::ActiveQueue<OutputTokenType> output_queues_[NOutputs];
};

} // namespace dsp::fhss
