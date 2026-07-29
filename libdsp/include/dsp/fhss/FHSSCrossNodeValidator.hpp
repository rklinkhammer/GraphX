/**
 * @file FHSSCrossNodeValidator.hpp
 * @brief Cross-node semantic validation for FHSS configurations.
 *
 * @details Enforces all 13 semantic validation rules with stable error codes,
 * clear error messages, and complete error collection (no fail-fast).
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSConfigurationDeriver.hpp"
#include <vector>
#include <string>
#include <expected>

namespace dsp::fhss {

/**
 * @enum ValidationErrorCode
 * @brief Stable error codes for all 13 validation rules.
 */
enum class ValidationErrorCode {
  // Topology and connectivity
  ERR_TOPOLOGY_001 = 1001,  // Graph connectivity must be preserved
  ERR_TOPOLOGY_002 = 1002,  // Reconfiguration must not lose nodes

  // Message-level validation
  ERR_MESSAGE_001 = 2001,  // No duplicate message IDs
  ERR_PREAMBLE_001 = 2002,  // Preamble must follow format spec
  ERR_SCHEDULE_001 = 2003,  // Messages must not overlap (if !allow_overlap)

  // Frequency and CFO constraints
  ERR_FREQUENCY_001 = 3001,  // Frequencies must be within valid range
  ERR_CFO_001 = 3002,        // CFO must be ≤ max_abs_cfo_hz
  ERR_BANDWIDTH_001 = 3003,  // Occupied bandwidth must be reasonable
  ERR_BANDWIDTH_CFO_001 = 3004,  // CFO and bandwidth must be compatible

  // Cross-node consistency
  ERR_CONSISTENCY_001 = 4001,  // All nodes must agree on message schema
  ERR_ROLE_001 = 4002,         // Role assignment must be valid for topology

  // Derived configuration
  ERR_PROJECTION_001 = 5001,  // Derived config must match expected
  ERR_IDLE_001 = 6001,        // Idle duration must be positive
};

/**
 * @struct ValidationError
 * @brief Single validation error with stable code and clear message.
 */
struct ValidationError {
  ValidationErrorCode code = ValidationErrorCode::ERR_TOPOLOGY_001;
  std::string message;
  std::string field;  // Which field caused the error
  std::string expected;  // What was expected
  std::string actual;    // What was found
};

/**
 * @class FHSSCrossNodeValidator
 * @brief Validates EffectiveConfiguration against all 13 semantic rules.
 *
 * **Key Properties:**
 * - All 13 rules run (no fail-fast)
 * - Error codes stable and unique
 * - Error messages clear and actionable (include field names and constraints)
 * - No source code paths in error messages
 * - Returns all violations found
 */
class FHSSCrossNodeValidator {
public:
  /**
   * @brief Validate effective configuration against all 13 rules.
   *
   * @param config The effective configuration to validate
   * @return Expected containing empty vector if valid, or vector of errors
   */
  [[nodiscard]] static std::expected<void, std::vector<ValidationError>>
  Validate(const EffectiveConfiguration& config);

private:
  // Individual validator functions (return true if valid, false if error found)
  
  /**
   * @brief Rule 1: Topology Invariant
   * Graph connectivity must be preserved
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateTopologyInvariant(const EffectiveConfiguration& config);

  /**
   * @brief Rule 2: Message Uniqueness
   * No duplicate message IDs
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateMessageUniqueness(const EffectiveConfiguration& config);

  /**
   * @brief Rule 3: Preamble Format
   * Preamble must follow format spec
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidatePreambleFormat(const EffectiveConfiguration& config);

  /**
   * @brief Rule 4: Frequency Constraint
   * Frequencies must be within valid range
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateFrequencyConstraint(const EffectiveConfiguration& config);

  /**
   * @brief Rule 5: CFO Constraint
   * CFO must be ≤ max_abs_cfo_hz
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateCfoConstraint(const EffectiveConfiguration& config);

  /**
   * @brief Rule 6: Bandwidth Constraint
   * Occupied bandwidth must be reasonable
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateBandwidthConstraint(const EffectiveConfiguration& config);

  /**
   * @brief Rule 7: Schedule Ordering
   * Messages must not overlap (if !allow_overlap)
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateScheduleOrdering(const EffectiveConfiguration& config);

  /**
   * @brief Rule 8: Cross-Node Consistency
   * All nodes must agree on message schema
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateCrossNodeConsistency(const EffectiveConfiguration& config);

  /**
   * @brief Rule 9: Bandwidth/CFO Agreement
   * CFO and bandwidth must be compatible
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateBandwidthCfoAgreement(const EffectiveConfiguration& config);

  /**
   * @brief Rule 10: Derived Projection Match
   * Derived config must match expected
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateDerivedProjectionMatch(const EffectiveConfiguration& config);

  /**
   * @brief Rule 11: Topology Preservation
   * Reconfiguration must not lose nodes
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateTopologyPreservation(const EffectiveConfiguration& config);

  /**
   * @brief Rule 12: Idle Duration Valid
   * Idle duration must be positive
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateIdleDurationValid(const EffectiveConfiguration& config);

  /**
   * @brief Rule 13: Role Consistency
   * Role assignment must be valid for topology
   */
  [[nodiscard]] static std::vector<ValidationError>
  ValidateRoleConsistency(const EffectiveConfiguration& config);

  /// Helper to create error with consistent formatting
  [[nodiscard]] static ValidationError
  MakeError(ValidationErrorCode code, const std::string& message,
            const std::string& field = "", const std::string& expected = "",
            const std::string& actual = "");
};

}  // namespace dsp::fhss
