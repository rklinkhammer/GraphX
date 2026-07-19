/**
 * @file FHSSAcquisitionPulseDetectorNode.hpp
 * @brief Evidence-driven bounded-capture FHSS pulse acquisition detector.
 */
// SPDX-License-Identifier: MIT

#pragma once

#include "config/ConfigError.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "dsp/fhss/FHSSReceiverObservationSource.hpp"
#include "graph/EdgeControl.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

struct FHSSAcquisitionPulseDetectorConfig {
  std::uint64_t detector_id = 0;
  double noise_power_quantile = 0.2;
  double threshold_above_noise_linear = 8.0;
  double release_threshold_ratio = 0.5;
  double min_absolute_power_linear = 1.0e-10;
  double min_symbol_coherence = 0.75;
  std::uint32_t smoothing_window_channel_samples = 8;
  std::uint64_t min_pulse_input_samples = 2'400;
  std::uint64_t max_pulse_input_samples = 4'000;
  std::uint64_t bridge_gap_input_samples = 80;
  std::uint64_t duplicate_tolerance_input_samples = 160;
  std::uint64_t max_buffered_channel_samples = 4'194'304;
  double nominal_bandwidth_hz = 5'000'000.0;
};

[[nodiscard]] inline FHSSVoidResult ValidateFHSSAcquisitionPulseDetectorConfig(
    const FHSSAcquisitionPulseDetectorConfig &config) {
  const bool finite = std::isfinite(config.noise_power_quantile) &&
                      std::isfinite(config.threshold_above_noise_linear) &&
                      std::isfinite(config.release_threshold_ratio) &&
                      std::isfinite(config.min_absolute_power_linear) &&
                      std::isfinite(config.min_symbol_coherence) &&
                      std::isfinite(config.nominal_bandwidth_hz);
  if (!finite || config.noise_power_quantile <= 0.0 ||
      config.noise_power_quantile >= 0.5 ||
      config.threshold_above_noise_linear <= 1.0 ||
      config.release_threshold_ratio <= 0.0 ||
      config.release_threshold_ratio > 1.0 ||
      config.min_absolute_power_linear < 0.0 ||
      config.min_symbol_coherence < 0.0 || config.min_symbol_coherence > 1.0 ||
      config.min_pulse_input_samples == 0u ||
      config.max_pulse_input_samples < config.min_pulse_input_samples ||
      config.max_buffered_channel_samples == 0u ||
      config.smoothing_window_channel_samples == 0u ||
      config.smoothing_window_channel_samples > 1024u ||
      config.nominal_bandwidth_hz <= 0.0) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidTiming,
                  "acquisition detector requires finite ordered thresholds, "
                  "pulse limits, and bounded buffering"));
  }
  return {};
}

[[nodiscard]] inline FHSSAcquisitionPulseDetectorConfig
FHSSAcquisitionPulseDetectorConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSAcquisitionPulseDetectorConfig config{};
  config.detector_id = FHSSJsonUint64(json, "detector_id", 0);
  config.noise_power_quantile =
      FHSSJsonDouble(json, "noise_power_quantile", config.noise_power_quantile);
  config.threshold_above_noise_linear =
      FHSSJsonDouble(json, "threshold_above_noise_linear",
                     config.threshold_above_noise_linear);
  config.release_threshold_ratio = FHSSJsonDouble(
      json, "release_threshold_ratio", config.release_threshold_ratio);
  config.min_absolute_power_linear = FHSSJsonDouble(
      json, "min_absolute_power_linear", config.min_absolute_power_linear);
  config.min_symbol_coherence =
      FHSSJsonDouble(json, "min_symbol_coherence", config.min_symbol_coherence);
  const auto smoothing_window =
      FHSSJsonUint64(json, "smoothing_window_channel_samples",
                     config.smoothing_window_channel_samples);
  if (smoothing_window > std::numeric_limits<std::uint32_t>::max()) {
    throw graph::ConfigError(
        "acquisition detector smoothing window exceeds uint32 range");
  }
  config.smoothing_window_channel_samples =
      static_cast<std::uint32_t>(smoothing_window);
  config.min_pulse_input_samples = FHSSJsonUint64(
      json, "min_pulse_input_samples", config.min_pulse_input_samples);
  config.max_pulse_input_samples = FHSSJsonUint64(
      json, "max_pulse_input_samples", config.max_pulse_input_samples);
  config.bridge_gap_input_samples = FHSSJsonUint64(
      json, "bridge_gap_input_samples", config.bridge_gap_input_samples);
  config.duplicate_tolerance_input_samples =
      FHSSJsonUint64(json, "duplicate_tolerance_input_samples",
                     config.duplicate_tolerance_input_samples);
  config.max_buffered_channel_samples =
      FHSSJsonUint64(json, "max_buffered_channel_samples",
                     config.max_buffered_channel_samples);
  config.nominal_bandwidth_hz =
      FHSSJsonDouble(json, "nominal_bandwidth_hz", config.nominal_bandwidth_hz);
  if (auto validation = ValidateFHSSAcquisitionPulseDetectorConfig(config);
      !validation) {
    throw graph::ConfigError(validation.error().message);
  }
  return config;
}

struct FHSSAcquiredPulseRange {
  std::uint64_t begin = 0;
  std::uint64_t end = 0;
  double noise_power_linear = 0.0;
  double mean_power_linear = 0.0;
  double peak_amplitude = 0.0;
  double symbol_coherence = 0.0;
  double phase_slope_rad_per_sample = 0.0;
  double confidence = 0.0;
};

class FHSSAcquisitionPulseDetectorKernel {
public:
  explicit FHSSAcquisitionPulseDetectorKernel(
      FHSSAcquisitionPulseDetectorConfig config = {})
      : config_(std::move(config)) {}

  [[nodiscard]] FHSSResult<std::vector<FHSSAcquiredPulseRange>>
  Detect(const std::vector<std::complex<double>> &samples,
         std::uint32_t decimation_factor) const {
    if (auto validation = ValidateFHSSAcquisitionPulseDetectorConfig(config_);
        !validation) {
      return std::unexpected(validation.error());
    }
    if (decimation_factor == 0u ||
        samples.size() > config_.max_buffered_channel_samples) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidTiming,
          "acquisition detector input exceeds its bounded capture contract"));
    }
    if (samples.empty()) {
      return std::vector<FHSSAcquiredPulseRange>{};
    }
    std::vector<double> powers;
    powers.reserve(samples.size());
    for (const auto &sample : samples) {
      const double power = std::norm(sample);
      if (!std::isfinite(power)) {
        return std::unexpected(
            MakeError(FHSSValidationCode::InvalidTiming,
                      "acquisition detector rejects non-finite IQ evidence"));
      }
      powers.push_back(power);
    }
    auto sorted = powers;
    const auto quantile_index = static_cast<std::size_t>(
        config_.noise_power_quantile * static_cast<double>(sorted.size() - 1u));
    std::nth_element(sorted.begin(), sorted.begin() + quantile_index,
                     sorted.end());
    // Squared complex Gaussian magnitude is exponential. The bounded-capture
    // contract requires at least half the samples to be background, so map the
    // whole-capture quantile to the conservative 2*q noise-only quantile.
    const double exponential_quantile_scale =
        -std::log(1.0 - 2.0 * config_.noise_power_quantile);
    const double noise_power =
        std::max(sorted[quantile_index] / exponential_quantile_scale,
                 std::numeric_limits<double>::min());
    const double enter_threshold =
        std::max(config_.min_absolute_power_linear,
                 noise_power * config_.threshold_above_noise_linear);
    const double release_threshold =
        enter_threshold * config_.release_threshold_ratio;
    const auto to_channel_samples = [decimation_factor](std::uint64_t value) {
      return value / decimation_factor +
             static_cast<std::uint64_t>(value % decimation_factor != 0u);
    };
    const auto min_duration =
        to_channel_samples(config_.min_pulse_input_samples);
    const auto max_duration =
        to_channel_samples(config_.max_pulse_input_samples);
    const auto bridge_gap =
        to_channel_samples(config_.bridge_gap_input_samples);
    const auto duplicate_tolerance =
        to_channel_samples(config_.duplicate_tolerance_input_samples);

    std::vector<double> detection_powers(powers.size());
    double running_power = 0.0;
    for (std::size_t i = 0; i < powers.size(); ++i) {
      running_power += powers[i];
      if (i >= config_.smoothing_window_channel_samples) {
        running_power -= powers[i - config_.smoothing_window_channel_samples];
      }
      const auto window_count = std::min<std::size_t>(
          i + 1u, config_.smoothing_window_channel_samples);
      detection_powers[i] = running_power / static_cast<double>(window_count);
    }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> raw_ranges;
    bool active = false;
    std::uint64_t start = 0;
    std::uint64_t last_above = 0;
    for (std::uint64_t i = 0; i < powers.size(); ++i) {
      if (!active && detection_powers[i] >= enter_threshold) {
        active = true;
        start = i;
        while (start > 0u && powers[start - 1u] >= release_threshold) {
          --start;
        }
        last_above = i;
      } else if (active && detection_powers[i] >= release_threshold) {
        last_above = i;
      } else if (active && i - last_above > bridge_gap) {
        raw_ranges.emplace_back(start, last_above + 1u);
        active = false;
      }
    }
    if (active) {
      raw_ranges.emplace_back(start, last_above + 1u);
    }

    std::vector<FHSSAcquiredPulseRange> result;
    for (const auto &[raw_begin, raw_end] : raw_ranges) {
      const auto raw_duration = raw_end - raw_begin;
      if (raw_duration < min_duration || raw_duration > max_duration) {
        continue;
      }
      auto begin = raw_begin;
      auto end = raw_end;
      const auto expected_duration =
          to_channel_samples(FHSSProtocolConstants::kPulseWidthSamples);
      if (samples.size() >= expected_duration) {
        const auto search_begin =
            raw_begin > config_.smoothing_window_channel_samples
                ? raw_begin - config_.smoothing_window_channel_samples
                : 0u;
        const auto search_end = std::min<std::uint64_t>(
            raw_end, samples.size() - expected_duration);
        double best_energy = -1.0;
        for (std::uint64_t candidate = search_begin; candidate <= search_end;
             ++candidate) {
          const double energy = std::accumulate(
              powers.begin() + static_cast<std::ptrdiff_t>(candidate),
              powers.begin() +
                  static_cast<std::ptrdiff_t>(candidate + expected_duration),
              0.0);
          if (energy > best_energy) {
            best_energy = energy;
            begin = candidate;
          }
        }
        end = begin + expected_duration;
      }
      const auto duration = end - begin;
      if (duration < min_duration || duration > max_duration) {
        continue;
      }
      double sum_power = 0.0;
      double peak = 0.0;
      std::complex<double> adjacent_sum{0.0, 0.0};
      for (std::uint64_t i = begin; i < end; ++i) {
        sum_power += powers[i];
        peak = std::max(peak, std::abs(samples[i]));
        if (i != begin) {
          adjacent_sum += samples[i] * std::conj(samples[i - 1u]);
        }
      }
      const double mean = sum_power / static_cast<double>(duration);
      const double coherence =
          EstimateSymbolCoherence(samples, begin, duration, decimation_factor);
      if (coherence < config_.min_symbol_coherence) {
        continue;
      }
      const double phase_slope = std::arg(adjacent_sum);
      const double snr_ratio = mean / noise_power;
      const double snr_confidence =
          std::clamp(std::log10(std::max(1.0, snr_ratio)) / 3.0, 0.0, 1.0);
      const double expected = static_cast<double>(
          to_channel_samples(FHSSProtocolConstants::kPulseWidthSamples));
      const double duration_score =
          std::clamp(1.0 - std::abs(static_cast<double>(duration) - expected) /
                               std::max(1.0, expected),
                     0.0, 1.0);
      FHSSAcquiredPulseRange candidate{
          .begin = begin,
          .end = end,
          .noise_power_linear = noise_power,
          .mean_power_linear = mean,
          .peak_amplitude = peak,
          .symbol_coherence = coherence,
          .phase_slope_rad_per_sample = phase_slope,
          .confidence = coherence * snr_confidence * duration_score};
      const bool overlaps_previous =
          !result.empty() &&
          (candidate.begin <= result.back().end ||
           candidate.begin - result.back().end <= duplicate_tolerance);
      if (overlaps_previous) {
        if (candidate.confidence > result.back().confidence) {
          result.back() = candidate;
        }
        continue;
      }
      result.push_back(candidate);
    }
    allocation_high_water_bytes_ = std::max(
        allocation_high_water_bytes_,
        (powers.capacity() + sorted.capacity() + detection_powers.capacity()) *
                sizeof(double) +
            raw_ranges.capacity() *
                sizeof(std::pair<std::uint64_t, std::uint64_t>) +
            result.capacity() * sizeof(FHSSAcquiredPulseRange));
    return result;
  }

  [[nodiscard]] std::size_t AllocationHighWaterBytes() const noexcept {
    return allocation_high_water_bytes_;
  }

private:
  [[nodiscard]] static double
  EstimateSymbolCoherence(const std::vector<std::complex<double>> &samples,
                          std::uint64_t begin, std::uint64_t count,
                          std::uint32_t decimation_factor) {
    const auto samples_per_symbol = std::max<std::uint32_t>(
        1u, FHSSProtocolConstants::kSamplesPerSymbol / decimation_factor);
    if (count < samples_per_symbol) {
      return 0.0;
    }
    double sum = 0.0;
    std::uint64_t symbols = 0;
    for (std::uint64_t offset = 0; offset + samples_per_symbol <= count;
         offset += samples_per_symbol) {
      std::complex<double> accumulator{0.0, 0.0};
      double amplitude_sum = 0.0;
      for (std::uint32_t i = 0; i < samples_per_symbol; ++i) {
        const auto sample = samples[begin + offset + i];
        accumulator += sample;
        amplitude_sum += std::abs(sample);
      }
      sum += amplitude_sum == 0.0 ? 0.0 : std::abs(accumulator) / amplitude_sum;
      ++symbols;
    }
    return symbols == 0u ? 0.0 : sum / static_cast<double>(symbols);
  }

  FHSSAcquisitionPulseDetectorConfig config_{};
  mutable std::size_t allocation_high_water_bytes_ = 0u;
};

[[nodiscard]] inline bool
FHSSGraphXSampleTimeMapsEqual(const FHSSGraphXSampleTimeMap &lhs,
                              const FHSSGraphXSampleTimeMap &rhs) {
  return lhs.has_input_global_start_sample ==
             rhs.has_input_global_start_sample &&
         lhs.input_packet_global_start_sample ==
             rhs.input_packet_global_start_sample &&
         lhs.output_start_sample == rhs.output_start_sample &&
         lhs.decimation_factor == rhs.decimation_factor &&
         lhs.group_delay_input_samples == rhs.group_delay_input_samples &&
         NearlyEqual(lhs.input_sample_rate_hz, rhs.input_sample_rate_hz) &&
         NearlyEqual(lhs.output_sample_rate_hz, rhs.output_sample_rate_hz);
}

[[nodiscard]] inline bool
FHSSAcquisitionDetectorMetadataIsValid(const FHSSChannelizedIqPacket &packet) {
  const auto &channel = packet.channel;
  const auto &map = channel.sample_time_map;
  if (!ValidateFrequencyIndex(channel.frequency_index).has_value() ||
      channel.channel_id != channel.frequency_index ||
      !std::isfinite(channel.rf_frequency_hz) ||
      !std::isfinite(channel.iq_offset_frequency_hz) ||
      !std::isfinite(channel.downconverter_translation_frequency_hz) ||
      !std::isfinite(channel.channel_sample_rate_hz) ||
      !NearlyEqual(channel.rf_frequency_hz,
                   RfFrequencyHz(channel.frequency_index)) ||
      channel.decimation_factor == 0u ||
      channel.filter_group_delay_input_samples < 0 ||
      !map.has_input_global_start_sample ||
      map.decimation_factor != channel.decimation_factor ||
      map.group_delay_input_samples !=
          channel.filter_group_delay_input_samples ||
      !NearlyEqual(map.input_sample_rate_hz,
                   FHSSProtocolConstants::kSampleRateHz) ||
      !NearlyEqual(map.output_sample_rate_hz, channel.channel_sample_rate_hz) ||
      !NearlyEqual(channel.channel_sample_rate_hz,
                   map.input_sample_rate_hz /
                       static_cast<double>(channel.decimation_factor)) ||
      !FHSSGraphXSampleTimeMapsEqual(packet.iq.sample_time_map, map)) {
    return false;
  }
  const auto delay =
      static_cast<std::uint64_t>(channel.filter_group_delay_input_samples);
  if (channel.input_global_start_sample >
          std::numeric_limits<std::uint64_t>::max() - delay ||
      channel.channel_global_start_sample !=
          channel.input_global_start_sample + delay) {
    return false;
  }
  const auto normalized =
      NormalizeToGlobalStartSample(FHSSSampleTimeMapFromGraphX(map), 0u);
  return normalized.has_value() &&
         *normalized == channel.input_global_start_sample;
}

[[nodiscard]] inline bool
FHSSAcquisitionDetectorSameStream(const FHSSGraphXChannelMetadata &lhs,
                                  const FHSSGraphXChannelMetadata &rhs) {
  return lhs.channel_id == rhs.channel_id &&
         lhs.frequency_index == rhs.frequency_index &&
         NearlyEqual(lhs.rf_frequency_hz, rhs.rf_frequency_hz) &&
         NearlyEqual(lhs.iq_offset_frequency_hz, rhs.iq_offset_frequency_hz) &&
         lhs.downconverter_passthrough == rhs.downconverter_passthrough &&
         NearlyEqual(lhs.downconverter_translation_frequency_hz,
                     rhs.downconverter_translation_frequency_hz) &&
         NearlyEqual(lhs.channel_sample_rate_hz, rhs.channel_sample_rate_hz) &&
         lhs.decimation_factor == rhs.decimation_factor &&
         lhs.filter_group_delay_input_samples ==
             rhs.filter_group_delay_input_samples;
}

class FHSSAcquisitionPulseDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSChannelizedIqToken>,
          graph::TypeList<FHSSPerChannelPulseEvidenceToken>,
          FHSSAcquisitionPulseDetectorNode>,
      public graph::IConfigurable,
      public graph::IParameterized,
      public IFHSSReceiverObservationSource {
public:
  using InputTokenType = FHSSChannelizedIqToken;
  using OutputTokenType = FHSSPerChannelPulseEvidenceToken;

  FHSSAcquisitionPulseDetectorNode() = default;
  explicit FHSSAcquisitionPulseDetectorNode(
      FHSSAcquisitionPulseDetectorConfig config)
      : config_(std::move(config)), kernel_(config_) {}

  void Configure(const graph::JsonView &cfg) override {
    config_ = FHSSAcquisitionPulseDetectorConfigFromJson(cfg);
    kernel_ = FHSSAcquisitionPulseDetectorKernel{config_};
    Reset();
  }
  [[nodiscard]] graph::JsonView GetParameters() const override {
    nlohmann::json params{
        {"detector_id", config_.detector_id},
        {"noise_power_quantile", config_.noise_power_quantile},
        {"threshold_above_noise_linear", config_.threshold_above_noise_linear},
        {"release_threshold_ratio", config_.release_threshold_ratio},
        {"min_absolute_power_linear", config_.min_absolute_power_linear},
        {"min_symbol_coherence", config_.min_symbol_coherence},
        {"smoothing_window_channel_samples",
         config_.smoothing_window_channel_samples},
        {"min_pulse_input_samples", config_.min_pulse_input_samples},
        {"max_pulse_input_samples", config_.max_pulse_input_samples},
        {"bridge_gap_input_samples", config_.bridge_gap_input_samples},
        {"duplicate_tolerance_input_samples",
         config_.duplicate_tolerance_input_samples},
        {"max_buffered_channel_samples", config_.max_buffered_channel_samples},
        {"nominal_bandwidth_hz", config_.nominal_bandwidth_hz}};
    return FHSSStableParameterJsonView(std::move(params));
  }
  [[nodiscard]] graph::JsonView
  GetParameterDescription(const std::string &name) const override {
    static const nlohmann::json descriptions{
        {"detector_id", "Stable evidence-source identifier."},
        {"noise_power_quantile",
         "Whole-capture power quantile used for noise estimation."},
        {"threshold_above_noise_linear",
         "Detection threshold multiplier above estimated noise."},
        {"release_threshold_ratio",
         "Hysteretic release-to-enter threshold ratio."},
        {"min_absolute_power_linear",
         "Absolute lower bound for the enter threshold."},
        {"min_symbol_coherence", "Minimum within-symbol complex coherence."},
        {"smoothing_window_channel_samples",
         "Moving-power window in channel samples."},
        {"min_pulse_input_samples",
         "Minimum accepted duration in input samples."},
        {"max_pulse_input_samples",
         "Maximum accepted duration in input samples."},
        {"bridge_gap_input_samples",
         "Maximum below-threshold gap bridged in input samples."},
        {"duplicate_tolerance_input_samples",
         "Duplicate association tolerance in input samples."},
        {"max_buffered_channel_samples",
         "Hard capture allocation bound in channel samples."},
        {"nominal_bandwidth_hz", "Reported detector bandwidth in hertz."}};
    if (!descriptions.contains(name)) {
      return FHSSStableParameterDescriptionJsonView(nlohmann::json::object());
    }
    return FHSSStableParameterDescriptionJsonView(
        {{"name", name}, {"description", descriptions.at(name)}});
  }
  [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
    return {"detector_id",
            "noise_power_quantile",
            "threshold_above_noise_linear",
            "release_threshold_ratio",
            "min_absolute_power_linear",
            "min_symbol_coherence",
            "smoothing_window_channel_samples",
            "min_pulse_input_samples",
            "max_pulse_input_samples",
            "bridge_gap_input_samples",
            "duplicate_tolerance_input_samples",
            "max_buffered_channel_samples",
            "nominal_bandwidth_hz"};
  }

  [[nodiscard]] std::size_t LastDetectedPulseCount() const noexcept {
    const std::lock_guard lock(diagnostics_mutex_);
    return last_detected_pulse_count_;
  }
  [[nodiscard]] std::size_t AllocationHighWaterBytes() const noexcept {
    const std::lock_guard lock(diagnostics_mutex_);
    return allocation_high_water_bytes_;
  }
  [[nodiscard]] graph::JsonView GetDiagnostics() const override {
    thread_local nlohmann::json diagnostics_snapshot;
    const std::lock_guard lock(diagnostics_mutex_);
    diagnostics_snapshot = {
        {"schema", "graphx.fhss.acquisition_detector.diagnostics.v1"},
        {"allocation_high_water_bytes", allocation_high_water_bytes_},
        {"last_detected_pulse_count", last_detected_pulse_count_}};
    return graph::JsonView(diagnostics_snapshot);
  }
  [[nodiscard]] std::shared_ptr<const FHSSReceiverNodeObservationSnapshot>
  SnapshotReceiverObservation() const override {
    const std::lock_guard lock(diagnostics_mutex_);
    auto snapshot = std::make_shared<FHSSReceiverNodeObservationSnapshot>();
    snapshot->source_schema =
        "graphx.fhss.acquisition_detector.observation.v1";
    snapshot->source_kind = "acquisition_detector";
    snapshot->allocation_high_water_bytes = allocation_high_water_bytes_;
    snapshot->detected_pulse_count = last_detected_pulse_count_;
    return snapshot;
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (auto validation = ValidateFHSSAcquisitionPulseDetectorConfig(config_);
        !validation) {
      return RejectInput();
    }
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq) ||
        !FHSSAcquisitionDetectorMetadataIsValid(input.sidecar)) {
      return RejectInput();
    }
    const auto &source = *input.sidecar.iq.host_complex64_samples;
    const auto offset = input.sidecar.iq.sample_offset;
    const auto count = input.sidecar.iq.sample_count;
    if (offset > source.size() || count > source.size() - offset ||
        count > config_.max_buffered_channel_samples ||
        buffered_samples_.size() >
            config_.max_buffered_channel_samples - count) {
      return RejectInput();
    }
    if (count != 0u) {
      if (!first_packet_.has_value()) {
        if (count > std::numeric_limits<std::uint64_t>::max() /
                        input.sidecar.channel.decimation_factor ||
            input.sidecar.channel.input_global_start_sample >
                std::numeric_limits<std::uint64_t>::max() -
                    count * input.sidecar.channel.decimation_factor) {
          return RejectInput();
        }
        first_packet_ = input.sidecar;
        next_input_global_sample_ =
            input.sidecar.channel.input_global_start_sample +
            count * input.sidecar.channel.decimation_factor;
      } else {
        if (!FHSSAcquisitionDetectorSameStream(input.sidecar.channel,
                                               first_packet_->channel) ||
            input.sidecar.channel.input_global_start_sample !=
                next_input_global_sample_) {
          return RejectInput();
        }
        if (count > std::numeric_limits<std::uint64_t>::max() /
                        input.sidecar.channel.decimation_factor ||
            next_input_global_sample_ >
                std::numeric_limits<std::uint64_t>::max() -
                    count * input.sidecar.channel.decimation_factor) {
          return RejectInput();
        }
        next_input_global_sample_ +=
            count * input.sidecar.channel.decimation_factor;
      }
      buffered_samples_.insert(
          buffered_samples_.end(),
          source.begin() + static_cast<std::ptrdiff_t>(offset),
          source.begin() + static_cast<std::ptrdiff_t>(offset + count));
      {
        const std::lock_guard lock(diagnostics_mutex_);
        allocation_high_water_bytes_ = std::max(
            allocation_high_water_bytes_,
            buffered_samples_.capacity() * sizeof(std::complex<double>));
      }
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.edge_control = input.edge_control;
    output.sidecar.channel = first_packet_.has_value() ? first_packet_->channel
                                                       : input.sidecar.channel;
    output.sidecar.correlation = first_packet_.has_value()
                                     ? first_packet_->correlation
                                     : input.sidecar.correlation;
    if (!graph::IsTerminalEdgeControl(input.edge_control)) {
      return output;
    }
    if (!graph::IsSuccessfulTerminalEdgeControl(input.edge_control)) {
      Reset();
      return output;
    }
    if (!first_packet_.has_value()) {
      Reset();
      return output;
    }
    auto detections = kernel_.Detect(buffered_samples_,
                                     first_packet_->channel.decimation_factor);
    if (!detections) {
      return RejectInput();
    }
    auto shared = std::make_shared<const std::vector<std::complex<double>>>(
        buffered_samples_);
    output.sidecar.channel_iq = FHSSGraphXComplexEvidenceFromHostSamples(
        shared, shared->size(), first_packet_->channel.sample_time_map);
    for (const auto &detection : *detections) {
      auto global_start = NormalizeToGlobalStartSample(
          FHSSSampleTimeMapFromGraphX(first_packet_->channel.sample_time_map),
          detection.begin);
      if (!global_start) {
        return RejectInput();
      }
      const auto duration_channel = detection.end - detection.begin;
      if (duration_channel > std::numeric_limits<std::uint64_t>::max() /
                                 first_packet_->channel.decimation_factor) {
        return RejectInput();
      }
      const auto duration_input =
          duration_channel * first_packet_->channel.decimation_factor;
      if (*global_start >
          std::numeric_limits<std::uint64_t>::max() - duration_input) {
        return RejectInput();
      }
      const double noise_db =
          10.0 * std::log10(std::max(detection.noise_power_linear,
                                     std::numeric_limits<double>::min()));
      const double power_db =
          10.0 * std::log10(std::max(detection.mean_power_linear,
                                     std::numeric_limits<double>::min()));
      const double signal_power = std::max(
          0.0, detection.mean_power_linear - detection.noise_power_linear);
      const double snr_db =
          10.0 *
          std::log10(std::max(signal_power / detection.noise_power_linear,
                              std::numeric_limits<double>::min()));
      const double cfo_hz = detection.phase_slope_rad_per_sample *
                            first_packet_->channel.channel_sample_rate_hz /
                            6.283185307179586476925286766559;
      FHSSGraphXPulseMetadata metadata{};
      metadata.timing.global_start_sample = *global_start;
      metadata.timing.global_end_sample = *global_start + duration_input;
      metadata.timing.duration_samples = duration_input;
      metadata.timing.channel_start_sample = detection.begin;
      metadata.timing.channel_id = first_packet_->channel.channel_id;
      metadata.timing.sample_time_map = first_packet_->channel.sample_time_map;
      metadata.frequency.frequency_index =
          first_packet_->channel.frequency_index;
      metadata.frequency.rf_frequency_hz =
          first_packet_->channel.rf_frequency_hz;
      metadata.frequency.iq_offset_frequency_hz =
          first_packet_->channel.iq_offset_frequency_hz;
      metadata.frequency.estimated_center_frequency_hz =
          first_packet_->channel.iq_offset_frequency_hz + cfo_hz;
      metadata.frequency.frequency_error_hz = cfo_hz;
      metadata.downconverter_passthrough =
          first_packet_->channel.downconverter_passthrough;
      metadata.downconverter_translation_frequency_hz =
          first_packet_->channel.downconverter_translation_frequency_hz;
      metadata.amplitude = detection.peak_amplitude;
      metadata.power_db = power_db;
      metadata.snr_db = snr_db;
      metadata.noise_floor_db = noise_db;
      metadata.phase_at_start_rad = std::arg((*shared)[detection.begin]);
      metadata.phase_slope_rad_per_sample =
          detection.phase_slope_rad_per_sample;
      metadata.cfo_hz = cfo_hz;
      metadata.bandwidth_hz = config_.nominal_bandwidth_hz;
      metadata.confidence = detection.confidence;
      metadata.detector_id = config_.detector_id;
      metadata.packet_sequence = input.token_id;
      output.sidecar.detected_pulses.push_back(metadata);
      auto evidence = output.sidecar.channel_iq;
      evidence.sample_offset = detection.begin;
      evidence.sample_count = duration_channel;
      output.sidecar.pulse_evidence.push_back(std::move(evidence));
    }
    {
      const std::lock_guard lock(diagnostics_mutex_);
      last_detected_pulse_count_ = output.sidecar.detected_pulses.size();
      allocation_high_water_bytes_ = std::max(
          allocation_high_water_bytes_,
                 buffered_samples_.capacity() * sizeof(std::complex<double>) +
                     kernel_.AllocationHighWaterBytes() +
                     output.sidecar.detected_pulses.capacity() *
                         sizeof(FHSSGraphXPulseMetadata) +
                     output.sidecar.pulse_evidence.capacity() *
              sizeof(FHSSGraphXComplexEvidence));
    }
    Reset();
    return output;
  }

private:
  [[nodiscard]] std::optional<OutputTokenType> RejectInput() {
    {
      const std::lock_guard lock(diagnostics_mutex_);
      last_detected_pulse_count_ = 0u;
    }
    Reset();
    return std::nullopt;
  }

  void Reset() {
    buffered_samples_.clear();
    first_packet_.reset();
    next_input_global_sample_ = 0;
  }

  FHSSAcquisitionPulseDetectorConfig config_{};
  FHSSAcquisitionPulseDetectorKernel kernel_{config_};
  std::vector<std::complex<double>> buffered_samples_;
  std::optional<FHSSChannelizedIqPacket> first_packet_;
  std::uint64_t next_input_global_sample_ = 0;
  std::size_t last_detected_pulse_count_ = 0;
  std::size_t allocation_high_water_bytes_ = 0u;
  mutable std::mutex diagnostics_mutex_;
};

} // namespace dsp::fhss
