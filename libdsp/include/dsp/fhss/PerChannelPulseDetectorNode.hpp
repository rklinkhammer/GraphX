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
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace dsp::fhss {

struct PerChannelPulseDetectorConfig {
  std::uint64_t detector_id = 0;
  std::uint64_t packet_sequence = 0;
  double min_power_linear = 1.0e-12;
  double noise_floor_db = -120.0;
  double nominal_bandwidth_hz = 5'000'000.0;
  std::uint64_t max_pulse_input_samples =
      FHSSProtocolConstants::kPulseWidthSamples;
};

[[nodiscard]] inline FHSSVoidResult ValidatePerChannelPulseDetectorConfig(
    const PerChannelPulseDetectorConfig &config) {
  if (config.min_power_linear < 0.0 || config.nominal_bandwidth_hz < 0.0 ||
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
    params["noise_floor_db"] = config_.noise_floor_db;
    params["nominal_bandwidth_hz"] = config_.nominal_bandwidth_hz;
    params["max_pulse_input_samples"] = config_.max_pulse_input_samples;
    return graph::JsonView(params);
  }

  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &) const override {
    return graph::JsonView(nlohmann::json::object());
  }

  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"detector_id", "packet_sequence", "min_power_linear",
            "noise_floor_db", "nominal_bandwidth_hz",
            "max_pulse_input_samples"};
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
    output.sidecar.channel = input.sidecar.channel;
    output.sidecar.channel_iq = input.sidecar.iq;
    output.sidecar.truth_metadata_required_for_decision = false;

    const auto channel_samples =
        std::min<std::uint64_t>(input.sidecar.iq.sample_count,
                                ChannelSamplesForOnePulse(input.sidecar));
    if (channel_samples == 0) {
      return output;
    }

    auto metadata = BuildPulseMetadata(input.sidecar, channel_samples);
    if (!metadata.has_value()) {
      return std::nullopt;
    }
    if (metadata->power_db <
        10.0 * std::log10(std::max(config_.min_power_linear,
                                   std::numeric_limits<double>::min()))) {
      metadata->confidence = 0.0;
    }

    auto evidence = input.sidecar.iq;
    evidence.sample_count = channel_samples;
    evidence.sample_time_map = input.sidecar.channel.sample_time_map;
    evidence.truth_metadata_required_for_decision = false;

    output.sidecar.detected_pulses.push_back(*metadata);
    output.sidecar.pulse_evidence.push_back(std::move(evidence));
    return output;
  }

private:
  [[nodiscard]] std::uint64_t ChannelSamplesForOnePulse(
      const FHSSChannelizedIqPacket &packet) const {
    const auto decimation =
        std::max<std::uint32_t>(1, packet.channel.decimation_factor);
    const auto requested_input_samples =
        std::min<std::uint64_t>(config_.max_pulse_input_samples,
                                FHSSProtocolConstants::kPulseWidthSamples);
    return (requested_input_samples + decimation - 1) / decimation;
  }

  [[nodiscard]] std::optional<FHSSGraphXPulseMetadata> BuildPulseMetadata(
      const FHSSChannelizedIqPacket &packet,
      std::uint64_t channel_samples) const {
    const auto global_start = NormalizeToGlobalStartSample(
        FHSSSampleTimeMapFromGraphX(packet.channel.sample_time_map),
        packet.iq.sample_offset);
    if (!global_start) {
      return std::nullopt;
    }

    const auto &samples = *packet.iq.host_complex64_samples;
    const auto begin = packet.iq.sample_offset;
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

    const auto decimation =
        std::max<std::uint32_t>(1, packet.channel.decimation_factor);
    const auto duration_samples = channel_samples * decimation;

    FHSSDetectedPulse pulse{};
    pulse.global_start_sample = *global_start;
    pulse.global_end_sample = pulse.global_start_sample + duration_samples;
    pulse.duration_samples = duration_samples;
    pulse.channel_start_sample = packet.iq.sample_offset;
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
    pulse.confidence = mean_power >= config_.min_power_linear ? 1.0 : 0.0;
    pulse.detector_id = config_.detector_id;
    pulse.packet_sequence = config_.packet_sequence;

    return FHSSGraphXPulseMetadataFromDetectedPulse(
        pulse, packet.channel.sample_time_map);
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

  PerChannelPulseDetectorConfig config_{};
};

} // namespace dsp::fhss
