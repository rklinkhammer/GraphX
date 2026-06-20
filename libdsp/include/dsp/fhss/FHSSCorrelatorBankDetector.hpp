/**
 * @file FHSSCorrelatorBankDetector.hpp
 * @brief PR1 FHSS correlator-bank detector helper.
 *
 * @details CPU-only known-slot detector for the deterministic FHSS fixture.
 * This is not a graph runtime node, channelizer, decoder, or RF-performance
 * claim. It emits timing/frequency candidates and dehopped complex evidence for
 * later CPSM branch metrics.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSPulseMerge.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>

namespace dsp::fhss {

struct FHSSCorrelatorBankDetectorConfig {
  FHSSDecodeConfig decode_config{};
  std::uint64_t input_packet_global_start_sample = 0;
  std::uint64_t message_start_sample = 0;
  std::uint64_t detector_id = 0;
  std::uint64_t packet_sequence = 0;
  bool allow_overlap = false;
};

struct FHSSFrequencyScore {
  std::uint32_t frequency_index = 0;
  double iq_offset_frequency_hz = 0.0;
  double score = 0.0;
};

struct FHSSCorrelatorBankDetectionResult {
  std::vector<FHSSLocalPulseDetection> local_detections;
  std::vector<std::vector<FHSSFrequencyScore>> slot_scores;
  std::uint32_t evaluated_frequency_count = 0;
};

[[nodiscard]] inline FHSSVoidResult ValidateCorrelatorBankDetectorConfig(
    const FHSSCorrelatorBankDetectorConfig &config) {
  if (config.allow_overlap) {
    return std::unexpected(
        MakeError(FHSSValidationCode::UnsupportedOverlap,
                  "overlapped messages are unsupported in the PR1 detector"));
  }
  if (config.message_start_sample != 0) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "PR1 correlator-bank detector requires message_start_sample = 0"));
  }
  if (auto timing = DeriveTimingModel(config.decode_config.timing); !timing) {
    return std::unexpected(timing.error());
  }
  if (auto frequency = ValidateFrequencyConfig(config.decode_config.frequency);
      !frequency) {
    return std::unexpected(frequency.error());
  }
  if (auto active = ValidateActiveFrequencySet(
          config.decode_config.active_frequency_indices);
      !active) {
    return std::unexpected(active.error());
  }
  if (auto preamble = ValidatePreamblePattern(
          config.decode_config.preamble_pulses,
          config.decode_config.active_frequency_indices);
      !preamble) {
    return std::unexpected(preamble.error());
  }
  if (auto iq =
          ValidateIqOffsets(config.decode_config.frequency,
                            config.decode_config.active_frequency_indices);
      !iq) {
    return std::unexpected(iq.error());
  }
  return {};
}

[[nodiscard]] inline std::vector<std::complex<double>>
DehopPulseSamples(const std::vector<std::complex<double>> &samples,
                  std::uint64_t local_start_sample,
                  std::uint64_t input_packet_global_start_sample,
                  std::uint64_t pulse_width_samples,
                  double iq_offset_frequency_hz, double sample_rate_hz) {
  std::vector<std::complex<double>> dehopped;
  dehopped.reserve(pulse_width_samples);
  for (std::uint64_t i = 0; i < pulse_width_samples; ++i) {
    const auto local_sample = local_start_sample + i;
    const auto global_sample = input_packet_global_start_sample + local_sample;
    const double phase = -2.0 * std::numbers::pi * iq_offset_frequency_hz *
                         static_cast<double>(global_sample) / sample_rate_hz;
    dehopped.push_back(samples[local_sample] *
                       std::exp(std::complex<double>(0.0, phase)));
  }
  return dehopped;
}

[[nodiscard]] inline double
PhaseCoherenceScore(const std::vector<std::complex<double>> &dehopped) {
  if (dehopped.size() < 2) {
    return 0.0;
  }

  double sum = 0.0;
  std::uint64_t count = 0;
  for (std::size_t i = 1; i < dehopped.size(); ++i) {
    const double prev_mag = std::abs(dehopped[i - 1]);
    const double curr_mag = std::abs(dehopped[i]);
    if (prev_mag <= std::numeric_limits<double>::epsilon() ||
        curr_mag <= std::numeric_limits<double>::epsilon()) {
      continue;
    }
    const auto product = dehopped[i] * std::conj(dehopped[i - 1]);
    sum += product.real() / (prev_mag * curr_mag);
    ++count;
  }

  if (count == 0) {
    return 0.0;
  }
  const double average = sum / static_cast<double>(count);
  return std::clamp((average + 1.0) / 2.0, 0.0, 1.0);
}

[[nodiscard]] inline double
AveragePowerDb(const std::vector<std::complex<double>> &samples) {
  if (samples.empty()) {
    return -std::numeric_limits<double>::infinity();
  }
  double power = 0.0;
  for (const auto &sample : samples) {
    power += std::norm(sample);
  }
  power /= static_cast<double>(samples.size());
  return 10.0 * std::log10(std::max(power, std::numeric_limits<double>::min()));
}

class FHSSCorrelatorBankDetectorNode {
public:
  [[nodiscard]] static FHSSResult<FHSSCorrelatorBankDetectionResult>
  Detect(const std::vector<std::complex<double>> &samples,
         const FHSSCorrelatorBankDetectorConfig &config) {
    if (auto validation = ValidateCorrelatorBankDetectorConfig(config);
        !validation) {
      return std::unexpected(validation.error());
    }

    const auto timing = DeriveTimingModel(config.decode_config.timing).value();
    const auto frequency_map =
        BuildFrequencyMap(config.decode_config.frequency);
    if (!frequency_map) {
      return std::unexpected(frequency_map.error());
    }

    FHSSCorrelatorBankDetectionResult result{};
    result.evaluated_frequency_count = static_cast<std::uint32_t>(
        config.decode_config.active_frequency_indices.size());

    const auto pulse_slots = samples.size() / timing.pulse_period_samples;
    result.local_detections.reserve(pulse_slots);
    result.slot_scores.reserve(pulse_slots);

    for (std::uint64_t slot = 0; slot < pulse_slots; ++slot) {
      const auto local_start_sample =
          config.message_start_sample + slot * timing.pulse_period_samples;
      if (local_start_sample + timing.pulse_width_samples > samples.size()) {
        break;
      }

      std::vector<FHSSFrequencyScore> slot_scores;
      slot_scores.reserve(config.decode_config.active_frequency_indices.size());
      std::vector<std::complex<double>> best_dehopped;
      std::uint32_t best_frequency_index = 0;
      double best_score = -1.0;

      for (const auto frequency_index :
           config.decode_config.active_frequency_indices) {
        const auto &entry = (*frequency_map)[frequency_index];
        auto dehopped = DehopPulseSamples(
            samples, local_start_sample,
            config.input_packet_global_start_sample, timing.pulse_width_samples,
            entry.iq_offset_frequency_hz,
            config.decode_config.timing.sample_rate_hz);
        const double score = PhaseCoherenceScore(dehopped);
        slot_scores.push_back(FHSSFrequencyScore{
            .frequency_index = frequency_index,
            .iq_offset_frequency_hz = entry.iq_offset_frequency_hz,
            .score = score});
        if (score > best_score) {
          best_score = score;
          best_frequency_index = frequency_index;
          best_dehopped = std::move(dehopped);
        }
      }

      const auto &best_entry = (*frequency_map)[best_frequency_index];
      auto evidence = std::make_shared<const std::vector<std::complex<double>>>(
          best_dehopped);
      const double power_db = AveragePowerDb(*evidence);

      FHSSLocalPulseDetection detection{};
      detection.sample_time_map.input_packet_global_start_sample =
          config.input_packet_global_start_sample;
      detection.sample_time_map.sample_rate_hz =
          config.decode_config.timing.sample_rate_hz;
      detection.local_start_offset = local_start_sample;
      detection.duration_samples = timing.pulse_width_samples;
      detection.channel_id = best_frequency_index;
      detection.frequency_index = best_frequency_index;
      detection.rf_frequency_hz = best_entry.rf_frequency_hz;
      detection.iq_offset_frequency_hz = best_entry.iq_offset_frequency_hz;
      detection.estimated_center_frequency_hz =
          best_entry.iq_offset_frequency_hz;
      detection.frequency_error_hz = 0.0;
      detection.amplitude = 1.0;
      detection.power_db = power_db;
      detection.snr_db = 100.0 * best_score;
      detection.noise_floor_db = 0.0;
      detection.phase_at_start_rad =
          evidence->empty() ? 0.0 : std::arg((*evidence)[0]);
      detection.phase_slope_rad_per_sample = 0.0;
      detection.cfo_hz = 0.0;
      detection.bandwidth_hz =
          config.decode_config.frequency.occupied_bandwidth_hz;
      detection.confidence = best_score;
      detection.detector_id = config.detector_id;
      detection.packet_sequence = config.packet_sequence;
      detection.complex_evidence =
          FHSSComplexEvidence{.samples = evidence,
                              .sample_offset = 0,
                              .sample_count = timing.pulse_width_samples};

      result.local_detections.push_back(std::move(detection));
      result.slot_scores.push_back(std::move(slot_scores));
    }

    return result;
  }
};

} // namespace dsp::fhss
