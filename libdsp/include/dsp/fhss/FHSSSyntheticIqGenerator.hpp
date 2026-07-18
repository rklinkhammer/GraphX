/**
 * @file FHSSSyntheticIqGenerator.hpp
 * @brief FHSS CPSM synthetic IQ fixture and realistic receive generator.
 *
 * @details CPU-only generator. This helper emits complex IQ and exact truth
 * metadata for tests and scenario runs. The default mode is deterministic. The
 * optional realistic overlay supports overlapping schedules, missing pulses,
 * stationary receivers, moving transmitter paths, propagation delay, path loss,
 * and Doppler. It does not implement graph runtime nodes, detector logic,
 * decoder logic, real RF capture, channelization, noise, multipath, or
 * production RF behavior.
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
#include <optional>
#include <random>
#include <vector>

namespace dsp::fhss {

struct FHSSVector3Meters {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct FHSSMotionWaypoint {
  double time_seconds = 0.0;
  FHSSVector3Meters position_m{};
};

struct FHSSRealisticTransmitterPath {
  std::uint64_t message_id = 0;
  std::vector<FHSSMotionWaypoint> waypoints;
};

struct FHSSStationaryReceiverConfig {
  FHSSVector3Meters position_m{};
};

struct FHSSRealisticIqConfig {
  bool enabled = false;
  FHSSStationaryReceiverConfig receiver{};
  std::vector<FHSSRealisticTransmitterPath> transmitter_paths;
  std::uint64_t rng_seed = 0;
  double missing_pulse_probability = 0.0;
  double timing_jitter_stddev_samples = 0.0;
  bool apply_propagation_delay = true;
  bool apply_path_loss = true;
  bool apply_doppler = true;
  double reference_range_m = 1'000.0;
  double minimum_range_m = 1.0;
};

struct FHSSTruthPulse {
  std::uint64_t global_start_sample = 0;
  std::uint64_t nominal_global_start_sample = 0;
  std::uint64_t duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  double doppler_hz = 0.0;
  double propagation_delay_seconds = 0.0;
  double range_m = 0.0;
  double amplitude = 1.0;
  bool dropped = false;
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
  FHSSRealisticIqConfig realistic{};
};

struct FHSSSyntheticIqFixture {
  std::vector<std::complex<double>> samples;
  std::vector<FHSSTruthPulse> truth_pulses;
  FHSSTimingModel timing{};
};

[[nodiscard]] inline FHSSVoidResult
ValidateGeneratorFeatureFlags(const FHSSSyntheticIqGeneratorConfig &config) {
  if (config.enable_noise || config.enable_multipath) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS synthetic IQ generator does not yet support noise or multipath"));
  }
  if (config.enable_doppler && !config.realistic.enabled) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidTiming,
                  "FHSS Doppler generation requires realistic.enabled = true"));
  }
  return {};
}

[[nodiscard]] inline double ClampProbability(double probability) {
  return std::clamp(probability, 0.0, 1.0);
}

[[nodiscard]] inline double DistanceMeters(const FHSSVector3Meters &lhs,
                                           const FHSSVector3Meters &rhs) {
  const double dx = lhs.x - rhs.x;
  const double dy = lhs.y - rhs.y;
  const double dz = lhs.z - rhs.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

[[nodiscard]] inline FHSSVector3Meters
InterpolatePosition(const FHSSMotionWaypoint &lhs,
                    const FHSSMotionWaypoint &rhs, double time_seconds) {
  const double span = rhs.time_seconds - lhs.time_seconds;
  const double alpha =
      span <= 0.0
          ? 0.0
          : std::clamp((time_seconds - lhs.time_seconds) / span, 0.0, 1.0);
  return FHSSVector3Meters{
      .x = lhs.position_m.x + alpha * (rhs.position_m.x - lhs.position_m.x),
      .y = lhs.position_m.y + alpha * (rhs.position_m.y - lhs.position_m.y),
      .z = lhs.position_m.z + alpha * (rhs.position_m.z - lhs.position_m.z)};
}

[[nodiscard]] inline std::optional<FHSSRealisticTransmitterPath>
FindTransmitterPath(const FHSSRealisticIqConfig &config,
                    std::uint64_t message_id) {
  for (const auto &path : config.transmitter_paths) {
    if (path.message_id == message_id) {
      return path;
    }
  }
  return std::nullopt;
}

[[nodiscard]] inline FHSSVector3Meters
TransmitterPositionAt(const FHSSRealisticTransmitterPath &path,
                      double time_seconds) {
  if (path.waypoints.empty()) {
    return {};
  }
  if (path.waypoints.size() == 1 ||
      time_seconds <= path.waypoints.front().time_seconds) {
    return path.waypoints.front().position_m;
  }
  for (std::size_t i = 1; i < path.waypoints.size(); ++i) {
    if (time_seconds <= path.waypoints[i].time_seconds) {
      return InterpolatePosition(path.waypoints[i - 1], path.waypoints[i],
                                 time_seconds);
    }
  }
  return path.waypoints.back().position_m;
}

[[nodiscard]] inline double
RangeRateMetersPerSecond(const FHSSRealisticTransmitterPath &path,
                         const FHSSStationaryReceiverConfig &receiver,
                         double time_seconds) {
  if (path.waypoints.size() < 2) {
    return 0.0;
  }
  constexpr double kDeltaSeconds = 1.0e-4;
  const auto before =
      TransmitterPositionAt(path, std::max(0.0, time_seconds - kDeltaSeconds));
  const auto after = TransmitterPositionAt(path, time_seconds + kDeltaSeconds);
  const double before_range = DistanceMeters(before, receiver.position_m);
  const double after_range = DistanceMeters(after, receiver.position_m);
  return (after_range - before_range) / (2.0 * kDeltaSeconds);
}

[[nodiscard]] inline FHSSVoidResult
ValidateRealisticIqConfig(const FHSSRealisticIqConfig &config) {
  if (!config.enabled) {
    return {};
  }
  if (config.missing_pulse_probability < 0.0 ||
      config.missing_pulse_probability > 1.0 ||
      config.timing_jitter_stddev_samples < 0.0 ||
      config.reference_range_m <= 0.0 || config.minimum_range_m <= 0.0) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS realistic IQ parameters must use valid probabilities, jitter, "
        "and ranges"));
  }
  for (const auto &path : config.transmitter_paths) {
    double previous_time = -1.0;
    for (const auto &waypoint : path.waypoints) {
      if (!std::isfinite(waypoint.time_seconds) ||
          waypoint.time_seconds < 0.0 ||
          waypoint.time_seconds < previous_time) {
        return std::unexpected(MakeError(
            FHSSValidationCode::InvalidTiming,
            "FHSS transmitter path waypoints must be sorted by non-negative "
            "time"));
      }
      previous_time = waypoint.time_seconds;
    }
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

inline void EnsureSampleSize(std::vector<std::complex<double>> &samples,
                             std::uint64_t size) {
  if (samples.size() < size) {
    samples.resize(static_cast<std::size_t>(size),
                   std::complex<double>{0.0, 0.0});
  }
}

inline void AppendCpsmPulseSamples(std::vector<std::complex<double>> &samples,
                                   std::uint64_t global_start_sample,
                                   std::uint32_t value,
                                   double iq_offset_frequency_hz,
                                   const FHSSTimingConfig &timing_config,
                                   const FHSSTimingModel &timing,
                                   double amplitude = 1.0,
                                   double doppler_hz = 0.0) {
  constexpr double kModulationIndex = 0.5;
  constexpr double kInitialPhaseRad = 0.0;
  double completed_phase_pulse_sum = 0.0;
  const double effective_iq_offset_hz = iq_offset_frequency_hz + doppler_hz;
  EnsureSampleSize(samples, global_start_sample + timing.pulse_width_samples);

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
      const double hop_phase = 2.0 * std::numbers::pi * effective_iq_offset_hz *
                               static_cast<double>(global_sample) /
                               timing_config.sample_rate_hz;

      samples[static_cast<std::size_t>(global_sample)] +=
          amplitude * std::exp(std::complex<double>(0.0, theta + hop_phase));
    }
    completed_phase_pulse_sum += 0.5 * symbol;
  }
}

[[nodiscard]] inline FHSSVoidResult
ValidateScheduledMessage(const FHSSScheduledMessageSpec &message,
                         const FHSSDecodeConfig &decode_config) {
  if (message.pulses.size() < FHSSProtocolConstants::kPreamblePulseCount) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidPreambleLength,
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
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidPayloadFrequency,
                    "scheduled message body pulses must be marked body"));
    }
  }

  const auto preamble = PreamblePatternFromMessage(message);
  if (auto validation = ValidatePreamblePattern(
          preamble, decode_config.active_frequency_indices);
      !validation) {
    return validation;
  }
  if (auto words = ValidatePreambleWordConsistency(preamble); !words) {
    return words;
  }

  std::vector<std::uint32_t> body_frequency_indices;
  body_frequency_indices.reserve(message.pulses.size() -
                                 FHSSProtocolConstants::kPreamblePulseCount);
  for (std::size_t i = FHSSProtocolConstants::kPreamblePulseCount;
       i < message.pulses.size(); ++i) {
    body_frequency_indices.push_back(message.pulses[i].frequency_index);
  }
  if (auto payload = ValidatePayloadFrequencies(
          body_frequency_indices, decode_config.active_frequency_indices);
      !payload) {
    return payload;
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult
ValidateMessageSchedule(const std::vector<FHSSScheduledMessageSpec> &messages,
                        const FHSSDecodeConfig &decode_config,
                        const FHSSTimingModel &timing, bool allow_overlap) {
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
      if (!allow_overlap) {
        return std::unexpected(MakeError(
            FHSSValidationCode::UnsupportedOverlap,
            "scheduled FHSS messages overlap; set allow_overlap to true for "
            "realistic generator scenarios"));
      }
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
  if (auto realistic = ValidateRealisticIqConfig(config.realistic);
      !realistic) {
    return std::unexpected(realistic.error());
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

  std::mt19937_64 rng(config.realistic.rng_seed);
  std::uniform_real_distribution<double> uniform01(0.0, 1.0);
  std::normal_distribution<double> jitter_samples(
      0.0, config.realistic.timing_jitter_stddev_samples);

  auto append_pulse = [&](std::uint64_t nominal_global_start_sample,
                          std::uint32_t frequency_index, std::uint32_t value,
                          bool is_preamble, std::uint64_t message_id) {
    const auto &entry = (*frequency_map)[frequency_index];
    std::uint64_t global_start_sample = nominal_global_start_sample;
    double doppler_hz = 0.0;
    double propagation_delay_seconds = 0.0;
    double range_m = 0.0;
    double amplitude = 1.0;
    bool dropped = false;

    if (config.realistic.enabled) {
      dropped = uniform01(rng) <
                ClampProbability(config.realistic.missing_pulse_probability);
      const double nominal_time_seconds =
          static_cast<double>(nominal_global_start_sample) /
          config.decode_config.timing.sample_rate_hz;
      if (auto path = FindTransmitterPath(config.realistic, message_id);
          path.has_value()) {
        const auto position =
            TransmitterPositionAt(*path, nominal_time_seconds);
        range_m =
            DistanceMeters(position, config.realistic.receiver.position_m);
        const double guarded_range =
            std::max(range_m, config.realistic.minimum_range_m);
        if (config.realistic.apply_propagation_delay) {
          constexpr double kSpeedOfLightMetersPerSecond = 299'792'458.0;
          propagation_delay_seconds =
              guarded_range / kSpeedOfLightMetersPerSecond;
          const auto delay_samples = static_cast<std::int64_t>(
              std::llround(propagation_delay_seconds *
                           config.decode_config.timing.sample_rate_hz));
          global_start_sample += static_cast<std::uint64_t>(
              std::max<std::int64_t>(0, delay_samples));
        }
        if (config.realistic.apply_path_loss) {
          amplitude = config.realistic.reference_range_m / guarded_range;
        }
        if (config.realistic.apply_doppler || config.enable_doppler) {
          constexpr double kSpeedOfLightMetersPerSecond = 299'792'458.0;
          const double range_rate_mps = RangeRateMetersPerSecond(
              *path, config.realistic.receiver, nominal_time_seconds);
          doppler_hz = -(range_rate_mps / kSpeedOfLightMetersPerSecond) *
                       entry.rf_frequency_hz;
        }
      }
      if (config.realistic.timing_jitter_stddev_samples > 0.0) {
        const auto jitter =
            static_cast<std::int64_t>(std::llround(jitter_samples(rng)));
        if (jitter < 0) {
          const auto magnitude = static_cast<std::uint64_t>(-jitter);
          global_start_sample = magnitude > global_start_sample
                                    ? 0
                                    : global_start_sample - magnitude;
        } else {
          global_start_sample += static_cast<std::uint64_t>(jitter);
        }
      }
    }

    fixture.truth_pulses.push_back(FHSSTruthPulse{
        .global_start_sample = global_start_sample,
        .nominal_global_start_sample = nominal_global_start_sample,
        .duration_samples = timing.pulse_width_samples,
        .frequency_index = frequency_index,
        .rf_frequency_hz = entry.rf_frequency_hz,
        .iq_offset_frequency_hz = entry.iq_offset_frequency_hz,
        .doppler_hz = doppler_hz,
        .propagation_delay_seconds = propagation_delay_seconds,
        .range_m = range_m,
        .amplitude = amplitude,
        .dropped = dropped,
        .value = value,
        .is_preamble = is_preamble,
        .message_id = message_id});
    if (dropped) {
      return;
    }

    AppendCpsmPulseSamples(fixture.samples, global_start_sample, value,
                           entry.iq_offset_frequency_hz,
                           config.decode_config.timing, timing, amplitude,
                           doppler_hz);
    EnsureSampleSize(fixture.samples,
                     global_start_sample + timing.pulse_period_samples);
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
