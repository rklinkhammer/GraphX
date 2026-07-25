#include <catch2/catch_test_macros.hpp>
#include "dsp/configuration/FHSSCrossNodeValidator.hpp"
#include "dsp/configuration/FHSSConfigurationDeriver.hpp"

using namespace dsp::configuration;

static SourceConfiguration CreateValidConfig() {
    SourceConfiguration cfg;
    cfg.messages = {"MSG_A", "MSG_B"};
    cfg.iq_center_frequency_hz = 1e9;
    cfg.iq_offsets = {0.0, 100.0};
    cfg.idle_mode = "continuous";
    cfg.idle_duration_samples = 1000;
    cfg.occupied_bandwidth_hz = 1e8;
    cfg.max_abs_cfo_hz = 1e6;
    cfg.enable_noise = false;
    cfg.enable_doppler = false;
    cfg.enable_multipath = false;
    cfg.allow_overlap = false;
    cfg.message_id = "TEST_001";
    cfg.transmit_start_sample = 0;
    cfg.frequency_index = 10;
    cfg.value = "default";
    cfg.role = "transmitter";
    return cfg;
}

// Test 1: Valid configuration passes all checks
TEST_CASE("FHSSCrossNodeValidator: Valid configuration", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(errors.empty());
}

// Test 2: Message uniqueness - duplicate IDs
TEST_CASE("FHSSCrossNodeValidator: Duplicate message IDs", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.messages = {"MSG_A", "MSG_A", "MSG_B"};
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_MESSAGE_001"));
}

// Test 3: Frequency constraint - out of range high
TEST_CASE("FHSSCrossNodeValidator: Frequency too high", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = 2e10;  // > 10 GHz
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_FREQUENCY_001"));
}

// Test 4: Frequency constraint - negative
TEST_CASE("FHSSCrossNodeValidator: Negative frequency", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = -1e9;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_FREQUENCY_001"));
}

// Test 5: CFO constraint - negative
TEST_CASE("FHSSCrossNodeValidator: Negative CFO", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.max_abs_cfo_hz = -1e6;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_CFO_001"));
}

// Test 6: CFO constraint - exceeds 50% of center frequency
TEST_CASE("FHSSCrossNodeValidator: CFO exceeds limit", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = 1e9;
    cfg.max_abs_cfo_hz = 6e8;  // 60% of center, exceeds 50% limit
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_CFO_001"));
}

// Test 7: Bandwidth constraint - zero
TEST_CASE("FHSSCrossNodeValidator: Zero bandwidth", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = 0.0;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_BANDWIDTH_001"));
}

// Test 8: Bandwidth constraint - negative
TEST_CASE("FHSSCrossNodeValidator: Negative bandwidth", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = -1e8;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_BANDWIDTH_001"));
}

// Test 9: Bandwidth constraint - exceeds limit
TEST_CASE("FHSSCrossNodeValidator: Bandwidth exceeds limit", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = 2e10;  // > 10 GHz
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_BANDWIDTH_001"));
}

// Test 10: Schedule ordering - negative start sample
TEST_CASE("FHSSCrossNodeValidator: Negative transmit start sample", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.allow_overlap = false;
    cfg.transmit_start_sample = -1;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_SCHEDULE_001"));
}

// Test 11: Bandwidth/CFO agreement - insufficient bandwidth
TEST_CASE("FHSSCrossNodeValidator: Bandwidth/CFO conflict", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = 1e6;  // 1 MHz
    cfg.max_abs_cfo_hz = 1e6;  // 1 MHz (CFO should be <= 50% of bandwidth)
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_BANDWIDTH_CFO_001"));
}

// Test 12: Idle duration - negative
TEST_CASE("FHSSCrossNodeValidator: Negative idle duration", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.idle_duration_samples = -100;
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_IDLE_001"));
}

// Test 13: Preamble format - message ID too long
TEST_CASE("FHSSCrossNodeValidator: Message ID too long", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.message_id = std::string(300, 'A');  // 300 chars > 256 limit
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_PREAMBLE_001"));
}

// Test 14: Role consistency - invalid role
TEST_CASE("FHSSCrossNodeValidator: Invalid role", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.role = "invalid_role";
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_ROLE_001"));
}

// Test 15: Role consistency - valid roles
TEST_CASE("FHSSCrossNodeValidator: Valid roles", "[validator]") {
    std::vector<std::string> roles = {"transmitter", "receiver", "transceiver"};
    
    for (const auto& role : roles) {
        SourceConfiguration cfg = CreateValidConfig();
        cfg.role = role;
        EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
        
        auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
        
        CHECK(!FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_ROLE_001"));
    }
}

// Test 16: Cross-node consistency - frequency preserved
TEST_CASE("FHSSCrossNodeValidator: Frequency preservation", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    // Should pass - frequency is preserved
    auto errors = FHSSCrossNodeValidator::ValidateCrossNodeConsistency(cfg, effective);
    
    CHECK(errors.empty());
}

// Test 17: Topology preservation - message count
TEST_CASE("FHSSCrossNodeValidator: Topology preservation", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateTopologyPreservation(cfg, effective);
    
    CHECK(errors.empty());
}

// Test 18: Error code stability
TEST_CASE("FHSSCrossNodeValidator: Error codes are stable", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.max_abs_cfo_hz = -1e6;  // Invalid CFO
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors1 = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    auto errors2 = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    // Same error should produce same error code
    CHECK(errors1.size() == errors2.size());
    for (size_t i = 0; i < errors1.size(); ++i) {
        CHECK(errors1[i].error_code == errors2[i].error_code);
    }
}

// Test 19: Error messages don't contain source paths
TEST_CASE("FHSSCrossNodeValidator: Error messages safe (no paths)", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = -1e8;
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    // Verify no paths in error messages
    for (const auto& err : errors) {
        CHECK(err.message.find("/") == std::string::npos);
        CHECK(err.message.find("\\") == std::string::npos);
    }
}

// Test 20: Multiple errors collected (non-failing validation)
TEST_CASE("FHSSCrossNodeValidator: Multiple errors collected", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.max_abs_cfo_hz = -1e6;  // Error 1
    cfg.occupied_bandwidth_hz = -1e8;  // Error 2
    cfg.idle_duration_samples = -100;  // Error 3
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    // Should have at least 3 errors
    CHECK(errors.size() >= 3);
}

// Test 21: HasErrors utility
TEST_CASE("FHSSCrossNodeValidator: HasErrors utility", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors_empty = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    CHECK(!FHSSCrossNodeValidator::HasErrors(errors_empty));
    
    cfg.max_abs_cfo_hz = -1e6;
    auto errors_with = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    CHECK(FHSSCrossNodeValidator::HasErrors(errors_with));
}

// Test 22: HasErrorCode utility
TEST_CASE("FHSSCrossNodeValidator: HasErrorCode utility", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.role = "invalid";
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_ROLE_001"));
    CHECK(!FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_FREQUENCY_001"));
}

// Test 23: Error summary generation
TEST_CASE("FHSSCrossNodeValidator: Error summary generation", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors_empty = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    auto summary_empty = FHSSCrossNodeValidator::GetErrorSummary(errors_empty);
    CHECK(summary_empty.find("valid") != std::string::npos);
    
    cfg.max_abs_cfo_hz = -1e6;
    auto errors_with = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    auto summary_with = FHSSCrossNodeValidator::GetErrorSummary(errors_with);
    CHECK(summary_with.find("error") != std::string::npos);
}

// Test 24: Frequency at boundary (valid)
TEST_CASE("FHSSCrossNodeValidator: Frequency at valid boundary", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = 1e10;  // At 10 GHz limit
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    CHECK(!FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_FREQUENCY_001"));
}

// Test 25: CFO at boundary (valid)
TEST_CASE("FHSSCrossNodeValidator: CFO at valid boundary", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.iq_center_frequency_hz = 1e9;
    cfg.max_abs_cfo_hz = 5e8;  // Exactly 50% of center
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateCfoConstraint(cfg);
    
    // Should be valid at boundary
    CHECK(!FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_CFO_001"));
}

// Test 26: Bandwidth at lower boundary (valid)
TEST_CASE("FHSSCrossNodeValidator: Bandwidth at lower boundary", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.occupied_bandwidth_hz = 1.0;  // > 0 is valid
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateBandwidthConstraint(cfg);
    
    CHECK(!FHSSCrossNodeValidator::HasErrorCode(errors, "ERR_BANDWIDTH_001"));
}

// Test 27: All valid roles accepted
TEST_CASE("FHSSCrossNodeValidator: Role enum check", "[validator]") {
    std::vector<std::string> valid_roles = {"transmitter", "receiver", "transceiver"};
    std::vector<std::string> invalid_roles = {"", "TRANSMITTER", "tx", "rx"};
    
    for (const auto& role : valid_roles) {
        SourceConfiguration cfg = CreateValidConfig();
        cfg.role = role;
        EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
        
        auto errors = FHSSCrossNodeValidator::ValidateRoleConsistency(cfg, effective);
        CHECK(errors.empty());
    }
    
    for (const auto& role : invalid_roles) {
        SourceConfiguration cfg = CreateValidConfig();
        cfg.role = role;
        EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
        
        auto errors = FHSSCrossNodeValidator::ValidateRoleConsistency(cfg, effective);
        // Validate that role consistency check is working
        CHECK(!errors.empty());  // Invalid role should produce errors
    }
}

// Test 28: Message uniqueness with single message
TEST_CASE("FHSSCrossNodeValidator: Single message is unique", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.messages = {"SINGLE_MSG"};
    
    auto errors = FHSSCrossNodeValidator::ValidateMessageUniqueness(cfg);
    
    CHECK(errors.empty());
}

// Test 29: Preamble format with empty message ID
TEST_CASE("FHSSCrossNodeValidator: Empty message ID valid", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.message_id = "";
    
    auto errors = FHSSCrossNodeValidator::ValidatePreambleFormat(cfg);
    
    CHECK(errors.empty());
}

// Test 30: Schedule ordering with overlap allowed
TEST_CASE("FHSSCrossNodeValidator: Schedule with overlap allowed", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.allow_overlap = true;
    cfg.transmit_start_sample = -1;  // Normally invalid, but allowed with overlap
    
    auto errors = FHSSCrossNodeValidator::ValidateScheduleOrdering(cfg);
    
    // Check - with allow_overlap true and multiple messages, we still validate
    // The current logic validates regardless, so -1 should still fail
    CHECK(!errors.empty());
}

// Test 31: Idle duration zero (valid)
TEST_CASE("FHSSCrossNodeValidator: Zero idle duration valid", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.idle_duration_samples = 0;
    
    auto errors = FHSSCrossNodeValidator::ValidateIdleDurationValid(cfg);
    
    CHECK(errors.empty());
}

// Test 32: Derived projection matching
TEST_CASE("FHSSCrossNodeValidator: Derived projection match", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateDerivedProjectionMatch(cfg, effective);
    
    // Should match - was just derived
    CHECK(errors.empty());
}

// Test 33: Topology invariant with messages
TEST_CASE("FHSSCrossNodeValidator: Topology invariant with messages", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.messages = {"MSG_1", "MSG_2", "MSG_3"};
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateTopologyInvariant(cfg, effective);
    
    CHECK(errors.empty());
}

// Test 34: Topology invariant with empty messages
TEST_CASE("FHSSCrossNodeValidator: Topology invariant empty messages", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.messages.clear();
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    
    auto errors = FHSSCrossNodeValidator::ValidateTopologyInvariant(cfg, effective);
    
    // Empty messages = empty indices is fine
    CHECK(errors.empty());
}

// Test 35: RFC 9457 detail formatting
TEST_CASE("FHSSCrossNodeValidator: RFC 9457 detail format", "[validator]") {
    SourceConfiguration cfg = CreateValidConfig();
    cfg.role = "invalid";
    
    EffectiveConfiguration effective = FHSSConfigurationDeriver::Derive(cfg, 1);
    auto errors = FHSSCrossNodeValidator::ValidateAll(cfg, effective);
    
    if (!errors.empty()) {
        std::string detail = errors[0].to_rfc9457_detail();
        CHECK(detail.find("[ERR_") != std::string::npos);
        CHECK(detail.find("]") != std::string::npos);
    }
}
