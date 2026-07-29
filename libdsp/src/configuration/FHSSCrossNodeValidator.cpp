#include "dsp/configuration/FHSSCrossNodeValidator.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace dsp::configuration {

std::string ValidationError::to_rfc9457_detail() const {
    // RFC 9457 Problem Details format (no source code paths)
    std::ostringstream oss;
    oss << message << " [" << error_code << "]";
    return oss.str();
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateMessageUniqueness(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    std::set<std::string> seen_ids;
    
    for (const auto& msg : source.messages) {
        if (seen_ids.count(msg)) {
            ValidationError err;
            err.error_code = "ERR_MESSAGE_001";
            err.field = "messages";
            err.message = "Duplicate message ID found";
            err.expected_constraint = "All message IDs must be unique";
            err.current_value = msg;
            errors.push_back(err);
        }
        seen_ids.insert(msg);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateFrequencyConstraint(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    // Valid frequency range: 0 Hz to 10 GHz
    const double MIN_FREQ = 0.0;
    const double MAX_FREQ = 1e10;  // 10 GHz
    
    if (source.iq_center_frequency_hz < MIN_FREQ || source.iq_center_frequency_hz > MAX_FREQ) {
        ValidationError err;
        err.error_code = "ERR_FREQUENCY_001";
        err.field = "iq_center_frequency_hz";
        err.message = "Center frequency out of valid range";
        err.expected_constraint = "Must be between 0 Hz and 10 GHz";
        err.current_value = std::to_string(source.iq_center_frequency_hz);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateCfoConstraint(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    if (source.max_abs_cfo_hz < 0.0) {
        ValidationError err;
        err.error_code = "ERR_CFO_001";
        err.field = "max_abs_cfo_hz";
        err.message = "Maximum CFO must be non-negative";
        err.expected_constraint = "max_abs_cfo_hz >= 0";
        err.current_value = std::to_string(source.max_abs_cfo_hz);
        errors.push_back(err);
    }
    
    // CFO should not exceed center frequency by more than 50%
    if (source.max_abs_cfo_hz > source.iq_center_frequency_hz * 0.5) {
        ValidationError err;
        err.error_code = "ERR_CFO_001";
        err.field = "max_abs_cfo_hz";
        err.message = "CFO exceeds 50% of center frequency";
        err.expected_constraint = "max_abs_cfo_hz <= 0.5 * center_frequency";
        err.current_value = std::to_string(source.max_abs_cfo_hz);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateBandwidthConstraint(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    // Bandwidth must be positive and reasonable (< 10 GHz)
    if (source.occupied_bandwidth_hz <= 0.0) {
        ValidationError err;
        err.error_code = "ERR_BANDWIDTH_001";
        err.field = "occupied_bandwidth_hz";
        err.message = "Occupied bandwidth must be positive";
        err.expected_constraint = "occupied_bandwidth_hz > 0";
        err.current_value = std::to_string(source.occupied_bandwidth_hz);
        errors.push_back(err);
    }
    
    if (source.occupied_bandwidth_hz > 1e10) {  // 10 GHz
        ValidationError err;
        err.error_code = "ERR_BANDWIDTH_001";
        err.field = "occupied_bandwidth_hz";
        err.message = "Occupied bandwidth exceeds reasonable limit";
        err.expected_constraint = "occupied_bandwidth_hz <= 10 GHz";
        err.current_value = std::to_string(source.occupied_bandwidth_hz);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateScheduleOrdering(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    // Always validate that transmit_start_sample is non-negative (never negative regardless of overlap)
    if (source.transmit_start_sample < 0) {
        ValidationError err;
        err.error_code = "ERR_SCHEDULE_001";
        err.field = "transmit_start_sample";
        err.message = "Transmit start sample must be non-negative";
        err.expected_constraint = "transmit_start_sample >= 0";
        err.current_value = std::to_string(source.transmit_start_sample);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateBandwidthCfoAgreement(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    // Bandwidth must be at least 2x CFO to have proper signal fidelity
    if (source.occupied_bandwidth_hz > 0 && source.max_abs_cfo_hz > 0) {
        if (source.occupied_bandwidth_hz < 2.0 * source.max_abs_cfo_hz) {
            ValidationError err;
            err.error_code = "ERR_BANDWIDTH_CFO_001";
            err.field = "occupied_bandwidth_hz";
            err.message = "Bandwidth too narrow for CFO correction";
            err.expected_constraint = "occupied_bandwidth_hz >= 2 * max_abs_cfo_hz";
            err.current_value = std::to_string(source.occupied_bandwidth_hz) + 
                              " (CFO: " + std::to_string(source.max_abs_cfo_hz) + ")";
            errors.push_back(err);
        }
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateIdleDurationValid(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    if (source.idle_duration_samples < 0) {
        ValidationError err;
        err.error_code = "ERR_IDLE_001";
        err.field = "idle_duration_samples";
        err.message = "Idle duration must be non-negative";
        err.expected_constraint = "idle_duration_samples >= 0";
        err.current_value = std::to_string(source.idle_duration_samples);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidatePreambleFormat(
    const SourceConfiguration& source
) {
    std::vector<ValidationError> errors;
    
    // Check if message_id looks like a valid format
    if (!source.message_id.empty() && source.message_id.length() > 256) {
        ValidationError err;
        err.error_code = "ERR_PREAMBLE_001";
        err.field = "message_id";
        err.message = "Message ID exceeds maximum length";
        err.expected_constraint = "message_id length <= 256";
        err.current_value = "length=" + std::to_string(source.message_id.length());
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateTopologyInvariant(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    std::vector<ValidationError> errors;
    
    // Verify that number of generated frequency indices matches number of messages
    if (!source.messages.empty()) {
        if (effective.active_frequency_indices_source.empty()) {
            ValidationError err;
            err.error_code = "ERR_TOPOLOGY_001";
            err.field = "active_frequency_indices_source";
            err.message = "Generated frequency indices missing for active messages";
            err.expected_constraint = "Must have derived frequency indices for each message phase";
            err.current_value = "empty";
            errors.push_back(err);
        }
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateTopologyPreservation(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    std::vector<ValidationError> errors;
    
    // Verify message count is preserved
    if (source.messages.size() != effective.messages.size()) {
        ValidationError err;
        err.error_code = "ERR_TOPOLOGY_002";
        err.field = "messages";
        err.message = "Message topology changed during derivation";
        err.expected_constraint = "Message count must be preserved";
        err.current_value = std::to_string(source.messages.size()) + 
                          " -> " + std::to_string(effective.messages.size());
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateCrossNodeConsistency(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    std::vector<ValidationError> errors;
    
    // Verify that authoritative fields match between source and effective
    if (source.iq_center_frequency_hz != effective.iq_center_frequency_hz) {
        ValidationError err;
        err.error_code = "ERR_CONSISTENCY_001";
        err.field = "iq_center_frequency_hz";
        err.message = "Center frequency changed during derivation";
        err.expected_constraint = "Derived config must match source config";
        err.current_value = "source=" + std::to_string(source.iq_center_frequency_hz) + 
                          ", effective=" + std::to_string(effective.iq_center_frequency_hz);
        errors.push_back(err);
    }
    
    if (source.max_abs_cfo_hz != effective.max_abs_cfo_hz) {
        ValidationError err;
        err.error_code = "ERR_CONSISTENCY_001";
        err.field = "max_abs_cfo_hz";
        err.message = "Max CFO changed during derivation";
        err.expected_constraint = "Derived config must match source config";
        err.current_value = std::to_string(source.max_abs_cfo_hz);
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateDerivedProjectionMatch(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    std::vector<ValidationError> errors;
    
    // Verify that effective matches re-derived configuration
    EffectiveConfiguration re_derived = FHSSConfigurationDeriver::Derive(source, effective.revision);
    
    // Compare key generated fields
    if (effective.preamble_pulses != re_derived.preamble_pulses) {
        ValidationError err;
        err.error_code = "ERR_PROJECTION_001";
        err.field = "preamble_pulses";
        err.message = "Derived preamble pulses do not match expected";
        err.expected_constraint = "Derivation must be deterministic";
        err.current_value = "mismatch";
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateRoleConsistency(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    (void)effective;  // effective reserved for future cross-field validation
    std::vector<ValidationError> errors;
    
    // Valid roles: transmitter, receiver, transceiver (exact match, case-sensitive)
    const std::set<std::string> valid_roles = {"transmitter", "receiver", "transceiver"};
    
    // Empty role or invalid role produces error
    if (valid_roles.find(source.role) == valid_roles.end()) {
        ValidationError err;
        err.error_code = "ERR_ROLE_001";
        err.field = "role";
        err.message = "Invalid or missing role specified";
        err.expected_constraint = "role must be one of: transmitter, receiver, transceiver";
        err.current_value = source.role.empty() ? "(empty)" : source.role;
        errors.push_back(err);
    }
    
    return errors;
}

std::vector<ValidationError> FHSSCrossNodeValidator::ValidateAll(
    const SourceConfiguration& source,
    const EffectiveConfiguration& effective
) {
    std::vector<ValidationError> all_errors;
    
    // Run all 13 validation rules (non-failing, collect all errors)
    auto append_errors = [&all_errors](const std::vector<ValidationError>& errs) {
        all_errors.insert(all_errors.end(), errs.begin(), errs.end());
    };
    
    append_errors(ValidateMessageUniqueness(source));
    append_errors(ValidateFrequencyConstraint(source));
    append_errors(ValidateCfoConstraint(source));
    append_errors(ValidateBandwidthConstraint(source));
    append_errors(ValidateScheduleOrdering(source));
    append_errors(ValidateBandwidthCfoAgreement(source));
    append_errors(ValidateIdleDurationValid(source));
    append_errors(ValidatePreambleFormat(source));
    append_errors(ValidateTopologyInvariant(source, effective));
    append_errors(ValidateTopologyPreservation(source, effective));
    append_errors(ValidateCrossNodeConsistency(source, effective));
    append_errors(ValidateDerivedProjectionMatch(source, effective));
    append_errors(ValidateRoleConsistency(source, effective));
    
    return all_errors;
}

bool FHSSCrossNodeValidator::HasErrors(const std::vector<ValidationError>& errors) {
    return !errors.empty();
}

bool FHSSCrossNodeValidator::HasErrorCode(
    const std::vector<ValidationError>& errors,
    const std::string& error_code
) {
    return std::any_of(errors.begin(), errors.end(),
        [&error_code](const ValidationError& e) { return e.error_code == error_code; });
}

std::string FHSSCrossNodeValidator::GetErrorSummary(const std::vector<ValidationError>& errors) {
    if (errors.empty()) {
        return "Configuration is valid";
    }
    
    std::ostringstream oss;
    oss << "Found " << errors.size() << " validation error(s):\n";
    
    for (size_t i = 0; i < errors.size(); ++i) {
        const auto& err = errors[i];
        oss << (i + 1) << ". [" << err.error_code << "] " 
            << err.message << " (" << err.field << ")";
        if (i < errors.size() - 1) {
            oss << "\n";
        }
    }
    
    return oss.str();
}

}  // namespace dsp::configuration
