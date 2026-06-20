/**
 * @file FHSSMessageAssembly.hpp
 * @brief PR7 hop-only preamble detection and FHSS message assembly helpers.
 *
 * @details CPU-only message-layer helpers for decoded FHSS pulse words. This
 * file does not add graph runtime integration, channelization, GPU execution,
 * Doppler/noise behavior, overlap-aware separation, or production RF claims.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSPulseWordDecoder.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

enum class FHSSMessageAssemblyStatus {
  Ok,
  MissingPreamble,
  InvalidPreambleFixture,
  InvalidActiveSet,
  PayloadFrequencyRejected,
  MessageTooLong,
  UnsupportedOverlap
};

enum class FHSSTruthMismatchKind {
  StartSample,
  Duration,
  Frequency,
  Value
};

struct FHSSTruthMismatch {
  std::size_t pulse_index = 0;
  FHSSTruthMismatchKind kind = FHSSTruthMismatchKind::StartSample;
  std::string message;
};

struct FHSSMessageDiagnostics {
  std::size_t pulse_count = 0;
  std::size_t rejected_count = 0;
  bool preamble_lock = false;
  std::size_t truth_mismatch_count = 0;
};

struct FHSSPreambleDetectionResult {
  bool preamble_lock = false;
  std::vector<std::uint32_t> active_frequency_indices;
  FHSSMessageAssemblyStatus status = FHSSMessageAssemblyStatus::Ok;
  std::string status_message;
};

struct FHSSAssembledMessage {
  std::vector<FHSSDecodedPulseWord> ordered_pulses;
  std::vector<FHSSDecodedPulseWord> preamble_pulses;
  std::vector<FHSSDecodedPulseWord> payload_pulses;
  std::vector<std::uint32_t> active_frequency_indices;
  std::vector<FHSSTruthMismatch> truth_mismatches;
  FHSSMessageDiagnostics diagnostics{};
  FHSSMessageAssemblyStatus status = FHSSMessageAssemblyStatus::Ok;
  std::string status_message;
};

struct FHSSMessageAssemblerConfig {
  std::vector<FHSSPreamblePulseSpec> preamble_pulses;
  std::vector<FHSSTruthPulse> truth_pulses;
};

[[nodiscard]] inline const char *
FHSSMessageAssemblyStatusName(FHSSMessageAssemblyStatus status) {
  switch (status) {
  case FHSSMessageAssemblyStatus::Ok:
    return "Ok";
  case FHSSMessageAssemblyStatus::MissingPreamble:
    return "MissingPreamble";
  case FHSSMessageAssemblyStatus::InvalidPreambleFixture:
    return "InvalidPreambleFixture";
  case FHSSMessageAssemblyStatus::InvalidActiveSet:
    return "InvalidActiveSet";
  case FHSSMessageAssemblyStatus::PayloadFrequencyRejected:
    return "PayloadFrequencyRejected";
  case FHSSMessageAssemblyStatus::MessageTooLong:
    return "MessageTooLong";
  case FHSSMessageAssemblyStatus::UnsupportedOverlap:
    return "UnsupportedOverlap";
  }
  return "Unknown";
}

[[nodiscard]] inline std::vector<FHSSDecodedPulseWord>
GloballyOrderDecodedPulses(std::vector<FHSSDecodedPulseWord> pulses) {
  std::sort(pulses.begin(), pulses.end(),
            [](const FHSSDecodedPulseWord &lhs,
               const FHSSDecodedPulseWord &rhs) {
              return lhs.candidate.detected_pulse.global_start_sample <
                     rhs.candidate.detected_pulse.global_start_sample;
            });
  return pulses;
}

[[nodiscard]] inline bool
DecodedPulsesOverlap(const FHSSDecodedPulseWord &lhs,
                     const FHSSDecodedPulseWord &rhs) {
  return PulsesOverlap(lhs.candidate.detected_pulse,
                       rhs.candidate.detected_pulse);
}

[[nodiscard]] inline bool HasUnsupportedOverlap(
    const std::vector<FHSSDecodedPulseWord> &ordered_pulses) {
  for (std::size_t i = 1; i < ordered_pulses.size(); ++i) {
    if (DecodedPulsesOverlap(ordered_pulses[i - 1], ordered_pulses[i])) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline std::vector<std::uint32_t>
DeriveActiveFrequenciesFromPreamble(
    const std::vector<FHSSPreamblePulseSpec> &preamble_pulses) {
  std::set<std::uint32_t> active;
  for (const auto &pulse : preamble_pulses) {
    active.insert(pulse.frequency_index);
  }
  return {active.begin(), active.end()};
}

class FHSSPreambleDetectorKernel {
public:
  [[nodiscard]] static FHSSPreambleDetectionResult Detect(
      const std::vector<FHSSDecodedPulseWord> &ordered_pulses,
      const std::vector<FHSSPreamblePulseSpec> &preamble_pulses) {
    FHSSPreambleDetectionResult result{};

    if (preamble_pulses.size() !=
        FHSSProtocolConstants::kPreamblePulseCount) {
      result.status = FHSSMessageAssemblyStatus::InvalidPreambleFixture;
      result.status_message =
          "FHSS PR7 preamble fixture must contain exactly 16 pulses";
      return result;
    }
    if (auto consistency = ValidatePreambleWordConsistency(preamble_pulses);
        !consistency) {
      result.status = FHSSMessageAssemblyStatus::InvalidPreambleFixture;
      result.status_message = consistency.error().message;
      return result;
    }

    result.active_frequency_indices =
        DeriveActiveFrequenciesFromPreamble(preamble_pulses);
    if (auto active = ValidateActiveFrequencySet(
            result.active_frequency_indices);
        !active) {
      result.status = FHSSMessageAssemblyStatus::InvalidActiveSet;
      result.status_message = active.error().message;
      return result;
    }

    if (ordered_pulses.size() <
        FHSSProtocolConstants::kPreamblePulseCount) {
      result.status = FHSSMessageAssemblyStatus::MissingPreamble;
      result.status_message =
          "FHSS PR7 message does not contain the 16-pulse preamble";
      return result;
    }

    for (std::size_t i = 0; i < FHSSProtocolConstants::kPreamblePulseCount;
         ++i) {
      if (ordered_pulses[i].candidate.detected_pulse.frequency_index !=
          preamble_pulses[i].frequency_index) {
        result.status = FHSSMessageAssemblyStatus::MissingPreamble;
        result.status_message =
            "FHSS PR7 hop-only preamble frequencies did not match";
        return result;
      }
    }

    result.preamble_lock = true;
    result.status = FHSSMessageAssemblyStatus::Ok;
    result.status_message = FHSSMessageAssemblyStatusName(result.status);
    return result;
  }
};

[[nodiscard]] inline std::vector<FHSSTruthMismatch>
CompareDecodedPulsesToTruth(
    const std::vector<FHSSDecodedPulseWord> &ordered_pulses,
    const std::vector<FHSSTruthPulse> &truth_pulses) {
  std::vector<FHSSTruthMismatch> mismatches;
  const auto count = std::min(ordered_pulses.size(), truth_pulses.size());
  for (std::size_t i = 0; i < count; ++i) {
    const auto &actual = ordered_pulses[i];
    const auto &truth = truth_pulses[i];
    const auto &detected = actual.candidate.detected_pulse;

    if (detected.global_start_sample != truth.global_start_sample) {
      mismatches.push_back(FHSSTruthMismatch{
          .pulse_index = i,
          .kind = FHSSTruthMismatchKind::StartSample,
          .message = "decoded pulse start sample does not match truth"});
    }
    if (detected.duration_samples != truth.duration_samples) {
      mismatches.push_back(FHSSTruthMismatch{
          .pulse_index = i,
          .kind = FHSSTruthMismatchKind::Duration,
          .message = "decoded pulse duration does not match truth"});
    }
    if (detected.frequency_index != truth.frequency_index) {
      mismatches.push_back(FHSSTruthMismatch{
          .pulse_index = i,
          .kind = FHSSTruthMismatchKind::Frequency,
          .message = "decoded pulse frequency does not match truth"});
    }
    if (actual.decoded_value != truth.value) {
      mismatches.push_back(FHSSTruthMismatch{
          .pulse_index = i,
          .kind = FHSSTruthMismatchKind::Value,
          .message = "decoded pulse value does not match truth"});
    }
  }
  return mismatches;
}

class FHSSMessageAssemblerKernel {
public:
  [[nodiscard]] static FHSSAssembledMessage Assemble(
      std::vector<FHSSDecodedPulseWord> decoded_pulses,
      const FHSSMessageAssemblerConfig &config) {
    FHSSAssembledMessage message{};
    message.ordered_pulses = GloballyOrderDecodedPulses(std::move(decoded_pulses));
    message.diagnostics.pulse_count = message.ordered_pulses.size();

    auto reject = [&](FHSSMessageAssemblyStatus status,
                      std::string status_message) {
      message.status = status;
      message.status_message = std::move(status_message);
      message.diagnostics.rejected_count = message.ordered_pulses.size();
      message.diagnostics.preamble_lock = false;
      message.diagnostics.truth_mismatch_count =
          message.truth_mismatches.size();
      return message;
    };

    if (message.ordered_pulses.size() >
        FHSSProtocolConstants::kMaxMessagePulseCount) {
      return reject(FHSSMessageAssemblyStatus::MessageTooLong,
                    "FHSS PR7 messages may contain at most 256 pulses");
    }

    if (HasUnsupportedOverlap(message.ordered_pulses)) {
      return reject(FHSSMessageAssemblyStatus::UnsupportedOverlap,
                    "FHSS PR7 keeps PR1 overlapped-message rejection");
    }

    auto preamble = FHSSPreambleDetectorKernel::Detect(message.ordered_pulses,
                                                       config.preamble_pulses);
    message.active_frequency_indices = preamble.active_frequency_indices;
    message.diagnostics.preamble_lock = preamble.preamble_lock;
    if (!preamble.preamble_lock) {
      return reject(preamble.status, preamble.status_message);
    }

    message.preamble_pulses.assign(
        message.ordered_pulses.begin(),
        message.ordered_pulses.begin() +
            FHSSProtocolConstants::kPreamblePulseCount);
    message.payload_pulses.assign(
        message.ordered_pulses.begin() +
            FHSSProtocolConstants::kPreamblePulseCount,
        message.ordered_pulses.end());

    for (const auto &payload : message.payload_pulses) {
      if (!ContainsIndex(message.active_frequency_indices,
                         payload.candidate.detected_pulse.frequency_index)) {
        return reject(
            FHSSMessageAssemblyStatus::PayloadFrequencyRejected,
            "FHSS PR7 payload frequency is outside locked active set");
      }
    }

    message.truth_mismatches =
        CompareDecodedPulsesToTruth(message.ordered_pulses,
                                    config.truth_pulses);
    message.diagnostics.truth_mismatch_count =
        message.truth_mismatches.size();
    message.status = FHSSMessageAssemblyStatus::Ok;
    message.status_message = FHSSMessageAssemblyStatusName(message.status);
    return message;
  }
};

class FHSSMessageSinkKernel {
public:
  [[nodiscard]] static FHSSMessageDiagnostics
  Diagnostics(const FHSSAssembledMessage &message) {
    return message.diagnostics;
  }
};

} // namespace dsp::fhss
