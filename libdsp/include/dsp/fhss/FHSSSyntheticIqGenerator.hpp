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

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

namespace dsp::fhss {

struct FHSSTruthPulse {
  std::uint64_t global_start_sample = 0;
  std::uint64_t duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  std::uint32_t value = 0;
  bool is_preamble = false;
  std::uint64_t message_id = 0;
};

struct FHSSSyntheticIqGeneratorConfig {
  FHSSDecodeConfig decode_config{};
  std::vector<FHSSScheduledMessageSpec> messages;
  std::uint64_t idle_duration_samples = 0;
  bool enable_noise = false;
  bool enable_doppler = false;
  bool enable_multipath = false;
  bool allow_overlap = false;
};

struct FHSSSyntheticIqFixture {
  std::vector<std::complex<double>> samples;
  std::vector<FHSSTruthPulse> truth_pulses;
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

inline void EnsureSampleSize(std::vector<std::complex<double>> &samples,
                             std::uint64_t size) {
  if (samples.size() < size) {
    samples.resize(static_cast<std::size_t>(size),
                   std::complex<double>{0.0, 0.0});
  }
}

[[nodiscard]] inline FHSSVoidResult ValidateScheduledMessage(
    const FHSSScheduledMessageSpec &message,
    const FHSSDecodeConfig &decode_config) {
  if (message.pulses.size() < FHSSProtocolConstants::kPreamblePulseCount) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidPreambleLength,
        "scheduled FHSS message must include a 16-pulse preamble"));
  }
  if (auto length = ValidateMessageLength(
          FHSSProtocolConstants::kPreamblePulseCount,
          message.pulses.size() - FHSSProtocolConstants::kPreamblePulseCount);
      !length) {
    return length;
  }

  for (std::size_t i = 0; i < message.pulses.size(); ++i) {
    const auto &pulse = message.pulses[i];
    if (i < FHSSProtocolConstants::kPreamblePulseCount) {
      if (pulse.role != FHSSMessagePulseRole::Preamble) {
        return std::unexpected(MakeError(
            FHSSValidationCode::InvalidPreambleFrequency,
            "first 16 scheduled message pulses must be marked preamble"));
      }
    } else if (pulse.role != FHSSMessagePulseRole::Body) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidPayloadFrequency,
          "scheduled message body pulses must be marked body"));
    }
  }

  const auto preamble = PreamblePatternFromMessage(message);
  if (auto validation =
          ValidatePreamblePattern(preamble,
                                  decode_config.active_frequency_indices);
      !validation) {
    return validation;
  }
  if (auto words = ValidatePreambleWordConsistency(preamble); !words) {
    return words;
  }

  std::vector<std::uint32_t> body_frequency_indices;
  body_frequency_indices.reserve(
      message.pulses.size() - FHSSProtocolConstants::kPreamblePulseCount);
  for (std::size_t i = FHSSProtocolConstants::kPreamblePulseCount;
       i < message.pulses.size(); ++i) {
    body_frequency_indices.push_back(message.pulses[i].frequency_index);
  }
  if (auto payload =
          ValidatePayloadFrequencies(body_frequency_indices,
                                     decode_config.active_frequency_indices);
      !payload) {
    return payload;
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult ValidateMessageSchedule(
    const std::vector<FHSSScheduledMessageSpec> &messages,
    const FHSSDecodeConfig &decode_config, const FHSSTimingModel &timing,
    bool allow_overlap) {
  if (allow_overlap && messages.size() > 1u) {
    return std::unexpected(MakeError(
        FHSSValidationCode::UnsupportedOverlap,
        "FHSS PR10 scheduled messages do not support overlap"));
  }

  std::uint64_t previous_end_sample = 0;
  bool have_previous = false;
  for (const auto &message : messages) {
    if (auto validation = ValidateScheduledMessage(message, decode_config);
        !validation) {
      return validation;
    }

    const auto message_duration =
        static_cast<std::uint64_t>(message.pulses.size()) *
        timing.pulse_period_samples;
    const auto message_end_sample =
        message.transmit_start_sample + message_duration;
    if (message_end_sample < message.transmit_start_sample) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidGlobalTiming,
                    "scheduled FHSS message sample range overflowed"));
    }
    if (have_previous && message.transmit_start_sample < previous_end_sample) {
      return std::unexpected(MakeError(
          FHSSValidationCode::UnsupportedOverlap,
          "scheduled FHSS messages overlap, which is unsupported in PR10"));
    }
    previous_end_sample = message_end_sample;
    have_previous = true;
  }

  return {};
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

  const auto timing = DeriveTimingModel(config.decode_config.timing).value();
  if (auto schedule = ValidateMessageSchedule(
          config.messages, config.decode_config, timing, config.allow_overlap);
      !schedule) {
    return std::unexpected(schedule.error());
  }

  const auto frequency_map = BuildFrequencyMap(config.decode_config.frequency);
  if (!frequency_map) {
    return std::unexpected(frequency_map.error());
  }

  std::size_t total_pulses = 0;
  std::uint64_t total_samples = config.idle_duration_samples;
  for (const auto &message : config.messages) {
    total_pulses += message.pulses.size();
    const auto message_end_sample =
        message.transmit_start_sample +
        static_cast<std::uint64_t>(message.pulses.size()) *
            timing.pulse_period_samples;
    total_samples = std::max(total_samples, message_end_sample);
  }

  FHSSSyntheticIqFixture fixture{};
  fixture.timing = timing;
  fixture.samples.reserve(static_cast<std::size_t>(total_samples));
  fixture.truth_pulses.reserve(total_pulses);
  EnsureSampleSize(fixture.samples, total_samples);

  auto append_pulse = [&](std::uint64_t global_start_sample,
                          std::uint32_t frequency_index, std::uint32_t value,
                          bool is_preamble, std::uint64_t message_id) {
    const auto &entry = (*frequency_map)[frequency_index];
    fixture.truth_pulses.push_back(
        FHSSTruthPulse{.global_start_sample = global_start_sample,
                       .duration_samples = timing.pulse_width_samples,
                       .frequency_index = frequency_index,
                       .rf_frequency_hz = entry.rf_frequency_hz,
                       .iq_offset_frequency_hz = entry.iq_offset_frequency_hz,
                       .value = value,
                       .is_preamble = is_preamble,
                       .message_id = message_id});

    const auto previous_size = fixture.samples.size();
    fixture.samples.resize(static_cast<std::size_t>(global_start_sample));
    AppendCpsmPulseSamples(fixture.samples, global_start_sample, value,
                           entry.iq_offset_frequency_hz,
                           config.decode_config.timing, timing);
    EnsureSampleSize(fixture.samples,
                     global_start_sample + timing.pulse_period_samples);
    if (fixture.samples.size() < previous_size) {
      fixture.samples.resize(previous_size);
    }
  };

  for (const auto &message : config.messages) {
    for (std::size_t i = 0; i < message.pulses.size(); ++i) {
      const auto &pulse = message.pulses[i];
      const auto global_start_sample =
          message.transmit_start_sample +
          static_cast<std::uint64_t>(i) * timing.pulse_period_samples;
      append_pulse(global_start_sample, pulse.frequency_index, pulse.value,
                   pulse.role == FHSSMessagePulseRole::Preamble,
                   message.message_id);
    }
  }

  return fixture;
}

} // namespace dsp::fhss
