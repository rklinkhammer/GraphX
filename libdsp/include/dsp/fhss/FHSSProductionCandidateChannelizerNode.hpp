/**
 * @file FHSSProductionCandidateChannelizerNode.hpp
 * @brief Deterministic CPU FIR channelizer used for Phase 2 characterization.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSFixtureFrequencyChannelizerNode.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"
#include "graph/TypedFixedFanNode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

struct FHSSProductionChannelizerConfig {
  FHSSFrequencyConfig frequency{};
  std::vector<std::uint32_t> receiver_frequency_indices;
  std::vector<std::uint32_t> channel_ids;
  std::uint32_t decimation_factor = 10;
  std::uint32_t fir_tap_count = 241;
  double passband_edge_hz = 2'500'000.0;
  double cutoff_frequency_hz = 4'000'000.0;
  double guarded_nyquist_margin_hz = 5'000'000.0;
  std::uint64_t max_input_samples = 4'194'304;
};

[[nodiscard]] inline FHSSVoidResult ValidateFHSSProductionChannelizerConfig(
    const FHSSProductionChannelizerConfig &config) {
  if (auto validation = ValidateFrequencyConfig(config.frequency);
      !validation) {
    return validation;
  }
  if (!std::isfinite(config.passband_edge_hz) ||
      !std::isfinite(config.cutoff_frequency_hz) ||
      !std::isfinite(config.guarded_nyquist_margin_hz) ||
      config.frequency.sample_rate_hz <= 0.0 ||
      config.decimation_factor == 0u || config.fir_tap_count < 3u ||
      config.fir_tap_count > 4095u || (config.fir_tap_count % 2u) == 0u ||
      config.max_input_samples == 0u) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidTiming,
                  "production channelizer requires finite positive rates, "
                  "bounded odd FIR length, and bounded input"));
  }
  const double output_nyquist =
      config.frequency.sample_rate_hz /
      (2.0 * static_cast<double>(config.decimation_factor));
  if (config.passband_edge_hz <= 0.0 ||
      config.cutoff_frequency_hz <= config.passband_edge_hz ||
      config.cutoff_frequency_hz >= output_nyquist ||
      config.guarded_nyquist_margin_hz < 0.0 ||
      config.guarded_nyquist_margin_hz >=
          0.5 * config.frequency.sample_rate_hz) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidTiming,
                                     "production channelizer passband/cutoff "
                                     "must fit the decimated Nyquist band"));
  }

  const auto receiver_indices = config.receiver_frequency_indices.empty()
                                    ? FHSSAllFrequencyIndices()
                                    : config.receiver_frequency_indices;
  const auto channel_ids =
      config.channel_ids.empty() ? receiver_indices : config.channel_ids;
  if (receiver_indices.size() != FHSSProtocolConstants::kFrequencyCount ||
      channel_ids.size() != receiver_indices.size()) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidFrequencyCount,
                                     "production channelizer requires exactly "
                                     "64 receiver indices and channel ids"));
  }

  std::set<std::uint32_t> indices;
  std::set<std::uint32_t> ids;
  std::vector<double> offsets;
  offsets.reserve(receiver_indices.size());
  const double guarded_limit = 0.5 * config.frequency.sample_rate_hz -
                               config.guarded_nyquist_margin_hz -
                               0.5 * config.frequency.occupied_bandwidth_hz -
                               config.frequency.max_abs_cfo_hz;
  if (!(guarded_limit > 0.0)) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidIqOffset,
                  "production channelizer has no guarded usable IQ band"));
  }
  for (std::size_t port = 0; port < receiver_indices.size(); ++port) {
    const auto index = receiver_indices[port];
    if (index >= FHSSProtocolConstants::kFrequencyCount ||
        !indices.insert(index).second ||
        !ids.insert(channel_ids[port]).second || index != port ||
        channel_ids[port] != port) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidFrequencyTable,
                    "production channelizer ports require distinct identity "
                    "index/channel mappings"));
    }
    const double offset = config.frequency.iq_offset_frequency_hz[index];
    if (!std::isfinite(offset) || std::abs(offset) > guarded_limit) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidIqOffset,
                    "production channelizer IQ offset is non-finite or outside "
                    "guarded Nyquist"));
    }
    if (std::ranges::any_of(offsets, [offset](double prior) {
          return std::abs(prior - offset) < 1.0;
        })) {
      return std::unexpected(
          MakeError(FHSSValidationCode::DuplicateIqOffset,
                    "production channelizer IQ offsets must be distinct"));
    }
    offsets.push_back(offset);
  }
  return {};
}

[[nodiscard]] inline FHSSProductionChannelizerConfig
FHSSProductionChannelizerConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSProductionChannelizerConfig config{};
  config.frequency = FHSSFrequencyConfigFromJson(json);
  if (json.contains("receiver_frequency_indices")) {
    config.receiver_frequency_indices =
        FHSSJsonUint32Array(json, "receiver_frequency_indices");
  }
  if (json.contains("channel_ids")) {
    config.channel_ids = FHSSJsonUint32Array(json, "channel_ids");
  }
  const auto decimation =
      FHSSJsonUint64(json, "decimation_factor", config.decimation_factor);
  const auto tap_count =
      FHSSJsonUint64(json, "fir_tap_count", config.fir_tap_count);
  if (decimation > std::numeric_limits<std::uint32_t>::max() ||
      tap_count > std::numeric_limits<std::uint32_t>::max()) {
    throw graph::ConfigError(
        "production channelizer integer configuration exceeds uint32 range");
  }
  config.decimation_factor = static_cast<std::uint32_t>(decimation);
  config.fir_tap_count = static_cast<std::uint32_t>(tap_count);
  config.passband_edge_hz =
      FHSSJsonDouble(json, "passband_edge_hz", config.passband_edge_hz);
  config.cutoff_frequency_hz =
      FHSSJsonDouble(json, "cutoff_frequency_hz", config.cutoff_frequency_hz);
  config.guarded_nyquist_margin_hz = FHSSJsonDouble(
      json, "guarded_nyquist_margin_hz", config.guarded_nyquist_margin_hz);
  config.max_input_samples =
      FHSSJsonUint64(json, "max_input_samples", config.max_input_samples);
  if (auto validation = ValidateFHSSProductionChannelizerConfig(config);
      !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

[[nodiscard]] inline std::vector<double>
DesignFHSSHammingLowpass(std::uint32_t tap_count, double cutoff_hz,
                         double sample_rate_hz) {
  std::vector<double> taps(tap_count);
  constexpr double kPi = 3.1415926535897932384626433832795;
  const double normalized = cutoff_hz / sample_rate_hz;
  const auto midpoint = static_cast<std::int64_t>((tap_count - 1u) / 2u);
  double sum = 0.0;
  for (std::uint32_t i = 0; i < tap_count; ++i) {
    const auto m = static_cast<std::int64_t>(i) - midpoint;
    const double ideal =
        m == 0 ? 2.0 * normalized
               : std::sin(2.0 * kPi * normalized * static_cast<double>(m)) /
                     (kPi * static_cast<double>(m));
    const double window =
        0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(i) /
                               static_cast<double>(tap_count - 1u));
    taps[i] = ideal * window;
    sum += taps[i];
  }
  for (auto &tap : taps) {
    tap /= sum;
  }
  return taps;
}

struct FHSSChannelizerKernelOutput {
  std::vector<std::complex<double>> samples;
  std::uint64_t first_causal_input_global_sample = 0;
  bool has_output = false;
};

class FHSSFirChannelizerKernel {
public:
  FHSSFirChannelizerKernel() = default;
  FHSSFirChannelizerKernel(std::vector<double> taps, std::uint32_t decimation,
                           double mix_frequency_hz, double sample_rate_hz)
      : taps_(std::move(taps)), decimation_(decimation),
        mix_frequency_hz_(mix_frequency_hz), sample_rate_hz_(sample_rate_hz) {
    history_.reserve(taps_.size());
  }

  [[nodiscard]] FHSSResult<FHSSChannelizerKernelOutput>
  Process(const std::vector<std::complex<double>> &input,
          std::uint64_t global_start_sample) {
    if (taps_.empty() || decimation_ == 0u ||
        !std::isfinite(mix_frequency_hz_) || !std::isfinite(sample_rate_hz_) ||
        sample_rate_hz_ <= 0.0) {
      return std::unexpected(MakeError(FHSSValidationCode::InvalidTiming,
                                       "invalid FIR channelizer kernel"));
    }
    if (next_global_sample_.has_value() &&
        *next_global_sample_ != global_start_sample) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidGlobalTiming,
          "FIR channelizer input packets must be globally contiguous"));
    }
    if (input.size() >
        std::numeric_limits<std::uint64_t>::max() - global_start_sample) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidGlobalTiming,
                    "FIR channelizer global sample range overflows uint64"));
    }
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    FHSSChannelizerKernelOutput output;
    output.samples.reserve((input.size() + decimation_ - 1u) / decimation_);
    for (std::size_t i = 0; i < input.size(); ++i) {
      const std::uint64_t global = global_start_sample + i;
      const double phase = -kTwoPi * mix_frequency_hz_ *
                           static_cast<double>(global) / sample_rate_hz_;
      const auto mixed =
          input[i] * std::complex<double>(std::cos(phase), std::sin(phase));
      history_.push_back(mixed);
      if (history_.size() > taps_.size()) {
        history_.erase(history_.begin());
      }
      if (history_.size() != taps_.size() || global % decimation_ != 0u) {
        continue;
      }
      std::complex<double> filtered{0.0, 0.0};
      for (std::size_t tap = 0; tap < taps_.size(); ++tap) {
        filtered += taps_[tap] * history_[history_.size() - 1u - tap];
      }
      if (!output.has_output) {
        output.first_causal_input_global_sample = global;
        output.has_output = true;
      }
      output.samples.push_back(filtered);
    }
    next_global_sample_ = global_start_sample + input.size();
    allocation_high_water_bytes_ =
        std::max(allocation_high_water_bytes_,
                 (history_.capacity() + output.samples.capacity()) *
                     sizeof(std::complex<double>));
    return output;
  }

  [[nodiscard]] std::size_t AllocationHighWaterBytes() const noexcept {
    return allocation_high_water_bytes_;
  }

  void Reset() {
    history_.clear();
    next_global_sample_.reset();
  }

private:
  std::vector<double> taps_;
  std::uint32_t decimation_ = 1;
  double mix_frequency_hz_ = 0.0;
  double sample_rate_hz_ = 1.0;
  std::vector<std::complex<double>> history_;
  std::optional<std::uint64_t> next_global_sample_;
  std::size_t allocation_high_water_bytes_ = 0u;
};

class FHSSProductionCandidateChannelizerNode
    : public graph::TypedFixedFanNode<FHSSProductionCandidateChannelizerNode,
                                      graph::TypeList<FHSSDownconvertedIqToken>,
                                      FHSSChannelizerOutputList>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using Base =
      graph::TypedFixedFanNode<FHSSProductionCandidateChannelizerNode,
                               graph::TypeList<FHSSDownconvertedIqToken>,
                               FHSSChannelizerOutputList>;
  using InputTokenType = FHSSDownconvertedIqToken;
  using OutputTokenType = FHSSChannelizedIqToken;
  static constexpr std::size_t kOutputPortCount =
      FHSSProtocolConstants::kFrequencyCount;

  FHSSProductionCandidateChannelizerNode() = default;
  explicit FHSSProductionCandidateChannelizerNode(
      FHSSProductionChannelizerConfig config) {
    SetConfig(std::move(config));
  }

  void SetConfig(FHSSProductionChannelizerConfig config) {
    config_ = std::move(config);
    initialized_ = false;
  }
  void Configure(const graph::JsonView &cfg) override {
    SetConfig(FHSSProductionChannelizerConfigFromJson(cfg));
  }
  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json iq_offsets = nlohmann::json::array();
    for (std::uint32_t index = 0;
         index < FHSSProtocolConstants::kFrequencyCount; ++index) {
      iq_offsets.push_back({{"index", index},
                            {"iq_offset_frequency_hz",
                             config_.frequency.iq_offset_frequency_hz[index]}});
    }
    nlohmann::json params{
        {"frequency_count", config_.frequency.frequency_count},
        {"sample_rate_hz", config_.frequency.sample_rate_hz},
        {"occupied_bandwidth_hz", config_.frequency.occupied_bandwidth_hz},
        {"max_abs_cfo_hz", config_.frequency.max_abs_cfo_hz},
        {"iq_offsets", std::move(iq_offsets)},
        {"receiver_frequency_indices", config_.receiver_frequency_indices},
        {"channel_ids", config_.channel_ids},
        {"decimation_factor", config_.decimation_factor},
        {"fir_tap_count", config_.fir_tap_count},
        {"passband_edge_hz", config_.passband_edge_hz},
        {"cutoff_frequency_hz", config_.cutoff_frequency_hz},
        {"guarded_nyquist_margin_hz", config_.guarded_nyquist_margin_hz},
        {"max_input_samples", config_.max_input_samples}};
    return FHSSStableParameterJsonView(std::move(params));
  }
  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &name) const override {
    static const nlohmann::json descriptions{
        {"frequency_count", "Number of typed channel output ports."},
        {"sample_rate_hz", "Complex input sample rate in hertz."},
        {"occupied_bandwidth_hz", "Declared wanted occupied bandwidth."},
        {"max_abs_cfo_hz", "Guard allowance for absolute CFO in hertz."},
        {"iq_offsets", "Finite distinct IQ centers indexed by output port."},
        {"receiver_frequency_indices", "Frequency index mapped to each port."},
        {"channel_ids", "Receiver channel identifier mapped to each port."},
        {"decimation_factor", "Integer FIR output decimation ratio."},
        {"fir_tap_count", "Odd bounded Hamming FIR tap count."},
        {"passband_edge_hz", "Characterized passband edge in hertz."},
        {"cutoff_frequency_hz", "Windowed-sinc FIR cutoff in hertz."},
        {"guarded_nyquist_margin_hz", "Input-Nyquist guard in hertz."},
        {"max_input_samples", "Maximum accepted complex samples per token."}};
    if (!descriptions.contains(name)) {
      return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
    }
    return FHSSStableParameterDescriptionJsonView(
        {{"name", name}, {"description", descriptions.at(name)}});
  }
  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"frequency_count",     "iq_offsets",
            "sample_rate_hz",      "occupied_bandwidth_hz",
            "max_abs_cfo_hz",      "receiver_frequency_indices",
            "channel_ids",         "decimation_factor",
            "fir_tap_count",       "passband_edge_hz",
            "cutoff_frequency_hz", "guarded_nyquist_margin_hz",
            "max_input_samples"};
  }
  [[nodiscard]] std::string GetNodeTypeName() const {
    return "FHSSProductionCandidateChannelizerNode";
  }
  [[nodiscard]] std::size_t AllocationHighWaterBytes() const noexcept {
    return allocation_high_water_bytes_;
  }
  [[nodiscard]] std::vector<graph::PortMetadata>
  GetInputPortMetadata() const override {
    return {{.port_index = 0,
             .payload_type = std::string(::TypeName<InputTokenType>()),
             .direction = "input",
             .port_name = "Input0"}};
  }
  [[nodiscard]] std::vector<graph::PortMetadata>
  GetOutputPortMetadata() const override {
    std::vector<graph::PortMetadata> result;
    result.reserve(kOutputPortCount);
    for (std::size_t port = 0; port < kOutputPortCount; ++port) {
      result.push_back(
          {.port_index = port,
           .payload_type = std::string(::TypeName<OutputTokenType>()),
           .direction = "output",
           .port_name = "Channel" + std::to_string(port)});
    }
    return result;
  }

  template <std::size_t Port>
  bool ConsumeInput(const typename Base::template InputType<Port> &input) {
    static_assert(Port == 0);
    if (auto validation = ValidateFHSSProductionChannelizerConfig(config_);
        !validation) {
      return false;
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq) ||
        input.sidecar.iq.sample_count > config_.max_input_samples) {
      return false;
    }
    const auto &source = *input.sidecar.iq.host_complex64_samples;
    if (input.sidecar.iq.sample_offset > source.size() ||
        input.sidecar.iq.sample_count >
            source.size() - input.sidecar.iq.sample_offset) {
      return false;
    }
    if (!initialized_) {
      InitializeKernels();
    }
    const std::vector<std::complex<double>> selected(
        source.begin() +
            static_cast<std::ptrdiff_t>(input.sidecar.iq.sample_offset),
        source.begin() +
            static_cast<std::ptrdiff_t>(input.sidecar.iq.sample_offset +
                                        input.sidecar.iq.sample_count));
    allocation_cycle_bytes_ =
        selected.capacity() * sizeof(std::complex<double>);
    return EnqueueAllOutputs<0>(input, selected);
  }

  template <std::size_t Port> std::optional<OutputTokenType> ProduceOutput() {
    static_assert(Port < kOutputPortCount);
    return Base::template DequeueOutput<Port>();
  }

private:
  void InitializeKernels() {
    const auto taps = DesignFHSSHammingLowpass(
        config_.fir_tap_count, config_.cutoff_frequency_hz,
        config_.frequency.sample_rate_hz);
    for (std::size_t port = 0; port < kernels_.size(); ++port) {
      kernels_[port] = FHSSFirChannelizerKernel{
          taps, config_.decimation_factor,
          config_.frequency.iq_offset_frequency_hz[port],
          config_.frequency.sample_rate_hz};
    }
    initialized_ = true;
  }

  template <std::size_t Port>
  bool EnqueueAllOutputs(const InputTokenType &input,
                         const std::vector<std::complex<double>> &selected) {
    if constexpr (Port < kOutputPortCount) {
      const auto global_start =
          input.sidecar.iq.sample_time_map.input_packet_global_start_sample;
      auto kernel_output = kernels_[Port].Process(selected, global_start);
      if (!kernel_output) {
        return false;
      }
      OutputTokenType output{};
      output.token_id = input.token_id;
      output.edge_control = input.edge_control;
      output.sidecar.correlation = input.sidecar.correlation;
      const auto map = BuildFrequencyMap(config_.frequency);
      if (!map) {
        return false;
      }
      const auto &entry = (*map)[Port];
      auto &channel = output.sidecar.channel;
      channel.channel_id = Port;
      channel.frequency_index = entry.index;
      channel.rf_frequency_hz = entry.rf_frequency_hz;
      channel.iq_offset_frequency_hz = entry.iq_offset_frequency_hz;
      channel.downconverter_passthrough =
          input.sidecar.downconverter.passthrough;
      channel.downconverter_translation_frequency_hz =
          input.sidecar.downconverter.translation_frequency_hz;
      channel.channel_sample_rate_hz =
          config_.frequency.sample_rate_hz / config_.decimation_factor;
      channel.decimation_factor = config_.decimation_factor;
      channel.filter_group_delay_input_samples =
          static_cast<std::int64_t>((config_.fir_tap_count - 1u) / 2u);
      const auto group_delay =
          static_cast<std::uint64_t>(channel.filter_group_delay_input_samples);
      std::uint64_t first_causal = global_start;
      if (kernel_output->has_output) {
        first_causal = kernel_output->first_causal_input_global_sample;
        if (first_causal < group_delay) {
          return false;
        }
        channel.input_global_start_sample = first_causal - group_delay;
      } else {
        if (global_start >
            std::numeric_limits<std::uint64_t>::max() - group_delay) {
          return false;
        }
        channel.input_global_start_sample = global_start;
        first_causal = global_start + group_delay;
      }
      channel.channel_global_start_sample = first_causal;
      channel.sample_time_map = input.sidecar.iq.sample_time_map;
      // NormalizeToGlobalStartSample applies output_start * decimation before
      // subtracting group delay. Preserve any non-integral delay/decimation
      // phase in the anchor so channel sample zero maps to the FIR center time.
      const auto delay_remainder = group_delay % config_.decimation_factor;
      if (channel.input_global_start_sample >
          std::numeric_limits<std::uint64_t>::max() - delay_remainder) {
        return false;
      }
      channel.sample_time_map.input_packet_global_start_sample =
          channel.input_global_start_sample + delay_remainder;
      channel.sample_time_map.output_start_sample =
          group_delay / config_.decimation_factor;
      channel.sample_time_map.decimation_factor = config_.decimation_factor;
      channel.sample_time_map.group_delay_input_samples =
          channel.filter_group_delay_input_samples;
      channel.sample_time_map.input_sample_rate_hz =
          config_.frequency.sample_rate_hz;
      channel.sample_time_map.output_sample_rate_hz =
          channel.channel_sample_rate_hz;
      output.sidecar.receiver_guard_or_metadata_channel =
          IsReservedFrequencyIndex(entry.index);
      auto samples = std::make_shared<const std::vector<std::complex<double>>>(
          std::move(kernel_output->samples));
      allocation_cycle_bytes_ += kernels_[Port].AllocationHighWaterBytes();
      allocation_high_water_bytes_ =
          std::max(allocation_high_water_bytes_, allocation_cycle_bytes_);
      output.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
          samples, samples->size(), channel.sample_time_map);
      if (!Base::template EnqueueOutput<Port>(output)) {
        return false;
      }
      return EnqueueAllOutputs<Port + 1>(input, selected);
    }
    if (graph::IsTerminalEdgeControl(input.edge_control)) {
      for (auto &kernel : kernels_) {
        kernel.Reset();
      }
      initialized_ = false;
    }
    return true;
  }

  FHSSProductionChannelizerConfig config_{};
  std::array<FHSSFirChannelizerKernel, kOutputPortCount> kernels_{};
  bool initialized_ = false;
  std::size_t allocation_cycle_bytes_ = 0u;
  std::size_t allocation_high_water_bytes_ = 0u;
};

} // namespace dsp::fhss
