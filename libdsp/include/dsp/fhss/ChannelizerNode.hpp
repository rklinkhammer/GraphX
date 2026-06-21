#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
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
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDownconvertedIqToken>,
          graph::TypeList<FHSSChannelizedIqStreamToken>,
          ChannelizerNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using InputTokenType = FHSSDownconvertedIqToken;
  using OutputTokenType = FHSSChannelizedIqStreamToken;

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
    params["channel_sample_rate_hz"] = config_.channel_sample_rate_hz;
    params["decimation_factor"] = config_.decimation_factor;
    params["filter_group_delay_input_samples"] =
        config_.filter_group_delay_input_samples;
    return graph::JsonView(params);
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return graph::JsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"channel_sample_rate_hz", "decimation_factor",
            "filter_group_delay_input_samples"};
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (auto validation = ValidateFHSSChannelizerConfig(config_);
        !validation) {
      return std::nullopt;
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return std::nullopt;
    }

    auto map_result = BuildFrequencyMap(config_.frequency);
    if (!map_result) {
      return std::nullopt;
    }

    const auto receiver_indices = ReceiverIndices();
    const auto channel_ids = ChannelIds(receiver_indices);
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.channels.reserve(receiver_indices.size());
    output.sidecar.channel_count_matches_frequency_count =
        FHSSGraphXChannelCountMatchesFrequencyTable(receiver_indices.size(),
                                                    config_.frequency);
    output.sidecar.truth_metadata_required_for_decision = false;

    for (std::size_t i = 0; i < receiver_indices.size(); ++i) {
      const auto frequency_index = receiver_indices[i];
      const auto &entry = (*map_result)[frequency_index];
      output.sidecar.channels.push_back(
          BuildChannelPacket(input.sidecar.iq, entry, channel_ids[i]));
    }
    return output;
  }

private:
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
  BuildChannelPacket(const FHSSGraphXComplexEvidence &input,
                     const FHSSFrequencyMapEntry &entry,
                     std::uint32_t channel_id) const {
    FHSSChannelizedIqPacket packet{};
    packet.channel.channel_id = channel_id;
    packet.channel.frequency_index = entry.index;
    packet.channel.rf_frequency_hz = entry.rf_frequency_hz;
    packet.channel.iq_offset_frequency_hz = entry.iq_offset_frequency_hz;
    packet.channel.channel_sample_rate_hz = config_.channel_sample_rate_hz;
    packet.channel.decimation_factor = config_.decimation_factor;
    packet.channel.filter_group_delay_input_samples =
        config_.filter_group_delay_input_samples;
    packet.channel.input_global_start_sample =
        input.sample_time_map.input_packet_global_start_sample;
    packet.channel.channel_global_start_sample =
        input.sample_time_map.input_packet_global_start_sample +
        static_cast<std::uint64_t>(
            std::max<std::int64_t>(0,
                                   config_.filter_group_delay_input_samples));
    packet.channel.sample_time_map = input.sample_time_map;
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
    packet.iq = MixAndDecimate(input, entry.iq_offset_frequency_hz,
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
