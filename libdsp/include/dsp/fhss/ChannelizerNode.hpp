#pragma once

#include <utility>

#include "config/ConfigError.hpp"
#include "core/ActiveQueue.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/FixedFanInOutNode.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

template <typename TokenT, typename Sequence> struct FHSSRepeatedTokenTypeList;

template <typename TokenT, std::size_t... Indices>
struct FHSSRepeatedTokenTypeList<TokenT, std::index_sequence<Indices...>> {
  template <std::size_t> using TokenForIndex = TokenT;
  using type = graph::TypeList<TokenForIndex<Indices>...>;
};

using FHSSChannelizerOutputList =
    typename FHSSRepeatedTokenTypeList<
        FHSSChannelizedIqToken,
        std::make_index_sequence<
            FHSSProtocolConstants::kFrequencyCount>>::type;

struct FHSSChannelizerConfig {
  FHSSFrequencyConfig frequency{};
  std::vector<std::uint32_t> receiver_frequency_indices;
  std::vector<std::uint32_t> channel_ids;
  std::vector<std::uint32_t> transmitted_active_frequency_indices;
  std::vector<std::uint32_t> transmitted_pulse_frequency_indices;
  double channel_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  std::uint32_t decimation_factor = 1;
  std::int64_t filter_group_delay_input_samples = 0;
};

[[nodiscard]] inline std::vector<std::uint32_t>
FHSSAllFrequencyIndices() {
  std::vector<std::uint32_t> indices;
  indices.reserve(FHSSProtocolConstants::kFrequencyCount);
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    indices.push_back(index);
  }
  return indices;
}

[[nodiscard]] inline FHSSVoidResult
ValidateFHSSChannelizerConfig(const FHSSChannelizerConfig &config) {
  if (auto validation = ValidateFrequencyConfig(config.frequency);
      !validation) {
    return validation;
  }
  if (config.decimation_factor == 0 || config.channel_sample_rate_hz <= 0.0) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "channelizer decimation and channel sample rate must be positive"));
  }
  const auto receiver_indices = config.receiver_frequency_indices.empty()
                                    ? FHSSAllFrequencyIndices()
                                    : config.receiver_frequency_indices;
  if (!FHSSGraphXChannelCountMatchesFrequencyTable(receiver_indices.size(),
                                                   config.frequency)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidFrequencyCount,
        "channelizer requires one receiver channel per configured frequency"));
  }

  std::set<std::uint32_t> unique_indices;
  for (const auto index : receiver_indices) {
    if (auto validation = ValidateFrequencyIndex(index); !validation) {
      return validation;
    }
    if (!unique_indices.insert(index).second) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidFrequencyTable,
          "channelizer receiver frequency indices must be distinct"));
    }
  }

  const auto channel_ids = config.channel_ids.empty() ? receiver_indices
                                                     : config.channel_ids;
  if (channel_ids.size() != receiver_indices.size()) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidFrequencyCount,
        "channelizer channel id count must match receiver frequency count"));
  }
  std::set<std::uint32_t> unique_channel_ids;
  for (const auto channel_id : channel_ids) {
    if (!unique_channel_ids.insert(channel_id).second) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidFrequencyTable,
          "channelizer channel ids must be distinct"));
    }
  }

  for (const auto index : config.transmitted_active_frequency_indices) {
    if (auto validation = ValidateSelectableFrequencyIndex(index);
        !validation) {
      return validation;
    }
  }
  for (const auto index : config.transmitted_pulse_frequency_indices) {
    if (auto validation = ValidateSelectableFrequencyIndex(index);
        !validation) {
      return validation;
    }
  }
  return {};
}

[[nodiscard]] inline FHSSChannelizerConfig
FHSSChannelizerConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSChannelizerConfig config{};
  config.frequency = FHSSFrequencyConfigFromJson(json);
  if (json.contains("receiver_frequency_indices")) {
    config.receiver_frequency_indices =
        FHSSJsonUint32Array(json, "receiver_frequency_indices");
  }
  if (json.contains("channel_ids")) {
    config.channel_ids = FHSSJsonUint32Array(json, "channel_ids");
  }
  if (json.contains("transmitted_active_frequency_indices")) {
    config.transmitted_active_frequency_indices =
        FHSSJsonUint32Array(json, "transmitted_active_frequency_indices");
  }
  if (json.contains("transmitted_pulse_frequency_indices")) {
    config.transmitted_pulse_frequency_indices =
        FHSSJsonUint32Array(json, "transmitted_pulse_frequency_indices");
  }
  config.decimation_factor = static_cast<std::uint32_t>(
      FHSSJsonUint64(json, "decimation_factor", config.decimation_factor));
  config.channel_sample_rate_hz =
      FHSSJsonDouble(json, "channel_sample_rate_hz",
                     config.frequency.sample_rate_hz /
                         static_cast<double>(config.decimation_factor));
  config.filter_group_delay_input_samples =
      static_cast<std::int64_t>(FHSSJsonUint64(
          json, "filter_group_delay_input_samples", 0));
  if (auto validation = ValidateFHSSChannelizerConfig(config); !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

class ChannelizerNode
    : public graph::NamedFixedFanInOutNode<ChannelizerNode,
                                           graph::TypeList<FHSSDownconvertedIqToken>,
                                           FHSSChannelizerOutputList>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using Base = graph::NamedFixedFanInOutNode<ChannelizerNode,
                                             graph::TypeList<FHSSDownconvertedIqToken>,
                                             FHSSChannelizerOutputList>;
  using InputTokenType = FHSSDownconvertedIqToken;
  using OutputTokenType = FHSSChannelizedIqToken;
  static constexpr std::size_t kOutputPortCount =
      FHSSProtocolConstants::kFrequencyCount;
  static constexpr std::size_t NInputs = Base::NInputs;
  static constexpr std::size_t NOutputs = Base::NOutputs;

  template <std::size_t PortID> using InputType = typename Base::template InputType<PortID>;
  template <std::size_t PortID>
  using OutputType = typename Base::template OutputType<PortID>;

  ChannelizerNode() {
    config_.receiver_frequency_indices = FHSSAllFrequencyIndices();
    config_.channel_ids = config_.receiver_frequency_indices;
  }
  explicit ChannelizerNode(FHSSChannelizerConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSChannelizerConfig config) {
    config_ = std::move(config);
  }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSChannelizerConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params;
    params["frequency_count"] = config_.frequency.frequency_count;
    params["iq_center_frequency_hz"] = 0.0;
    params["iq_offsets"] = nlohmann::json::array();
    params["sample_rate_hz"] = config_.frequency.sample_rate_hz;
    params["occupied_bandwidth_hz"] = config_.frequency.occupied_bandwidth_hz;
    params["max_abs_cfo_hz"] = config_.frequency.max_abs_cfo_hz;
    params["receiver_frequency_indices"] = config_.receiver_frequency_indices;
    params["channel_ids"] = config_.channel_ids;
    params["transmitted_active_frequency_indices"] =
        config_.transmitted_active_frequency_indices;
    params["transmitted_pulse_frequency_indices"] =
        config_.transmitted_pulse_frequency_indices;
    params["channel_sample_rate_hz"] = config_.channel_sample_rate_hz;
    params["decimation_factor"] = config_.decimation_factor;
    params["filter_group_delay_input_samples"] =
        config_.filter_group_delay_input_samples;
    return FHSSStableParameterJsonView(std::move(params));
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"frequency_count",
            "iq_center_frequency_hz",
            "iq_offsets",
            "sample_rate_hz",
            "occupied_bandwidth_hz",
            "max_abs_cfo_hz",
            "receiver_frequency_indices",
            "channel_ids",
            "transmitted_active_frequency_indices",
            "transmitted_pulse_frequency_indices",
            "channel_sample_rate_hz",
            "decimation_factor",
            "filter_group_delay_input_samples"};
  }

  template <std::size_t Port>
  bool ConsumeInput(const typename Base::template InputType<Port> &input) {
    static_assert(Port == 0);
    if (auto validation = ValidateFHSSChannelizerConfig(config_);
        !validation) {
      return false;
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return false;
    }

    auto map_result = BuildFrequencyMap(config_.frequency);
    if (!map_result) {
      return false;
    }

    const auto receiver_indices = ReceiverIndices();
    const auto channel_ids = ChannelIds(receiver_indices);
    return EnqueueAllOutputs<0>(input, *map_result, receiver_indices,
                                channel_ids);
  }

  bool Init() override { return Base::Init(); }

  bool Start() override { return Base::Start(); }

  void Stop() override { Base::Stop(); }

  void Join() override { Base::Join(); }

  bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override {
    return Base::JoinWithTimeout(timeout_ms);
  }

  graph::LifecycleState GetLifecycleState() const override {
    return Base::GetLifecycleState();
  }

  int GetInputPortCount() const { return 1; }
  int GetOutputPortCount() const {
    return static_cast<int>(kOutputPortCount);
  }

  std::string GetNodeTypeName() const { return "ChannelizerNode"; }

  std::vector<graph::PortMetadata> GetInputPortMetadata() const override {
    return {graph::PortMetadata{
        .port_index = 0,
        .payload_type = std::string(::TypeName<InputTokenType>()),
        .direction = "input",
        .port_name = "Input0"}};
  }

  std::vector<graph::PortMetadata> GetOutputPortMetadata() const override {
    std::vector<graph::PortMetadata> out;
    out.reserve(kOutputPortCount);
    const auto payload_type = std::string(::TypeName<OutputTokenType>());
    for (std::size_t port = 0; port < kOutputPortCount; ++port) {
      out.push_back(graph::PortMetadata{
          .port_index = port,
          .payload_type = payload_type,
          .direction = "output",
          .port_name = "Channel" + std::to_string(port)});
    }
    return out;
  }

  template <std::size_t OutputPort>
  std::optional<OutputTokenType> ProduceOutput() {
    static_assert(OutputPort < FHSSProtocolConstants::kFrequencyCount);
    return Base::template DequeueOutput<OutputPort>();
  }

private:
  template <std::size_t Port>
  bool EnqueueAllOutputs(const InputTokenType &input,
                         const std::array<FHSSFrequencyMapEntry,
                                          FHSSProtocolConstants::kFrequencyCount>
                             &freq_map,
                         const std::vector<std::uint32_t> &receiver_indices,
                         const std::vector<std::uint32_t> &channel_ids) {
    if constexpr (Port < kOutputPortCount) {
      if (receiver_indices[Port] != Port || channel_ids[Port] != Port) {
        return false;
      }
      OutputTokenType output;
      output.token_id = input.token_id;
      output.sidecar = BuildChannelPacket(input.sidecar, freq_map[Port], Port);

      if (!Base::template EnqueueOutput<Port>(output)) {
        return false;
      }

      return EnqueueAllOutputs<Port + 1>(input, freq_map, receiver_indices,
                                         channel_ids);
    }
    return true;
  }
  [[nodiscard]] std::vector<std::uint32_t> ReceiverIndices() const {
    return config_.receiver_frequency_indices.empty()
               ? FHSSAllFrequencyIndices()
               : config_.receiver_frequency_indices;
  }

  [[nodiscard]] std::vector<std::uint32_t>
  ChannelIds(const std::vector<std::uint32_t> &receiver_indices) const {
    return config_.channel_ids.empty() ? receiver_indices : config_.channel_ids;
  }

  [[nodiscard]] FHSSChannelizedIqPacket
  BuildChannelPacket(const FHSSDownconvertedIqPacket &input,
                     const FHSSFrequencyMapEntry &entry,
                     std::uint32_t channel_id) const {
    FHSSChannelizedIqPacket packet{};
    packet.channel.channel_id = channel_id;
    packet.channel.frequency_index = entry.index;
    packet.channel.rf_frequency_hz = entry.rf_frequency_hz;
    packet.channel.iq_offset_frequency_hz = entry.iq_offset_frequency_hz;
    packet.channel.downconverter_passthrough =
        input.downconverter.passthrough;
    packet.channel.downconverter_translation_frequency_hz =
        input.downconverter.translation_frequency_hz;
    packet.channel.channel_sample_rate_hz = config_.channel_sample_rate_hz;
    packet.channel.decimation_factor = config_.decimation_factor;
    packet.channel.filter_group_delay_input_samples =
        config_.filter_group_delay_input_samples;
    packet.channel.input_global_start_sample =
        input.iq.sample_time_map.input_packet_global_start_sample;
    packet.channel.channel_global_start_sample =
        input.iq.sample_time_map.input_packet_global_start_sample +
        static_cast<std::uint64_t>(
            std::max<std::int64_t>(0,
                                   config_.filter_group_delay_input_samples));
    packet.channel.sample_time_map = input.iq.sample_time_map;
    packet.channel.sample_time_map.decimation_factor =
        config_.decimation_factor;
    packet.channel.sample_time_map.group_delay_input_samples =
        config_.filter_group_delay_input_samples;
    packet.channel.sample_time_map.input_sample_rate_hz =
        config_.frequency.sample_rate_hz;
    packet.channel.sample_time_map.output_sample_rate_hz =
        config_.channel_sample_rate_hz;
    packet.receiver_guard_or_metadata_channel =
        IsReservedFrequencyIndex(entry.index);
    packet.truth_metadata_required_for_decision = false;
    packet.iq = MixAndDecimate(input.iq, entry.iq_offset_frequency_hz,
                               packet.channel.sample_time_map);
    return packet;
  }

  [[nodiscard]] FHSSGraphXComplexEvidence
  MixAndDecimate(const FHSSGraphXComplexEvidence &input,
                 double channel_offset_hz,
                 const FHSSGraphXSampleTimeMap &time_map) const {
    const auto &source = *input.host_complex64_samples;
    auto samples = std::make_shared<std::vector<std::complex<double>>>();
    const auto count = input.sample_count;
    const auto offset = input.sample_offset;
    const std::uint64_t usable =
        (offset <= source.size()) ? std::min<std::uint64_t>(
                                        count, source.size() - offset)
                                  : 0;
    samples->reserve(static_cast<std::size_t>(
        (usable + config_.decimation_factor - 1) / config_.decimation_factor));
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double radians_per_sample =
        -kTwoPi * channel_offset_hz / config_.frequency.sample_rate_hz;
    const auto global_start = input.sample_time_map.input_packet_global_start_sample;
    for (std::uint64_t i = 0; i < usable; i += config_.decimation_factor) {
      const double phase =
          radians_per_sample * static_cast<double>(global_start + i);
      samples->push_back(source[offset + i] *
                         std::complex<double>(std::cos(phase),
                                              std::sin(phase)));
    }
    auto evidence = FHSSGraphXComplexEvidenceFromHostSamples(
        samples, samples->size(), time_map);
    evidence.sample_time_map.output_start_sample =
        time_map.output_start_sample;
    return evidence;
  }

  FHSSChannelizerConfig config_{};
};

} // namespace dsp::fhss
