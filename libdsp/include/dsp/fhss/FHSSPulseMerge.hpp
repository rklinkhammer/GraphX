/**
 * @file FHSSPulseMerge.hpp
 * @brief FHSS detected-pulse normalization and merge helpers.
 *
 * @details PR3 CPU-only association contracts. This file models the
 * FHSSPulseMergeNode semantics without adding a GraphX runtime node or plugin.
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
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

struct FHSSSampleTimeMap {
  bool has_input_global_start_sample = true;
  std::uint64_t input_packet_global_start_sample = 0;
  std::uint64_t output_start_sample = 0;
  std::uint32_t decimation_factor = 1;
  std::int64_t group_delay_input_samples = 0;
  double sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
};

struct FHSSComplexEvidence {
  std::shared_ptr<const std::vector<std::complex<double>>> samples;
  std::uint64_t sample_offset = 0;
  std::uint64_t sample_count = 0;
};

struct FHSSLocalPulseDetection {
  FHSSSampleTimeMap sample_time_map{};
  std::uint64_t local_start_offset = 0;
  std::uint64_t duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
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
  FHSSComplexEvidence complex_evidence{};
};

struct FHSSPulseCandidateWithEvidence {
  FHSSPulseCandidate candidate{};
  FHSSComplexEvidence complex_evidence{};
};

enum class FHSSPulseMergeRejectReason {
  InvalidMetadata,
  DuplicateLowerConfidence,
  UnsupportedOverlap
};

struct FHSSPulseMergeRejection {
  FHSSPulseCandidateWithEvidence rejected_candidate{};
  FHSSPulseMergeRejectReason reason =
      FHSSPulseMergeRejectReason::InvalidMetadata;
  std::string message;
};

struct FHSSPulseMergeConfig {
  FHSSTimingModel timing{};
  std::optional<std::uint64_t> message_epoch_sample{};
};

struct FHSSPulseMergeResult {
  std::vector<FHSSPulseCandidateWithEvidence> ordered_candidates;
  std::vector<FHSSPulseMergeRejection> rejections;
};

[[nodiscard]] inline FHSSVoidResult
ValidateSampleTimeMap(const FHSSSampleTimeMap &map) {
  if (!map.has_input_global_start_sample) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS pulse association requires input global sample timing"));
  }
  if (map.decimation_factor == 0) {
    return std::unexpected(MakeError(FHSSValidationCode::InvalidGlobalTiming,
                                     "decimation factor must be non-zero"));
  }
  if (!NearlyEqual(map.sample_rate_hz, FHSSProtocolConstants::kSampleRateHz)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS PR3 sample-time map must identify the 500 Msps input domain"));
  }
  return {};
}

[[nodiscard]] inline FHSSResult<std::uint64_t>
NormalizeToGlobalStartSample(const FHSSSampleTimeMap &map,
                             std::uint64_t local_start_offset) {
  if (auto validation = ValidateSampleTimeMap(map); !validation) {
    return std::unexpected(validation.error());
  }

  const auto channel_local_input_offset = static_cast<std::int64_t>(
      (map.output_start_sample + local_start_offset) * map.decimation_factor);
  const auto global_offset =
      channel_local_input_offset - map.group_delay_input_samples;
  if (global_offset < 0) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "sample-time mapping produced a negative global offset"));
  }

  return map.input_packet_global_start_sample +
         static_cast<std::uint64_t>(global_offset);
}

[[nodiscard]] inline bool
EvidenceRangeIsValid(const FHSSComplexEvidence &evidence) {
  if (!evidence.samples) {
    return evidence.sample_count == 0;
  }
  return evidence.sample_offset <= evidence.samples->size() &&
         evidence.sample_count <=
             evidence.samples->size() - evidence.sample_offset;
}

[[nodiscard]] inline FHSSResult<FHSSPulseCandidateWithEvidence>
NormalizeLocalDetection(const FHSSLocalPulseDetection &local,
                        const FHSSPulseMergeConfig &config = {}) {
  if (auto index = ValidateFrequencyIndex(local.frequency_index); !index) {
    return std::unexpected(index.error());
  }
  if (local.duration_samples == 0) {
    return std::unexpected(
        MakeError(FHSSValidationCode::InvalidGlobalTiming,
                  "detected pulse duration must be non-zero"));
  }
  if (!EvidenceRangeIsValid(local.complex_evidence)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "complex evidence range is outside the referenced sample payload"));
  }

  auto global_start = NormalizeToGlobalStartSample(local.sample_time_map,
                                                   local.local_start_offset);
  if (!global_start) {
    return std::unexpected(global_start.error());
  }

  FHSSDetectedPulse detected{};
  detected.global_start_sample = *global_start;
  detected.global_end_sample = *global_start + local.duration_samples;
  detected.duration_samples = local.duration_samples;
  detected.channel_start_sample = local.local_start_offset;
  detected.channel_id = local.channel_id;
  detected.frequency_index = local.frequency_index;
  detected.rf_frequency_hz = local.rf_frequency_hz;
  detected.iq_offset_frequency_hz = local.iq_offset_frequency_hz;
  detected.estimated_center_frequency_hz = local.estimated_center_frequency_hz;
  detected.frequency_error_hz = local.frequency_error_hz;
  detected.amplitude = local.amplitude;
  detected.power_db = local.power_db;
  detected.snr_db = local.snr_db;
  detected.noise_floor_db = local.noise_floor_db;
  detected.phase_at_start_rad = local.phase_at_start_rad;
  detected.phase_slope_rad_per_sample = local.phase_slope_rad_per_sample;
  detected.cfo_hz = local.cfo_hz;
  detected.bandwidth_hz = local.bandwidth_hz;
  detected.confidence = local.confidence;
  detected.detector_id = local.detector_id;
  detected.packet_sequence = local.packet_sequence;

  FHSSPulseCandidate candidate{};
  candidate.detected_pulse = detected;
  candidate.provisional_slot_index =
      detected.global_start_sample / config.timing.pulse_period_samples;
  if (config.message_epoch_sample.has_value()) {
    if (detected.global_start_sample < *config.message_epoch_sample) {
      return std::unexpected(
          MakeError(FHSSValidationCode::InvalidGlobalTiming,
                    "detected pulse starts before the message epoch sample"));
    }
    candidate.final_slot_index =
        (detected.global_start_sample - *config.message_epoch_sample) /
        config.timing.pulse_period_samples;
  }

  return FHSSPulseCandidateWithEvidence{
      .candidate = candidate, .complex_evidence = local.complex_evidence};
}

[[nodiscard]] inline bool PulsesOverlap(const FHSSDetectedPulse &lhs,
                                        const FHSSDetectedPulse &rhs) {
  return lhs.global_start_sample < rhs.global_end_sample &&
         rhs.global_start_sample < lhs.global_end_sample;
}

[[nodiscard]] inline bool IsDuplicateDetection(const FHSSDetectedPulse &lhs,
                                               const FHSSDetectedPulse &rhs) {
  return lhs.frequency_index == rhs.frequency_index && PulsesOverlap(lhs, rhs);
}

[[nodiscard]] inline bool
IsHigherQualityCandidate(const FHSSPulseCandidateWithEvidence &candidate,
                         const FHSSPulseCandidateWithEvidence &incumbent) {
  if (!NearlyEqual(candidate.candidate.detected_pulse.confidence,
                   incumbent.candidate.detected_pulse.confidence)) {
    return candidate.candidate.detected_pulse.confidence >
           incumbent.candidate.detected_pulse.confidence;
  }
  return candidate.candidate.detected_pulse.snr_db >
         incumbent.candidate.detected_pulse.snr_db;
}

class FHSSPulseMergeNode {
public:
  [[nodiscard]] static FHSSPulseMergeResult
  Merge(const std::vector<FHSSLocalPulseDetection> &local_detections,
        const FHSSPulseMergeConfig &config = {}) {
    FHSSPulseMergeResult result{};

    for (const auto &local : local_detections) {
      auto normalized = NormalizeLocalDetection(local, config);
      if (!normalized) {
        result.rejections.push_back(FHSSPulseMergeRejection{
            .rejected_candidate = {},
            .reason = FHSSPulseMergeRejectReason::InvalidMetadata,
            .message = normalized.error().message});
        continue;
      }

      bool consumed = false;
      for (auto &incumbent : result.ordered_candidates) {
        if (!IsDuplicateDetection(normalized->candidate.detected_pulse,
                                  incumbent.candidate.detected_pulse)) {
          continue;
        }

        if (IsHigherQualityCandidate(*normalized, incumbent)) {
          result.rejections.push_back(FHSSPulseMergeRejection{
              .rejected_candidate = incumbent,
              .reason = FHSSPulseMergeRejectReason::DuplicateLowerConfidence,
              .message =
                  "duplicate pulse replaced by higher-quality detection"});
          incumbent = *normalized;
        } else {
          result.rejections.push_back(FHSSPulseMergeRejection{
              .rejected_candidate = *normalized,
              .reason = FHSSPulseMergeRejectReason::DuplicateLowerConfidence,
              .message =
                  "duplicate pulse rejected in favor of existing detection"});
        }
        consumed = true;
        break;
      }
      if (!consumed) {
        result.ordered_candidates.push_back(*normalized);
      }
    }

    std::sort(result.ordered_candidates.begin(),
              result.ordered_candidates.end(),
              [](const FHSSPulseCandidateWithEvidence &lhs,
                 const FHSSPulseCandidateWithEvidence &rhs) {
                return lhs.candidate.detected_pulse.global_start_sample <
                       rhs.candidate.detected_pulse.global_start_sample;
              });

    std::vector<FHSSPulseCandidateWithEvidence> accepted;
    accepted.reserve(result.ordered_candidates.size());
    for (const auto &candidate : result.ordered_candidates) {
      bool rejected_for_overlap = false;
      for (const auto &incumbent : accepted) {
        if (candidate.candidate.detected_pulse.frequency_index !=
                incumbent.candidate.detected_pulse.frequency_index &&
            PulsesOverlap(candidate.candidate.detected_pulse,
                          incumbent.candidate.detected_pulse)) {
          result.rejections.push_back(FHSSPulseMergeRejection{
              .rejected_candidate = candidate,
              .reason = FHSSPulseMergeRejectReason::UnsupportedOverlap,
              .message =
                  "cross-frequency overlapping pulses are unsupported in PR1"});
          rejected_for_overlap = true;
          break;
        }
      }
      if (!rejected_for_overlap) {
        accepted.push_back(candidate);
      }
    }

    result.ordered_candidates = std::move(accepted);
    return result;
  }
};

} // namespace dsp::fhss
