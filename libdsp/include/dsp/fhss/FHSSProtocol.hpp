/**
 * @file FHSSProtocol.hpp
 * @brief FHSS deterministic fixture protocol contracts.
 *
 * @details Defines PR1-only protocol types and validation helpers for the
 * FHSS CPSM decoder roadmap. This file intentionally contains no signal
 * generation, detection, decoding, graph runtime, plugin, or RF capture logic.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

struct FHSSProtocolConstants {
  static constexpr std::uint32_t kFrequencyCount = 64;
  static constexpr std::uint32_t kFirstFrequencyIndex = 0;
  static constexpr std::uint32_t kLastFrequencyIndex = 63;
  static constexpr std::uint32_t kFirstSelectableFrequencyIndex = 1;
  static constexpr std::uint32_t kLastSelectableFrequencyIndex = 62;
  static constexpr std::uint32_t kActiveFrequencyCount = 4;
  static constexpr std::uint32_t kPreamblePulseCount = 16;
  static constexpr std::uint32_t kMaxMessagePulseCount = 256;

  static constexpr double kBaseFrequencyHz = 1'000'000'000.0;
  static constexpr double kFrequencySpacingHz = 8'000'000.0;
  static constexpr double kSampleRateHz = 500'000'000.0;
  static constexpr double kBitRateHz = 5'000'000.0;
  static constexpr std::uint32_t kBitsPerPulse = 32;
  static constexpr std::uint32_t kSamplesPerSymbol = 100;
  static constexpr std::uint64_t kPulseWidthSamples = 3200;
  static constexpr std::uint64_t kPulseGapSamples = 3300;
  static constexpr std::uint64_t kPulsePeriodSamples = 6500;
  static constexpr double kPulseGapSeconds = 6.6e-6;
};

enum class FHSSValidationCode {
  Ok,
  InvalidFrequencyCount,
  InvalidFrequencyIndex,
  ReservedFrequencyIndex,
  InvalidFrequencyTable,
  InvalidActiveFrequencySet,
  InvalidPreambleLength,
  InvalidPreambleFrequency,
  InvalidPayloadFrequency,
  InvalidMessageLength,
  InvalidTiming,
  InvalidGlobalTiming,
  InvalidIqOffset,
  DuplicateIqOffset,
  PreambleWordMismatch,
  DuplicatePulse,
  UnsupportedOverlap
};

struct FHSSValidationError {
  FHSSValidationCode code = FHSSValidationCode::Ok;
  std::string message;
};

template <typename T> using FHSSResult = std::expected<T, FHSSValidationError>;

using FHSSVoidResult = std::expected<void, FHSSValidationError>;

struct FHSSTimingConfig {
  double sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  double bit_rate_hz = FHSSProtocolConstants::kBitRateHz;
  std::uint32_t bits_per_pulse = FHSSProtocolConstants::kBitsPerPulse;
  double pulse_gap_seconds = FHSSProtocolConstants::kPulseGapSeconds;
};

struct FHSSTimingModel {
  std::uint32_t samples_per_symbol = FHSSProtocolConstants::kSamplesPerSymbol;
  std::uint64_t pulse_width_samples = FHSSProtocolConstants::kPulseWidthSamples;
  std::uint64_t pulse_gap_samples = FHSSProtocolConstants::kPulseGapSamples;
  std::uint64_t pulse_period_samples =
      FHSSProtocolConstants::kPulsePeriodSamples;
};

struct FHSSFrequencyMapEntry {
  std::uint32_t index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
};

struct FHSSFrequencyConfig {
  std::uint32_t frequency_count = FHSSProtocolConstants::kFrequencyCount;
  double base_frequency_hz = FHSSProtocolConstants::kBaseFrequencyHz;
  double frequency_spacing_hz = FHSSProtocolConstants::kFrequencySpacingHz;
  double sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  double occupied_bandwidth_hz = 0.0;
  double max_abs_cfo_hz = 0.0;
  std::array<double, FHSSProtocolConstants::kFrequencyCount>
      iq_offset_frequency_hz{};
};

struct FHSSPayloadRandomConfig {
  std::uint64_t rng_seed = 0;
  bool deterministic = true;
};

struct FHSSPreamblePulseSpec {
  std::uint32_t frequency_index = 0;
  std::uint32_t word_value = 0;
};

struct FHSSTruthPulse {
  std::uint64_t global_start_sample = 0;
  std::uint64_t duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  std::uint32_t value = 0;
  bool is_preamble = false;
};

struct FHSSDetectedPulse {
  std::uint64_t global_start_sample = 0;
  std::uint64_t global_end_sample = 0;
  std::uint64_t duration_samples = 0;
  std::uint64_t channel_start_sample = 0;
  std::uint32_t channel_id = 0;
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  double estimated_center_frequency_hz = 0.0;
  double frequency_error_hz = 0.0;
  double amplitude = 0.0;
  double power_db = 0.0;
  double snr_db = 0.0;
  double noise_floor_db = 0.0;
  double phase_at_start_rad = 0.0;
  double phase_slope_rad_per_sample = 0.0;
  double cfo_hz = 0.0;
  double bandwidth_hz = 0.0;
  double confidence = 0.0;
  std::uint64_t detector_id = 0;
  std::uint64_t packet_sequence = 0;
};

struct FHSSPulseCandidate {
  FHSSDetectedPulse detected_pulse{};
  std::optional<std::uint64_t> provisional_slot_index{};
  std::optional<std::uint64_t> final_slot_index{};
};

struct FHSSDecodedPulse {
  FHSSTruthPulse pulse_metadata{};
  std::uint32_t decoded_value = 0;
  double confidence = 0.0;
};

struct FHSSMessage {
  std::vector<FHSSDecodedPulse> pulses;
  bool preamble_lock = false;
};

struct FHSSDecodeConfig {
  FHSSTimingConfig timing{};
  FHSSFrequencyConfig frequency{};
  FHSSPayloadRandomConfig payload_random{};
  std::vector<std::uint32_t> active_frequency_indices;
  std::vector<FHSSPreamblePulseSpec> preamble_pulses;
  std::vector<std::uint32_t> payload_frequency_indices;
};

[[nodiscard]] inline FHSSValidationError MakeError(FHSSValidationCode code,
                                                   std::string message) {
  return FHSSValidationError{code, std::move(message)};
}

[[nodiscard]] inline bool NearlyEqual(double lhs, double rhs,
                                      double epsilon = 1.0e-6) {
  return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] inline bool IsFrequencyIndexInTable(std::uint32_t index) {
  return index <= FHSSProtocolConstants::kLastFrequencyIndex;
}

[[nodiscard]] inline bool IsReservedFrequencyIndex(std::uint32_t index) {
  return index == FHSSProtocolConstants::kFirstFrequencyIndex ||
         index == FHSSProtocolConstants::kLastFrequencyIndex;
}

[[nodiscard]] inline bool IsSelectableFrequencyIndex(std::uint32_t index) {
  return index >= FHSSProtocolConstants::kFirstSelectableFrequencyIndex &&
         index <= FHSSProtocolConstants::kLastSelectableFrequencyIndex;
}

[[nodiscard]] inline double
RfFrequencyHz(std::uint32_t index, const FHSSFrequencyConfig &config = {}) {
  return config.base_frequency_hz +
         static_cast<double>(index) * config.frequency_spacing_hz;
}

[[nodiscard]] inline FHSSVoidResult
ValidateFrequencyIndex(std::uint32_t index) {
  if (!IsFrequencyIndexInTable(index)) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidFrequencyIndex,
                  "frequency index is outside the 64-entry FHSS table"));
  }
  return {};
}

[[nodiscard]] inline FHSSVoidResult
ValidateSelectableFrequencyIndex(std::uint32_t index) {
  if (auto table_result = ValidateFrequencyIndex(index); !table_result) {
    return table_result;
  }
  if (IsReservedFrequencyIndex(index)) {
    return std::unexpected(
        MakeError(FHSSValidationCode::ReservedFrequencyIndex,
                  "frequency index 0 or 63 is reserved and not selectable"));
  }
  return {};
}

[[nodiscard]] inline FHSSResult<FHSSTimingModel>
DeriveTimingModel(const FHSSTimingConfig &config) {
  if (!NearlyEqual(config.sample_rate_hz,
                   FHSSProtocolConstants::kSampleRateHz) ||
      !NearlyEqual(config.bit_rate_hz, FHSSProtocolConstants::kBitRateHz) ||
      config.bits_per_pulse != FHSSProtocolConstants::kBitsPerPulse) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR1 timing must use 500 Msps, 5 Mbps, and 32 bits per pulse"));
  }

  const double samples_per_symbol_exact =
      config.sample_rate_hz / config.bit_rate_hz;
  const auto samples_per_symbol =
      static_cast<std::uint32_t>(std::llround(samples_per_symbol_exact));
  if (!NearlyEqual(samples_per_symbol_exact,
                   static_cast<double>(samples_per_symbol)) ||
      samples_per_symbol != FHSSProtocolConstants::kSamplesPerSymbol) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidTiming,
                                     "sample rate must divide bit rate into "
                                     "exactly 100 samples per symbol"));
  }

  const auto pulse_width_samples =
      static_cast<std::uint64_t>(config.bits_per_pulse) * samples_per_symbol;
  const auto pulse_gap_samples = static_cast<std::uint64_t>(
      std::llround(config.pulse_gap_seconds * config.sample_rate_hz));
  const auto pulse_period_samples = pulse_width_samples + pulse_gap_samples;

  if (pulse_width_samples != FHSSProtocolConstants::kPulseWidthSamples ||
      pulse_gap_samples != FHSSProtocolConstants::kPulseGapSamples ||
      pulse_period_samples != FHSSProtocolConstants::kPulsePeriodSamples) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidTiming,
                                     "FHSS PR1 timing must derive 3200 pulse, "
                                     "3300 gap, and 6500 period samples"));
  }

  return FHSSTimingModel{.samples_per_symbol = samples_per_symbol,
                         .pulse_width_samples = pulse_width_samples,
                         .pulse_gap_samples = pulse_gap_samples,
                         .pulse_period_samples = pulse_period_samples};
}

[[nodiscard]] inline FHSSVoidResult
ValidateFrequencyConfig(const FHSSFrequencyConfig &config) {
  if (config.frequency_count != FHSSProtocolConstants::kFrequencyCount) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidFrequencyCount,
                  "FHSS PR1 requires exactly 64 RF metadata frequencies"));
  }
  if (!NearlyEqual(config.base_frequency_hz,
                   FHSSProtocolConstants::kBaseFrequencyHz) ||
      !NearlyEqual(config.frequency_spacing_hz,
                   FHSSProtocolConstants::kFrequencySpacingHz)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidFrequencyTable,
        "FHSS PR1 frequency table must start at 1 GHz with 8 MHz spacing"));
  }
  if (!NearlyEqual(config.sample_rate_hz,
                   FHSSProtocolConstants::kSampleRateHz)) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidTiming,
                  "FHSS PR1 frequency validation assumes 500 Msps fixture IQ"));
  }
  if (config.occupied_bandwidth_hz < 0.0 || config.max_abs_cfo_hz < 0.0) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidIqOffset,
        "occupied bandwidth and max CFO guards must be non-negative"));
  }
  return {};
}

[[nodiscard]] inline FHSSResult<
    std::array<FHSSFrequencyMapEntry, FHSSProtocolConstants::kFrequencyCount>>
BuildFrequencyMap(const FHSSFrequencyConfig &config) {
  if (auto validation = ValidateFrequencyConfig(config); !validation) {
    return std::unexpected(validation.error());
  }

  std::array<FHSSFrequencyMapEntry, FHSSProtocolConstants::kFrequencyCount>
      map{};
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    map[index] = FHSSFrequencyMapEntry{
        .index = index,
        .rf_frequency_hz = RfFrequencyHz(index, config),
        .iq_offset_frequency_hz = config.iq_offset_frequency_hz[index]};
  }
  return map;
}

[[nodiscard]] inline FHSSVoidResult
ValidateIqOffsets(const FHSSFrequencyConfig &config,
                  const std::vector<std::uint32_t> &active_frequency_indices) {
  if (auto validation = ValidateFrequencyConfig(config); !validation) {
    return validation;
  }

  const double nyquist_hz = config.sample_rate_hz / 2.0;
  std::vector<double> normalized_offsets;
  normalized_offsets.reserve(active_frequency_indices.size());

  for (const auto index : active_frequency_indices) {
    if (auto index_validation = ValidateSelectableFrequencyIndex(index);
        !index_validation) {
      return index_validation;
    }

    const double offset_hz = config.iq_offset_frequency_hz[index];
    if (!std::isfinite(offset_hz)) {
      return std::unexpected(MakeError(FHSSValidationCode::InvalidIqOffset,
                                       "IQ offset frequency must be finite"));
    }

    const double guarded_edge = std::abs(offset_hz) +
                                (config.occupied_bandwidth_hz / 2.0) +
                                config.max_abs_cfo_hz;
    if (!(guarded_edge < nyquist_hz)) {
      return std::unexpected(MakeError(FHSSValidationCode::InvalidIqOffset,
                                       "IQ offset plus occupied-bandwidth/CFO "
                                       "guard must remain inside Nyquist"));
    }

    double normalized = std::fmod(offset_hz, config.sample_rate_hz);
    if (normalized < 0.0) {
      normalized += config.sample_rate_hz;
    }
    for (const auto existing : normalized_offsets) {
      if (NearlyEqual(existing, normalized)) {
        return std::unexpected(
            MakeError(FHSSValidationCode::DuplicateIqOffset,
                      "active IQ offsets must be distinct modulo sample rate"));
      }
    }
    normalized_offsets.push_back(normalized);
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult ValidateActiveFrequencySet(
    const std::vector<std::uint32_t> &active_frequency_indices) {
  if (active_frequency_indices.size() !=
      FHSSProtocolConstants::kActiveFrequencyCount) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidActiveFrequencySet,
        "FHSS PR1 requires exactly four active preamble frequencies"));
  }

  std::set<std::uint32_t> unique_indices;
  for (const auto index : active_frequency_indices) {
    if (auto validation = ValidateSelectableFrequencyIndex(index);
        !validation) {
      return validation;
    }
    unique_indices.insert(index);
  }

  if (unique_indices.size() != FHSSProtocolConstants::kActiveFrequencyCount) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidActiveFrequencySet,
                  "active preamble frequency indices must be distinct"));
  }

  return {};
}

[[nodiscard]] inline bool
ContainsIndex(const std::vector<std::uint32_t> &indices, std::uint32_t index) {
  return std::find(indices.begin(), indices.end(), index) != indices.end();
}

[[nodiscard]] inline FHSSVoidResult ValidatePreamblePattern(
    const std::vector<FHSSPreamblePulseSpec> &preamble_pulses,
    const std::vector<std::uint32_t> &active_frequency_indices) {
  if (preamble_pulses.size() != FHSSProtocolConstants::kPreamblePulseCount) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidPreambleLength,
                  "FHSS PR1 preamble must contain exactly 16 pulses"));
  }

  for (const auto &pulse : preamble_pulses) {
    if (auto validation =
            ValidateSelectableFrequencyIndex(pulse.frequency_index);
        !validation) {
      return validation;
    }
    if (!ContainsIndex(active_frequency_indices, pulse.frequency_index)) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidPreambleFrequency,
          "preamble hop entry must be inside the active frequency set"));
    }
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult ValidatePayloadFrequencies(
    const std::vector<std::uint32_t> &payload_frequency_indices,
    const std::vector<std::uint32_t> &active_frequency_indices) {
  for (const auto index : payload_frequency_indices) {
    if (auto validation = ValidateSelectableFrequencyIndex(index);
        !validation) {
      return validation;
    }
    if (!ContainsIndex(active_frequency_indices, index)) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidPayloadFrequency,
                    "payload/body frequency must be selected from the active "
                    "preamble frequencies"));
    }
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult ValidatePreambleWordConsistency(
    const std::vector<FHSSPreamblePulseSpec> &preamble_pulses) {
  std::array<std::optional<std::uint32_t>,
             FHSSProtocolConstants::kFrequencyCount>
      words_by_index{};

  for (const auto &pulse : preamble_pulses) {
    if (auto validation =
            ValidateSelectableFrequencyIndex(pulse.frequency_index);
        !validation) {
      return validation;
    }

    auto &existing_word = words_by_index[pulse.frequency_index];
    if (!existing_word.has_value()) {
      existing_word = pulse.word_value;
    } else if (existing_word.value() != pulse.word_value) {
      return std::unexpected(MakeError(
          FHSSValidationCode::PreambleWordMismatch,
          "identical preamble frequencies must have identical word values"));
    }
  }

  return {};
}

[[nodiscard]] inline FHSSVoidResult
ValidateMessageLength(std::size_t preamble_pulse_count,
                      std::size_t payload_pulse_count) {
  const auto total_pulses = preamble_pulse_count + payload_pulse_count;
  if (total_pulses > FHSSProtocolConstants::kMaxMessagePulseCount) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidMessageLength,
        "FHSS PR1 messages may contain at most 256 pulses including preamble"));
  }
  return {};
}

[[nodiscard]] inline FHSSVoidResult
ValidateDecodeConfig(const FHSSDecodeConfig &config) {
  if (auto timing = DeriveTimingModel(config.timing); !timing) {
    return std::unexpected(timing.error());
  }
  if (auto frequency = ValidateFrequencyConfig(config.frequency); !frequency) {
    return frequency;
  }
  if (auto active = ValidateActiveFrequencySet(config.active_frequency_indices);
      !active) {
    return active;
  }
  if (auto iq =
          ValidateIqOffsets(config.frequency, config.active_frequency_indices);
      !iq) {
    return iq;
  }
  if (auto preamble = ValidatePreamblePattern(config.preamble_pulses,
                                              config.active_frequency_indices);
      !preamble) {
    return preamble;
  }
  if (auto words = ValidatePreambleWordConsistency(config.preamble_pulses);
      !words) {
    return words;
  }
  if (auto payload = ValidatePayloadFrequencies(
          config.payload_frequency_indices, config.active_frequency_indices);
      !payload) {
    return payload;
  }
  if (auto length =
          ValidateMessageLength(config.preamble_pulses.size(),
                                config.payload_frequency_indices.size());
      !length) {
    return length;
  }
  if (!config.payload_random.deterministic) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidPayloadFrequency,
        "FHSS PR1 payload/body random selection must be deterministic"));
  }
  return {};
}

} // namespace dsp::fhss
