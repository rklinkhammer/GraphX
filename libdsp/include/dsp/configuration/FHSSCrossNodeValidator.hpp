#pragma once

#include <vector>
#include <string>
#include <optional>

namespace dsp::configuration {

// Forward declarations
struct SourceConfiguration;
struct EffectiveConfiguration;

/// @brief Validation error with stable error code and clear message
struct ValidationError {
    std::string error_code;        // e.g., "ERR_MESSAGE_001", "ERR_FREQUENCY_001"
    std::string field;              // Affected field name
    std::string message;            // Clear, actionable error message (no code paths)
    std::string expected_constraint; // What the constraint was
    std::string current_value;      // Current value that violated constraint

    std::string to_rfc9457_detail() const;
};

/// @brief FHSSCrossNodeValidator - Enforces 13 semantic validation rules
/// 
/// Validation Rules (all checked, non-failing):
/// 1. ERR_TOPOLOGY_001 - Topology Invariant: Graph connectivity must be preserved
/// 2. ERR_MESSAGE_001 - Message Uniqueness: No duplicate message IDs
/// 3. ERR_PREAMBLE_001 - Preamble Format: Preamble must follow format spec
/// 4. ERR_FREQUENCY_001 - Frequency Constraint: Frequencies must be within valid range
/// 5. ERR_CFO_001 - CFO Constraint: CFO must be ≤ max_abs_cfo_hz
/// 6. ERR_BANDWIDTH_001 - Bandwidth Constraint: Occupied bandwidth must be reasonable
/// 7. ERR_SCHEDULE_001 - Schedule Ordering: Messages must not overlap (if !allow_overlap)
/// 8. ERR_CONSISTENCY_001 - Cross-Node Consistency: All nodes must agree on message schema
/// 9. ERR_BANDWIDTH_CFO_001 - Bandwidth/CFO Agreement: CFO and bandwidth must be compatible
/// 10. ERR_PROJECTION_001 - Derived Projection Match: Derived config must match expected
/// 11. ERR_TOPOLOGY_002 - Topology Preservation: Reconfiguration must not lose nodes
/// 12. ERR_IDLE_001 - Idle Duration Valid: Idle duration must be positive
/// 13. ERR_ROLE_001 - Role Consistency: Role assignment must be valid for topology
class FHSSCrossNodeValidator {
public:
    /// @brief Validate configuration against all 13 rules
    /// @param source Authoritative configuration
    /// @param effective Derived effective configuration
    /// @return Vector of ValidationError (empty if valid)
    static std::vector<ValidationError> ValidateAll(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );

    /// @brief Validate a single rule
    /// @return Empty vector if valid, vector with error if invalid
    static std::vector<ValidationError> ValidateMessageUniqueness(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateFrequencyConstraint(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateCfoConstraint(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateBandwidthConstraint(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateScheduleOrdering(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateBandwidthCfoAgreement(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateIdleDurationValid(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidatePreambleFormat(const SourceConfiguration& source);
    static std::vector<ValidationError> ValidateTopologyInvariant(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );
    static std::vector<ValidationError> ValidateTopologyPreservation(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );
    static std::vector<ValidationError> ValidateCrossNodeConsistency(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );
    static std::vector<ValidationError> ValidateDerivedProjectionMatch(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );
    static std::vector<ValidationError> ValidateRoleConsistency(
        const SourceConfiguration& source,
        const EffectiveConfiguration& effective
    );

    /// @brief Check if any errors exist in the result
    static bool HasErrors(const std::vector<ValidationError>& errors);

    /// @brief Check if specific error code exists
    static bool HasErrorCode(
        const std::vector<ValidationError>& errors,
        const std::string& error_code
    );

    /// @brief Get human-readable summary of all errors (no code paths)
    static std::string GetErrorSummary(const std::vector<ValidationError>& errors);
};

}  // namespace dsp::configuration
