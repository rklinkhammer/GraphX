#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace dsp::fhss {

struct PerChannelPulseDetectorConfig {
  std::uint64_t detector_id = 0;
  std::uint64_t packet_sequence = 0;
  double min_power_linear = 1.0e-12;
  double min_symbol_coherence = 0.5;
  double noise_floor_db = -120.0;
  double nominal_bandwidth_hz = 5'000'000.0;
  std::uint64_t max_pulse_input_samples =
      FHSSProtocolConstants::kPulseWidthSamples;
};

[[nodiscard]] inline FHSSVoidResult ValidatePerChannelPulseDetectorConfig(
    const PerChannelPulseDetectorConfig &config) {
  if (config.min_power_linear < 0.0 || config.min_symbol_coherence < 0.0 ||
      config.min_symbol_coherence > 1.0 || config.nominal_bandwidth_hz < 0.0 ||
      config.max_pulse_input_samples == 0) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "per-channel detector thresholds and pulse length must be valid"));
  }
  return {};
}

[[nodiscard]] inline PerChannelPulseDetectorConfig
PerChannelPulseDetectorConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  PerChannelPulseDetectorConfig config{};
  config.detector_id = FHSSJsonUint64(json, "detector_id", 0);
  config.packet_sequence = FHSSJsonUint64(json, "packet_sequence", 0);
  config.min_power_linear =
      FHSSJsonDouble(json, "min_power_linear", config.min_power_linear);
  config.min_symbol_coherence = FHSSJsonDouble(
      json, "min_symbol_coherence", config.min_symbol_coherence);
  config.noise_floor_db =
      FHSSJsonDouble(json, "noise_floor_db", config.noise_floor_db);
  config.nominal_bandwidth_hz = FHSSJsonDouble(
      json, "nominal_bandwidth_hz", config.nominal_bandwidth_hz);
  config.max_pulse_input_samples = FHSSJsonUint64(
      json, "max_pulse_input_samples", config.max_pulse_input_samples);
  if (auto validation = ValidatePerChannelPulseDetectorConfig(config);
      !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

class PerChannelPulseDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSChannelizedIqToken>,
          graph::TypeList<FHSSPerChannelPulseEvidenceToken>,
          PerChannelPulseDetectorNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
  using InputTokenType = FHSSChannelizedIqToken;
  using OutputTokenType = FHSSPerChannelPulseEvidenceToken;

  PerChannelPulseDetectorNode() = default;
  explicit PerChannelPulseDetectorNode(PerChannelPulseDetectorConfig config)
      : config_(config) {}

  void SetConfig(PerChannelPulseDetectorConfig config) { config_ = config; }

  void Configure(const graph::JsonView &cfg) override {
    SetConfig(PerChannelPulseDetectorConfigFromJson(cfg));
  }

  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params;
    params["detector_id"] = config_.detector_id;
    params["packet_sequence"] = config_.packet_sequence;
    params["min_power_linear"] = config_.min_power_linear;
    params["min_symbol_coherence"] = config_.min_symbol_coherence;
    params["noise_floor_db"] = config_.noise_floor_db;
    params["nominal_bandwidth_hz"] = config_.nominal_bandwidth_hz;
    params["max_pulse_input_samples"] = config_.max_pulse_input_samples;
    return FHSSStableParameterJsonView(std::move(params));
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"detector_id", "packet_sequence", "min_power_linear",
            "min_symbol_coherence", "noise_floor_db", "nominal_bandwidth_hz",
            "max_pulse_input_samples"};
  }

  [[nodiscard]] std::size_t LastDetectedPulseCount() const noexcept {
    return last_detected_pulse_count_;
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (auto validation = ValidatePerChannelPulseDetectorConfig(config_);
        !validation) {
      return std::nullopt;
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return std::nullopt;
    }
    if (auto index =
            ValidateFrequencyIndex(input.sidecar.channel.frequency_index);
        !index) {
      return std::nullopt;
    }
    if (input.sidecar.channel.channel_id !=
        input.sidecar.channel.frequency_index) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.edge_control = input.edge_control;
    output.sidecar.channel = input.sidecar.channel;
    output.sidecar.channel_iq = input.sidecar.iq;

    const auto pulse_channel_samples = ChannelSamplesForOnePulse(input.sidecar);
    const auto period_channel_samples =
        ChannelSamplesForInputSamples(input.sidecar,
                                      FHSSProtocolConstants::kPulsePeriodSamples);
    if (pulse_channel_samples == 0 || period_channel_samples == 0) {
      return output;
    }

    for (std::uint64_t offset = input.sidecar.iq.sample_offset;
         offset + pulse_channel_samples <=
         input.sidecar.iq.sample_offset + input.sidecar.iq.sample_count;
         offset += period_channel_samples) {
      auto metadata =
          BuildPulseMetadata(input.sidecar, offset, pulse_channel_samples);
      if (!metadata.has_value()) {
        return std::nullopt;
      }
      if (metadata->confidence < config_.min_symbol_coherence) {
        continue;
      }
      if (metadata->power_db <
          10.0 * std::log10(std::max(config_.min_power_linear,
                                     std::numeric_limits<double>::min()))) {
        metadata->confidence = 0.0;
      }
      if (metadata->confidence <= 0.0) {
        continue;
      }

      auto evidence = input.sidecar.iq;
      evidence.sample_offset = offset;
      evidence.sample_count = pulse_channel_samples;
      evidence.sample_time_map = input.sidecar.channel.sample_time_map;

      output.sidecar.detected_pulses.push_back(*metadata);
      output.sidecar.pulse_evidence.push_back(std::move(evidence));
    }
    last_detected_pulse_count_ = output.sidecar.detected_pulses.size();
    return output;
  }

private:
  std::size_t last_detected_pulse_count_{0};
  [[nodiscard]] std::uint64_t ChannelSamplesForInputSamples(
      const FHSSChannelizedIqPacket &packet,
      std::uint64_t input_samples) const {
    const auto decimation =
        std::max<std::uint32_t>(1, packet.channel.decimation_factor);
    return (input_samples + decimation - 1) / decimation;
  }

  [[nodiscard]] std::uint64_t ChannelSamplesForOnePulse(
      const FHSSChannelizedIqPacket &packet) const {
    const auto requested_input_samples =
        std::min<std::uint64_t>(config_.max_pulse_input_samples,
                                FHSSProtocolConstants::kPulseWidthSamples);
    return ChannelSamplesForInputSamples(packet, requested_input_samples);
  }

  [[nodiscard]] std::optional<FHSSGraphXPulseMetadata> BuildPulseMetadata(
      const FHSSChannelizedIqPacket &packet, std::uint64_t channel_offset,
      std::uint64_t channel_samples) const {
    const auto global_start = NormalizeToGlobalStartSample(
        FHSSSampleTimeMapFromGraphX(packet.channel.sample_time_map),
        channel_offset);
    if (!global_start) {
      return std::nullopt;
    }

    const auto &samples = *packet.iq.host_complex64_samples;
    const auto begin = channel_offset;
    if (begin > samples.size() || channel_samples > samples.size() - begin) {
      return std::nullopt;
    }

    double sum_power = 0.0;
    double peak_amplitude = 0.0;
    for (std::uint64_t i = 0; i < channel_samples; ++i) {
      const auto sample = samples[begin + i];
      const auto amplitude = std::abs(sample);
      peak_amplitude = std::max(peak_amplitude, amplitude);
      sum_power += std::norm(sample);
    }
    const auto mean_power =
        sum_power / static_cast<double>(std::max<std::uint64_t>(
                        1, channel_samples));
    constexpr double kMinPower = 1.0e-24;
    const double power_db = 10.0 * std::log10(std::max(mean_power, kMinPower));
    const double snr_db = power_db - config_.noise_floor_db;
    const double coherence =
        EstimateSymbolCoherence(samples, begin, channel_samples,
                                packet.channel.decimation_factor);

    const auto decimation =
        std::max<std::uint32_t>(1, packet.channel.decimation_factor);
    const auto duration_samples = channel_samples * decimation;

    FHSSDetectedPulse pulse{};
    pulse.global_start_sample = *global_start;
    pulse.global_end_sample = pulse.global_start_sample + duration_samples;
    pulse.duration_samples = duration_samples;
    pulse.channel_start_sample = channel_offset;
    pulse.channel_id = packet.channel.channel_id;
    pulse.frequency_index = packet.channel.frequency_index;
    pulse.rf_frequency_hz = packet.channel.rf_frequency_hz;
    pulse.iq_offset_frequency_hz = packet.channel.iq_offset_frequency_hz;
    pulse.estimated_center_frequency_hz =
        packet.channel.iq_offset_frequency_hz;
    pulse.frequency_error_hz = 0.0;
    pulse.amplitude = peak_amplitude;
    pulse.power_db = power_db;
    pulse.snr_db = snr_db;
    pulse.noise_floor_db = config_.noise_floor_db;
    pulse.phase_at_start_rad = std::arg(samples[begin]);
    pulse.phase_slope_rad_per_sample =
        EstimatePhaseSlope(samples, begin, channel_samples);
    pulse.cfo_hz = 0.0;
    pulse.bandwidth_hz = config_.nominal_bandwidth_hz;
    pulse.confidence = mean_power >= config_.min_power_linear ? coherence : 0.0;
    pulse.detector_id = config_.detector_id;
    pulse.packet_sequence = config_.packet_sequence;

    auto metadata = FHSSGraphXPulseMetadataFromDetectedPulse(
        pulse, packet.channel.sample_time_map);
    metadata.downconverter_passthrough =
        packet.channel.downconverter_passthrough;
    metadata.downconverter_translation_frequency_hz =
        packet.channel.downconverter_translation_frequency_hz;
    return metadata;
  }

  [[nodiscard]] static double EstimatePhaseSlope(
      const std::vector<std::complex<double>> &samples, std::uint64_t begin,
      std::uint64_t count) {
    if (count < 2) {
      return 0.0;
    }
    const auto first = samples[begin];
    const auto last = samples[begin + count - 1];
    return std::arg(last * std::conj(first)) /
           static_cast<double>(count - 1);
  }

  [[nodiscard]] static double EstimateSymbolCoherence(
      const std::vector<std::complex<double>> &samples, std::uint64_t begin,
      std::uint64_t count, std::uint32_t decimation_factor) {
    const auto decimation = std::max<std::uint32_t>(1, decimation_factor);
    const auto samples_per_symbol =
        std::max<std::uint32_t>(1, FHSSProtocolConstants::kSamplesPerSymbol /
                                       decimation);
    if (count < samples_per_symbol) {
      return 0.0;
    }
    double coherence_sum = 0.0;
    std::uint64_t symbols = 0;
    for (std::uint64_t offset = 0; offset + samples_per_symbol <= count;
         offset += samples_per_symbol) {
      std::complex<double> sum{0.0, 0.0};
      for (std::uint32_t i = 0; i < samples_per_symbol; ++i) {
        sum += samples[begin + offset + i];
      }
      coherence_sum += std::abs(sum) / static_cast<double>(samples_per_symbol);
      ++symbols;
    }
    return symbols == 0 ? 0.0 : coherence_sum / static_cast<double>(symbols);
  }

  PerChannelPulseDetectorConfig config_{};
};

} // namespace dsp::fhss
