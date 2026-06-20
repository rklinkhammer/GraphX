/**
 * @file FHSSSyntheticIqGenerator.hpp
 * @brief Deterministic FHSS CPSM synthetic IQ fixture generator.
 *
 * @details PR2 CPU-only fixture generator. This helper emits complex IQ and
 * exact truth metadata for tests. It does not implement graph runtime nodes,
 * detector logic, decoder logic, real RF capture, channelization, or production
 * RF behavior.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSProtocol.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

namespace dsp::fhss {

struct FHSSSyntheticIqGeneratorConfig {
  FHSSDecodeConfig decode_config{};
  std::vector<std::uint32_t> payload_values;
  bool enable_noise = false;
  bool enable_doppler = false;
  bool enable_multipath = false;
  bool allow_overlap = false;
};

struct FHSSSyntheticIqFixture {
  std::vector<std::complex<double>> samples;
  std::vector<FHSSTruthPulse> truth_pulses;
  std::vector<std::uint32_t> payload_frequency_indices;
  FHSSTimingModel timing{};
};

[[nodiscard]] inline FHSSVoidResult
ValidateGeneratorFeatureFlags(const FHSSSyntheticIqGeneratorConfig &config) {
  if (config.enable_noise || config.enable_doppler || config.enable_multipath ||
      config.allow_overlap) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR2 fixture keeps noise, Doppler, multipath, and overlap "
        "disabled"));
  }
  return {};
}

[[nodiscard]] inline double CpsmSymbolForBit(std::uint32_t value,
                                             std::uint32_t bit_index) {
  const auto bit = (value >> (31u - bit_index)) & 0x1u;
  return bit == 0u ? 1.0 : -1.0;
}

[[nodiscard]] inline double
RectangularFullResponsePhasePulse(std::uint32_t sample_in_symbol,
                                  std::uint32_t samples_per_symbol) {
  return 0.5 * static_cast<double>(sample_in_symbol) /
         static_cast<double>(samples_per_symbol);
}

[[nodiscard]] inline std::vector<std::uint32_t>
GenerateDeterministicPayloadFrequencyIndices(
    const std::vector<std::uint32_t> &active_frequency_indices,
    std::size_t payload_count, std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> distribution(
      0, active_frequency_indices.size() - 1u);

  std::vector<std::uint32_t> indices;
  indices.reserve(payload_count);
  for (std::size_t i = 0; i < payload_count; ++i) {
    indices.push_back(active_frequency_indices[distribution(rng)]);
  }
  return indices;
}

inline void AppendCpsmPulseSamples(std::vector<std::complex<double>> &samples,
                                   std::uint64_t global_start_sample,
                                   std::uint32_t value,
                                   double iq_offset_frequency_hz,
                                   const FHSSTimingConfig &timing_config,
                                   const FHSSTimingModel &timing) {
  constexpr double kAmplitude = 1.0;
  constexpr double kModulationIndex = 0.5;
  constexpr double kInitialPhaseRad = 0.0;
  double completed_phase_pulse_sum = 0.0;

  for (std::uint32_t symbol_index = 0;
       symbol_index < FHSSProtocolConstants::kBitsPerPulse; ++symbol_index) {
    const double symbol = CpsmSymbolForBit(value, symbol_index);
    for (std::uint32_t sample_in_symbol = 0;
         sample_in_symbol < timing.samples_per_symbol; ++sample_in_symbol) {
      const auto local_sample =
          static_cast<std::uint64_t>(symbol_index) * timing.samples_per_symbol +
          sample_in_symbol;
      const auto global_sample = global_start_sample + local_sample;
      const double q = RectangularFullResponsePhasePulse(
          sample_in_symbol, timing.samples_per_symbol);
      const double theta = 2.0 * std::numbers::pi * kModulationIndex *
                               (completed_phase_pulse_sum + symbol * q) +
                           kInitialPhaseRad;
      const double hop_phase = 2.0 * std::numbers::pi * iq_offset_frequency_hz *
                               static_cast<double>(global_sample) /
                               timing_config.sample_rate_hz;

      samples.push_back(kAmplitude *
                        std::exp(std::complex<double>(0.0, theta + hop_phase)));
    }
    completed_phase_pulse_sum += 0.5 * symbol;
  }
}

[[nodiscard]] inline FHSSResult<FHSSSyntheticIqFixture>
GenerateSyntheticIqFixture(const FHSSSyntheticIqGeneratorConfig &config) {
  if (auto flags = ValidateGeneratorFeatureFlags(config); !flags) {
    return std::unexpected(flags.error());
  }
  if (auto active = ValidateActiveFrequencySet(
          config.decode_config.active_frequency_indices);
      !active) {
    return std::unexpected(active.error());
  }
  if (auto timing = DeriveTimingModel(config.decode_config.timing); !timing) {
    return std::unexpected(timing.error());
  }
  if (auto frequency = ValidateFrequencyConfig(config.decode_config.frequency);
      !frequency) {
    return std::unexpected(frequency.error());
  }
  if (auto iq =
          ValidateIqOffsets(config.decode_config.frequency,
                            config.decode_config.active_frequency_indices);
      !iq) {
    return std::unexpected(iq.error());
  }
  if (auto preamble = ValidatePreamblePattern(
          config.decode_config.preamble_pulses,
          config.decode_config.active_frequency_indices);
      !preamble) {
    return std::unexpected(preamble.error());
  }
  if (auto words =
          ValidatePreambleWordConsistency(config.decode_config.preamble_pulses);
      !words) {
    return std::unexpected(words.error());
  }
  if (auto length =
          ValidateMessageLength(config.decode_config.preamble_pulses.size(),
                                config.payload_values.size());
      !length) {
    return std::unexpected(length.error());
  }
  if (!config.decode_config.payload_random.deterministic) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidPayloadFrequency,
        "FHSS PR2 payload/body frequency selection must be deterministic"));
  }

  const auto timing = DeriveTimingModel(config.decode_config.timing).value();
  auto payload_frequency_indices = GenerateDeterministicPayloadFrequencyIndices(
      config.decode_config.active_frequency_indices,
      config.payload_values.size(),
      config.decode_config.payload_random.rng_seed);

  if (auto payload = ValidatePayloadFrequencies(
          payload_frequency_indices,
          config.decode_config.active_frequency_indices);
      !payload) {
    return std::unexpected(payload.error());
  }

  const auto frequency_map = BuildFrequencyMap(config.decode_config.frequency);
  if (!frequency_map) {
    return std::unexpected(frequency_map.error());
  }

  const auto total_pulses = config.decode_config.preamble_pulses.size() +
                            config.payload_values.size();
  FHSSSyntheticIqFixture fixture{};
  fixture.timing = timing;
  fixture.payload_frequency_indices = payload_frequency_indices;
  fixture.samples.reserve(total_pulses * timing.pulse_period_samples);
  fixture.truth_pulses.reserve(total_pulses);

  auto append_pulse = [&](std::uint64_t pulse_index,
                          std::uint32_t frequency_index, std::uint32_t value,
                          bool is_preamble) {
    const auto &entry = (*frequency_map)[frequency_index];
    const auto global_start_sample = pulse_index * timing.pulse_period_samples;
    fixture.truth_pulses.push_back(
        FHSSTruthPulse{.global_start_sample = global_start_sample,
                       .duration_samples = timing.pulse_width_samples,
                       .frequency_index = frequency_index,
                       .rf_frequency_hz = entry.rf_frequency_hz,
                       .iq_offset_frequency_hz = entry.iq_offset_frequency_hz,
                       .value = value,
                       .is_preamble = is_preamble});

    AppendCpsmPulseSamples(fixture.samples, global_start_sample, value,
                           entry.iq_offset_frequency_hz,
                           config.decode_config.timing, timing);
    fixture.samples.insert(fixture.samples.end(), timing.pulse_gap_samples,
                           std::complex<double>{0.0, 0.0});
  };

  std::uint64_t pulse_index = 0;
  for (const auto &pulse : config.decode_config.preamble_pulses) {
    append_pulse(pulse_index, pulse.frequency_index, pulse.word_value, true);
    ++pulse_index;
  }
  for (std::size_t i = 0; i < config.payload_values.size(); ++i) {
    append_pulse(pulse_index, payload_frequency_indices[i],
                 config.payload_values[i], false);
    ++pulse_index;
  }

  return fixture;
}

} // namespace dsp::fhss
