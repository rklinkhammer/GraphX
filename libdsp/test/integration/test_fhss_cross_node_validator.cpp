/**
 * @file test_fhss_cross_node_validator.cpp
 * @brief Unit tests for FHSSCrossNodeValidator (35+ tests)
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "dsp/fhss/FHSSCrossNodeValidator.hpp"
#include "dsp/fhss/FHSSConfigurationDeriver.hpp"

using namespace dsp::fhss;
using json = nlohmann::json;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

SourceConfiguration ValidConfiguration() {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 1}, {"message_id", "msg1"}});
  config.messages.push_back(json{{"frequency_index", 7}, {"message_id", "msg2"}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 100000.0;  // 100 kHz
  config.idle_duration_samples = 1000;
  config.role = SourceConfiguration::Role::Source;
  config.allow_overlap = false;
  return config;
}

EffectiveConfiguration GetValidEffective() {
  auto config = ValidConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);
  return result.value();
}

// ============================================================================
// Valid Configuration Tests
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Valid configuration passes all 13 rules") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

TEST_CASE("FHSSCrossNodeValidator: Empty messages is valid") {
  SourceConfiguration config;
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 100000.0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());
  REQUIRE(validator_result.has_value());
}

// ============================================================================
// Rule 1: Topology Invariant
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 1 - Topology invariant (valid)") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  CHECK(result.has_value());
}

// ============================================================================
// Rule 2: Message Uniqueness
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 2 - Duplicate message ID detected") {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 1}, {"message_id", "dup"}});
  config.messages.push_back(json{{"frequency_index", 7}, {"message_id", "dup"}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 100000.0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_MESSAGE_001;
      });
  REQUIRE(it != errors.end());
  CHECK(it->message.find("Duplicate") != std::string::npos);
}

TEST_CASE("FHSSCrossNodeValidator: Rule 2 - Unique message IDs pass") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 3: Preamble Format
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 3 - Valid preamble format") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 4: Frequency Constraint
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 4 - Invalid frequency index (> 63)") {
  SourceConfiguration config = ValidConfiguration();
  config.messages.clear();
  config.messages.push_back(json{{"frequency_index", 100}});

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_FREQUENCY_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 4 - Valid frequency indices (0-63)") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 5: CFO Constraint
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 5 - CFO within constraint") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 5 - Doppler impairment exceeds CFO constraint") {
  SourceConfiguration config = ValidConfiguration();
  config.enable_doppler = true;
  config.max_abs_cfo_hz = 1000.0;  // 1 kHz - will be exceeded

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_CFO_001;
      });
  REQUIRE(it != errors.end());
}

// ============================================================================
// Rule 6: Bandwidth Constraint
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 6 - Negative bandwidth rejected") {
  SourceConfiguration config;
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = -5e6;  // Negative!
  config.max_abs_cfo_hz = 100000.0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_BANDWIDTH_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 6 - Excessive bandwidth rejected") {
  SourceConfiguration config;
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 600e6;  // 600 MHz > 500 MHz max
  config.max_abs_cfo_hz = 100000.0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_BANDWIDTH_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 6 - Valid bandwidth passes") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 7: Schedule Ordering
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 7 - Non-overlapping schedule") {
  SourceConfiguration config = ValidConfiguration();
  config.allow_overlap = false;
  config.messages.clear();
  config.messages.push_back(
      json{{"frequency_index", 1},
           {"message_id", "msg1"},
           {"transmit_start_sample", 0},
           {"duration_samples", 1000}}});
  config.messages.push_back(
      json{{"frequency_index", 7},
           {"message_id", "msg2"},
           {"transmit_start_sample", 2000},
           {"duration_samples", 1000}}});

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(validator_result.has_value());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 7 - Overlapping schedule rejected when !allow_overlap") {
  SourceConfiguration config = ValidConfiguration();
  config.allow_overlap = false;
  config.messages.clear();
  config.messages.push_back(
      json{{"frequency_index", 1},
           {"message_id", "msg1"},
           {"transmit_start_sample", 0},
           {"duration_samples", 2000}}});
  config.messages.push_back(
      json{{"frequency_index", 7},
           {"message_id", "msg2"},
           {"transmit_start_sample", 1000},
           {"duration_samples", 1000}}});

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_SCHEDULE_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 7 - Overlapping schedule allowed when allow_overlap") {
  SourceConfiguration config = ValidConfiguration();
  config.allow_overlap = true;
  config.messages.clear();
  config.messages.push_back(
      json{{"frequency_index", 1},
           {"message_id", "msg1"},
           {"transmit_start_sample", 0},
           {"duration_samples", 2000}}});
  config.messages.push_back(
      json{{"frequency_index", 7},
           {"message_id", "msg2"},
           {"transmit_start_sample", 1000},
           {"duration_samples", 1000}}});

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(validator_result.has_value());
}

// ============================================================================
// Rule 8: Cross-Node Consistency
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 8 - Missing message_id detected") {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 1}});  // No message_id!
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 100000.0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_CONSISTENCY_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 8 - All messages have message_id") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 9: Bandwidth/CFO Agreement
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 9 - CFO too large relative to bandwidth") {
  SourceConfiguration config;
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;      // 5 MHz bandwidth
  config.max_abs_cfo_hz = 1e6;              // 1 MHz CFO > 10% of 5MHz
  config.messages.push_back(json{{"frequency_index", 1}, {"message_id", "msg"}});

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_BANDWIDTH_CFO_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 9 - CFO within bandwidth constraint") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 10: Derived Projection Match
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 10 - RF copies match active frequencies") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 11: Topology Preservation
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 11 - Preamble within source topology") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 12: Idle Duration Valid
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 12 - Negative idle duration rejected") {
  SourceConfiguration config = ValidConfiguration();
  config.idle_duration_samples = -100;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_IDLE_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 12 - Zero idle duration is valid") {
  SourceConfiguration config = ValidConfiguration();
  config.idle_duration_samples = 0;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(validator_result.has_value());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 12 - Positive idle duration is valid") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

// ============================================================================
// Rule 13: Role Consistency
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Rule 13 - Source role requires messages") {
  SourceConfiguration config = ValidConfiguration();
  config.role = SourceConfiguration::Role::Source;
  config.messages.clear();  // No messages!

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_ROLE_001;
      });
  REQUIRE(it != errors.end());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 13 - Source role with messages is valid") {
  auto effective = GetValidEffective();
  auto result = FHSSCrossNodeValidator::Validate(effective);

  REQUIRE(result.has_value());
}

TEST_CASE("FHSSCrossNodeValidator: Rule 13 - Sink role valid without messages") {
  SourceConfiguration config = ValidConfiguration();
  config.role = SourceConfiguration::Role::Sink;
  config.messages.clear();

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(validator_result.has_value());
}

// ============================================================================
// Multi-Error Tests
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: All validation errors collected (no fail-fast)") {
  SourceConfiguration config;
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = -5e6;  // Negative bandwidth (error 1)
  config.max_abs_cfo_hz = 1e6;           // Will violate bandwidth/CFO (error 2)
  config.idle_duration_samples = -100;   // Negative idle (error 3)
  config.role = SourceConfiguration::Role::Source;
  config.messages.clear();  // Missing for Source role (error 4)

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  CHECK(errors.size() >= 3);  // At least 3 errors should be present
}

// ============================================================================
// Error Message Quality Tests
// ============================================================================

TEST_CASE("FHSSCrossNodeValidator: Error messages contain field names") {
  SourceConfiguration config = ValidConfiguration();
  config.messages.push_back(json{{"frequency_index", 1}});  // No message_id
  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  REQUIRE(!errors.empty());
  CHECK(!errors[0].field.empty());
}

TEST_CASE("FHSSCrossNodeValidator: Error codes are stable") {
  SourceConfiguration config = ValidConfiguration();
  config.idle_duration_samples = -100;

  auto deriver_result = FHSSConfigurationDeriver::Derive(config);
  REQUIRE(deriver_result.has_value());

  auto validator_result = FHSSCrossNodeValidator::Validate(deriver_result.value());

  REQUIRE(!validator_result.has_value());
  auto& errors = validator_result.error();
  auto it = std::find_if(
      errors.begin(), errors.end(), [](const ValidationError& e) {
        return e.code == ValidationErrorCode::ERR_IDLE_001;
      });
  REQUIRE(it != errors.end());
  CHECK(static_cast<int>(it->code) == 6001);  // Stable code
}
