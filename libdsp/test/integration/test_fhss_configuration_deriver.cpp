/**
 * @file test_fhss_configuration_deriver.cpp
 * @brief Unit tests for FHSSConfigurationDeriver (25+ tests)
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "dsp/fhss/FHSSConfigurationDeriver.hpp"

using namespace dsp::fhss;
using json = nlohmann::json;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

SourceConfiguration EmptyConfiguration() {
  return SourceConfiguration{};
}

SourceConfiguration SimpleConfiguration() {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 10}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 1000.0;
  return config;
}

SourceConfiguration MultiMessageConfiguration() {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 1}, {"message_id", "msg1"}});
  config.messages.push_back(json{{"frequency_index", 7}, {"message_id", "msg2"}});
  config.messages.push_back(json{{"frequency_index", 12}, {"message_id", "msg3"}});
  config.messages.push_back(json{{"frequency_index", 62}, {"message_id", "msg4"}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 1000.0;
  return config;
}

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: Empty configuration derives empty output") {
  auto config = EmptyConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.empty());
  CHECK(result->preamble_pulses.empty());
  CHECK(result->rf_copies.empty());
}

TEST_CASE("FHSSConfigurationDeriver: Simple single-message configuration") {
  auto config = SimpleConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.size() == 1);
  CHECK(result->active_frequency_indices_source[0] == 10);
}

TEST_CASE("FHSSConfigurationDeriver: Multiple messages derive unique frequencies") {
  auto config = MultiMessageConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.size() == 4);
  // Frequencies should be sorted
  CHECK(result->active_frequency_indices_source[0] == 1);
  CHECK(result->active_frequency_indices_source[1] == 7);
  CHECK(result->active_frequency_indices_source[2] == 12);
  CHECK(result->active_frequency_indices_source[3] == 62);
}

// ============================================================================
// Determinism Tests (Critical)
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: Determinism - Same input produces identical output") {
  auto config = MultiMessageConfiguration();

  auto result1 = FHSSConfigurationDeriver::Derive(config);
  auto result2 = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result1.has_value());
  REQUIRE(result2.has_value());

  // Compare all derived fields
  CHECK(result1->active_frequency_indices_source ==
        result2->active_frequency_indices_source);
  CHECK(result1->active_frequency_indices_preamble ==
        result2->active_frequency_indices_preamble);
  CHECK(result1->preamble_pulses == result2->preamble_pulses);
  CHECK(result1->rf_copies.size() == result2->rf_copies.size());
  CHECK(result1->pulse_frequency_indices_source ==
        result2->pulse_frequency_indices_source);
}

TEST_CASE("FHSSConfigurationDeriver: Determinism - No random variation across 100 runs") {
  auto config = MultiMessageConfiguration();

  std::vector<EffectiveConfiguration> results;
  for (int i = 0; i < 100; ++i) {
    auto result = FHSSConfigurationDeriver::Derive(config);
    REQUIRE(result.has_value());
    results.push_back(result.value());
  }

  // All results should be identical
  for (size_t i = 1; i < results.size(); ++i) {
    CHECK(results[i].active_frequency_indices_source ==
          results[0].active_frequency_indices_source);
    CHECK(results[i].preamble_pulses == results[0].preamble_pulses);
  }
}

// ============================================================================
// Individual Field Derivation Tests
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: DeriveActiveFrequencyIndicesSource") {
  auto config = MultiMessageConfiguration();
  auto result =
      FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesSource(config);

  REQUIRE(result.has_value());
  CHECK(result->size() == 4);
  CHECK(result->at(0) == 1);
  CHECK(result->at(3) == 62);
}

TEST_CASE("FHSSConfigurationDeriver: DeriveActiveFrequencyIndicesPreamble") {
  auto config = MultiMessageConfiguration();
  auto result =
      FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesPreamble(config);

  REQUIRE(result.has_value());
  // Preamble takes every other frequency
  CHECK(result->size() == 2);
  CHECK(result->at(0) == 1);
  CHECK(result->at(1) == 12);
}

TEST_CASE("FHSSConfigurationDeriver: DeriveActiveFrequencyIndicesChannelizer") {
  auto config = MultiMessageConfiguration();
  auto result =
      FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesChannelizer(config);

  REQUIRE(result.has_value());
  // Channelizer uses all active frequencies
  CHECK(result->size() == 4);
}

TEST_CASE("FHSSConfigurationDeriver: DerivePreamblePulses creates pulses for preamble frequencies") {
  auto config = MultiMessageConfiguration();
  auto result = FHSSConfigurationDeriver::DerivePreamblePulses(config);

  REQUIRE(result.has_value());
  CHECK(result->size() > 0);

  // All pulse frequency indices should be in valid range (0-63)
  for (const auto& pulse : *result) {
    CHECK(pulse.frequency_index >= 0);
    CHECK(pulse.frequency_index <= 63);
    CHECK(pulse.pulse_index < result->size());
  }
}

TEST_CASE("FHSSConfigurationDeriver: DeriveRfCopies creates one copy per active frequency") {
  auto config = MultiMessageConfiguration();
  auto result = FHSSConfigurationDeriver::DeriveRfCopies(config);

  REQUIRE(result.has_value());
  CHECK(result->size() == 4);  // Same as active frequency count

  // Check RF frequencies are reasonable
  for (const auto& copy : *result) {
    CHECK(copy.rf_frequency_hz >= 1e9);  // Above 1 GHz
    CHECK(copy.bandwidth_hz > 0);
  }
}

TEST_CASE("FHSSConfigurationDeriver: DeriveImpairmentCopies based on flags") {
  SourceConfiguration config = SimpleConfiguration();
  config.enable_noise = true;
  config.enable_doppler = true;
  config.enable_multipath = true;

  auto result = FHSSConfigurationDeriver::DeriveImpairmentCopies(config);

  REQUIRE(result.has_value());
  CHECK(result->size() == 3);

  // Check impairment types in expected order
  CHECK(result->at(0).impairment_type == "AWGN");
  CHECK(result->at(1).impairment_type == "Doppler");
  CHECK(result->at(2).impairment_type == "Multipath");
}

TEST_CASE("FHSSConfigurationDeriver: DeriveMessageAssemblerConfig") {
  auto config = SimpleConfiguration();
  config.allow_overlap = true;

  auto result = FHSSConfigurationDeriver::DeriveMessageAssemblerConfig(config);

  REQUIRE(result.has_value());
  CHECK(result->max_pulses_per_message == 256);
  CHECK(result->timeout_samples == 10000);
  CHECK(result->allow_partial_assembly == true);
}

TEST_CASE("FHSSConfigurationDeriver: DerivePulseFrequencyIndicesSource") {
  auto config = MultiMessageConfiguration();
  auto result =
      FHSSConfigurationDeriver::DerivePulseFrequencyIndicesSource(config);

  REQUIRE(result.has_value());
  CHECK(result->size() > 0);
  CHECK(result->size() <= 20);
}

TEST_CASE("FHSSConfigurationDeriver: DerivePulseFrequencyIndicesPreamble") {
  auto config = MultiMessageConfiguration();
  auto result =
      FHSSConfigurationDeriver::DerivePulseFrequencyIndicesPreamble(config);

  REQUIRE(result.has_value());
  CHECK(result->size() <= 16);
}

// ============================================================================
// Derivation Consistency Tests
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: RF copies count equals active frequencies count") {
  auto config = MultiMessageConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->rf_copies.size() ==
        result->active_frequency_indices_source.size());
}

TEST_CASE("FHSSConfigurationDeriver: Preamble is subset of source frequencies") {
  auto config = MultiMessageConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_preamble.size() <=
        result->active_frequency_indices_source.size());
}

TEST_CASE("FHSSConfigurationDeriver: Revision starts at 1") {
  auto config = SimpleConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->revision == 1);
}

TEST_CASE("FHSSConfigurationDeriver: ETag format is valid") {
  auto config = SimpleConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->etag.find("Rev:") == 0);
}

// ============================================================================
// Golden Test Dataset Tests
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: Golden dataset - Empty input") {
  auto config = EmptyConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.empty());
  CHECK(result->preamble_pulses.empty());
  CHECK(result->rf_copies.empty());
  CHECK(result->impairment_copies.empty());
}

TEST_CASE("FHSSConfigurationDeriver: Golden dataset - Single frequency") {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 32}});
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 1000.0;

  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.size() == 1);
  CHECK(result->active_frequency_indices_source[0] == 32);
  CHECK(result->rf_copies.size() == 1);
}

TEST_CASE("FHSSConfigurationDeriver: Golden dataset - All active frequencies") {
  SourceConfiguration config;
  for (int i = 1; i <= 62; ++i) {
    config.messages.push_back(json{{"frequency_index", i}});
  }
  config.iq_center_frequency_hz = 1e9;
  config.occupied_bandwidth_hz = 5e6;
  config.max_abs_cfo_hz = 1000.0;

  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->active_frequency_indices_source.size() == 62);
  CHECK(result->rf_copies.size() == 62);
}

TEST_CASE("FHSSConfigurationDeriver: Golden dataset - With all impairments") {
  SourceConfiguration config = SimpleConfiguration();
  config.enable_noise = true;
  config.enable_doppler = true;
  config.enable_multipath = true;

  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->impairment_copies.size() == 3);
}

TEST_CASE("FHSSConfigurationDeriver: Golden dataset - No impairments") {
  SourceConfiguration config = SimpleConfiguration();
  config.enable_noise = false;
  config.enable_doppler = false;
  config.enable_multipath = false;

  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->impairment_copies.empty());
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_CASE("FHSSConfigurationDeriver: Duplicate frequency indices are deduplicated") {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 10}});
  config.messages.push_back(json{{"frequency_index", 10}});
  config.messages.push_back(json{{"frequency_index", 10}});

  auto result = FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesSource(config);

  REQUIRE(result.has_value());
  CHECK(result->size() == 1);
  CHECK(result->at(0) == 10);
}

TEST_CASE("FHSSConfigurationDeriver: Frequency indices are sorted") {
  SourceConfiguration config;
  config.messages.push_back(json{{"frequency_index", 62}});
  config.messages.push_back(json{{"frequency_index", 1}});
  config.messages.push_back(json{{"frequency_index", 32}});
  config.messages.push_back(json{{"frequency_index", 7}});

  auto result = FHSSConfigurationDeriver::DeriveActiveFrequencyIndicesSource(config);

  REQUIRE(result.has_value());
  CHECK(result->size() == 4);
  // Check sorted order
  for (size_t i = 1; i < result->size(); ++i) {
    CHECK(result->at(i) > result->at(i - 1));
  }
}

TEST_CASE("FHSSConfigurationDeriver: Source configuration is preserved in effective") {
  auto config = SimpleConfiguration();
  auto result = FHSSConfigurationDeriver::Derive(config);

  REQUIRE(result.has_value());
  CHECK(result->source.iq_center_frequency_hz == config.iq_center_frequency_hz);
  CHECK(result->source.occupied_bandwidth_hz == config.occupied_bandwidth_hz);
  CHECK(result->source.max_abs_cfo_hz == config.max_abs_cfo_hz);
}
