/**
 * @file FHSSCrossNodeValidator.cpp
 * @brief Implementation of cross-node semantic validation for FHSS configurations.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/fhss/FHSSCrossNodeValidator.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace dsp::fhss {

// ============================================================================
// Error Handling
// ============================================================================

ValidationError FHSSCrossNodeValidator::MakeError(
    ValidationErrorCode code, const std::string& message,
    const std::string& field, const std::string& expected,
    const std::string& actual) {
  return ValidationError{code, message, field, expected, actual};
}

// ============================================================================
// Main Validation Function
// ============================================================================

std::expected<void, std::vector<ValidationError>>
FHSSCrossNodeValidator::Validate(const EffectiveConfiguration& config) {
  std::vector<ValidationError> all_errors;

  // Run all 13 validator functions (no fail-fast)
  auto errors1 = ValidateTopologyInvariant(config);
  all_errors.insert(all_errors.end(), errors1.begin(), errors1.end());

  auto errors2 = ValidateMessageUniqueness(config);
  all_errors.insert(all_errors.end(), errors2.begin(), errors2.end());

  auto errors3 = ValidatePreambleFormat(config);
  all_errors.insert(all_errors.end(), errors3.begin(), errors3.end());

  auto errors4 = ValidateFrequencyConstraint(config);
  all_errors.insert(all_errors.end(), errors4.begin(), errors4.end());

  auto errors5 = ValidateCfoConstraint(config);
  all_errors.insert(all_errors.end(), errors5.begin(), errors5.end());

  auto errors6 = ValidateBandwidthConstraint(config);
  all_errors.insert(all_errors.end(), errors6.begin(), errors6.end());

  auto errors7 = ValidateScheduleOrdering(config);
  all_errors.insert(all_errors.end(), errors7.begin(), errors7.end());

  auto errors8 = ValidateCrossNodeConsistency(config);
  all_errors.insert(all_errors.end(), errors8.begin(), errors8.end());

  auto errors9 = ValidateBandwidthCfoAgreement(config);
  all_errors.insert(all_errors.end(), errors9.begin(), errors9.end());

  auto errors10 = ValidateDerivedProjectionMatch(config);
  all_errors.insert(all_errors.end(), errors10.begin(), errors10.end());

  auto errors11 = ValidateTopologyPreservation(config);
  all_errors.insert(all_errors.end(), errors11.begin(), errors11.end());

  auto errors12 = ValidateIdleDurationValid(config);
  all_errors.insert(all_errors.end(), errors12.begin(), errors12.end());

  auto errors13 = ValidateRoleConsistency(config);
  all_errors.insert(all_errors.end(), errors13.begin(), errors13.end());

  // Return result
  if (!all_errors.empty()) {
    return std::unexpected(all_errors);
  }

  return {};  // All validations passed
}

// ============================================================================
// Individual Validation Rules
// ============================================================================

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateTopologyInvariant(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 1: Graph connectivity must be preserved
  // Check that RF copies exist for each active frequency
  if (config.active_frequency_indices_source.empty() && !config.rf_copies.empty()) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_TOPOLOGY_001,
        "Graph topology violation: RF copies exist but no active frequencies defined",
        "active_frequency_indices_source",
        "non-empty",
        "empty"));
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateMessageUniqueness(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 2: No duplicate message IDs
  std::set<std::string> seen_ids;
  for (const auto& msg : config.source.messages) {
    if (msg.contains("message_id")) {
      std::string msg_id = msg["message_id"].is_string()
                               ? msg["message_id"].get<std::string>()
                               : msg["message_id"].dump();

      if (seen_ids.count(msg_id)) {
        errors.push_back(MakeError(
            ValidationErrorCode::ERR_MESSAGE_001,
            "Duplicate message ID found in configuration",
            "message_id",
            "unique IDs",
            msg_id));
      }
      seen_ids.insert(msg_id);
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidatePreambleFormat(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 3: Preamble must follow format spec
  // Verify preamble pulses have valid frequency indices
  for (size_t i = 0; i < config.preamble_pulses.size(); ++i) {
    const auto& pulse = config.preamble_pulses[i];
    if (pulse.frequency_index > 63) {  // Valid range is 0-63
      errors.push_back(MakeError(
          ValidationErrorCode::ERR_PREAMBLE_001,
          "Preamble pulse has invalid frequency index",
          "preamble_pulses[" + std::to_string(i) + "].frequency_index",
          "0-63",
          std::to_string(pulse.frequency_index)));
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateFrequencyConstraint(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 4: Frequencies must be within valid range
  auto check_indices = [&errors](const std::string& field_name,
                                   const std::vector<int32_t>& indices) {
    for (size_t i = 0; i < indices.size(); ++i) {
      if (indices[i] < 0 || indices[i] > 63) {
        errors.push_back(MakeError(
            ValidationErrorCode::ERR_FREQUENCY_001,
            "Frequency index out of valid range",
            field_name + "[" + std::to_string(i) + "]",
            "0-63",
            std::to_string(indices[i])));
      }
    }
  };

  check_indices("active_frequency_indices_source",
                config.active_frequency_indices_source);
  check_indices("active_frequency_indices_preamble",
                config.active_frequency_indices_preamble);
  check_indices("active_frequency_indices_channelizer",
                config.active_frequency_indices_channelizer);

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateCfoConstraint(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 5: CFO must be ≤ max_abs_cfo_hz
  // In this simplified implementation, we check the derived impairments
  for (const auto& impairment : config.impairment_copies) {
    if (impairment.impairment_type == "Doppler") {
      if (impairment.parameter_value > config.source.max_abs_cfo_hz) {
        std::ostringstream expected_str, actual_str;
        expected_str << "<= " << config.source.max_abs_cfo_hz;
        actual_str << impairment.parameter_value;

        errors.push_back(MakeError(
            ValidationErrorCode::ERR_CFO_001,
            "CFO exceeds maximum absolute CFO constraint",
            "impairment_copies.Doppler.parameter_value",
            expected_str.str(),
            actual_str.str()));
      }
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateBandwidthConstraint(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 6: Occupied bandwidth must be reasonable (positive, <= 500 MHz)
  if (config.source.occupied_bandwidth_hz < 0) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_BANDWIDTH_001,
        "Occupied bandwidth must be positive",
        "occupied_bandwidth_hz",
        "> 0",
        std::to_string(config.source.occupied_bandwidth_hz)));
  } else if (config.source.occupied_bandwidth_hz > 500'000'000.0) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_BANDWIDTH_001,
        "Occupied bandwidth exceeds maximum (500 MHz)",
        "occupied_bandwidth_hz",
        "<= 500000000",
        std::to_string(config.source.occupied_bandwidth_hz)));
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateScheduleOrdering(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 7: Messages must not overlap (if !allow_overlap)
  if (!config.source.allow_overlap && config.source.messages.size() > 1) {
    std::vector<std::pair<int32_t, int32_t>> schedules;  // (start, duration)

    for (const auto& msg : config.source.messages) {
      if (msg.contains("transmit_start_sample") && msg.contains("duration_samples")) {
        int32_t start = msg["transmit_start_sample"].is_number_integer()
                            ? msg["transmit_start_sample"].get<int32_t>()
                            : 0;
        int32_t duration = msg["duration_samples"].is_number_integer()
                               ? msg["duration_samples"].get<int32_t>()
                               : 0;
        schedules.push_back({start, duration});
      }
    }

    // Check for overlaps
    for (size_t i = 0; i < schedules.size(); ++i) {
      for (size_t j = i + 1; j < schedules.size(); ++j) {
        int32_t start_i = schedules[i].first;
        int32_t end_i = start_i + schedules[i].second;
        int32_t start_j = schedules[j].first;
        int32_t end_j = start_j + schedules[j].second;

        // Check if intervals overlap
        if (!(end_i <= start_j || end_j <= start_i)) {
          errors.push_back(MakeError(
              ValidationErrorCode::ERR_SCHEDULE_001,
              "Messages overlap in time schedule",
              "messages[" + std::to_string(i) + "] and [" + std::to_string(j) + "]",
              "no overlap",
              "overlap detected"));
        }
      }
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateCrossNodeConsistency(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 8: All nodes must agree on message schema
  // Check that all messages have the required fields
  for (size_t i = 0; i < config.source.messages.size(); ++i) {
    const auto& msg = config.source.messages[i];
    if (!msg.contains("message_id")) {
      errors.push_back(MakeError(
          ValidationErrorCode::ERR_CONSISTENCY_001,
          "Message missing required field 'message_id'",
          "messages[" + std::to_string(i) + "].message_id",
          "present",
          "missing"));
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateBandwidthCfoAgreement(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 9: CFO and bandwidth must be compatible
  // Rule: CFO should be at most 10% of bandwidth
  if (config.source.occupied_bandwidth_hz > 0) {
    double max_cfo = config.source.occupied_bandwidth_hz * 0.1;
    if (config.source.max_abs_cfo_hz > max_cfo) {
      errors.push_back(MakeError(
          ValidationErrorCode::ERR_BANDWIDTH_CFO_001,
          "CFO is too large relative to bandwidth",
          "max_abs_cfo_hz vs occupied_bandwidth_hz",
          "<= " + std::to_string(max_cfo),
          std::to_string(config.source.max_abs_cfo_hz)));
    }
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateDerivedProjectionMatch(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 10: Derived config must match expected
  // Verify that RF copies count matches active frequencies count
  if (config.rf_copies.size() != config.active_frequency_indices_source.size()) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_PROJECTION_001,
        "Derived RF copies count does not match active frequencies",
        "rf_copies.size() vs active_frequency_indices_source.size()",
        std::to_string(config.active_frequency_indices_source.size()),
        std::to_string(config.rf_copies.size())));
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateTopologyPreservation(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 11: Reconfiguration must not lose nodes
  // Ensure active frequencies don't decrease compared to preamble
  if (config.active_frequency_indices_preamble.size() >
      config.active_frequency_indices_source.size()) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_TOPOLOGY_002,
        "Topology violation: preamble frequencies exceed source frequencies",
        "active_frequency_indices_preamble vs active_frequency_indices_source",
        "<= source size",
        std::to_string(config.active_frequency_indices_preamble.size())));
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateIdleDurationValid(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 12: Idle duration must be positive (or zero for no idle)
  if (config.source.idle_duration_samples < 0) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_IDLE_001,
        "Idle duration cannot be negative",
        "idle_duration_samples",
        ">= 0",
        std::to_string(config.source.idle_duration_samples)));
  }

  return errors;
}

std::vector<ValidationError>
FHSSCrossNodeValidator::ValidateRoleConsistency(
    const EffectiveConfiguration& config) {
  std::vector<ValidationError> errors;

  // Rule 13: Role assignment must be valid for topology
  // If role is Source, must have messages
  if (config.source.role == SourceConfiguration::Role::Source &&
      config.source.messages.empty()) {
    errors.push_back(MakeError(
        ValidationErrorCode::ERR_ROLE_001,
        "Source role requires messages to be configured",
        "role",
        "Source role with messages",
        "Source role without messages"));
  }

  return errors;
}

}  // namespace dsp::fhss
