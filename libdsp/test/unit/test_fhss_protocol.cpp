// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "dsp/fhss/FHSSProtocol.hpp"

namespace {

using dsp::fhss::FHSSDecodeConfig;
using dsp::fhss::FHSSFrequencyConfig;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSValidationCode;

FHSSFrequencyConfig ValidFrequencyConfig() {
  FHSSFrequencyConfig config{};
  config.occupied_bandwidth_hz = 5'000'000.0;
  config.max_abs_cfo_hz = 1'000.0;
  config.iq_offset_frequency_hz[1] = -96'000'000.0;
  config.iq_offset_frequency_hz[7] = -32'000'000.0;
  config.iq_offset_frequency_hz[12] = 32'000'000.0;
  config.iq_offset_frequency_hz[62] = 96'000'000.0;
  return config;
}

std::vector<std::uint32_t> ValidActiveFrequencies() { return {1, 7, 12, 62}; }

std::vector<FHSSPreamblePulseSpec> ValidPreamble() {
  return {
      {1, 0x1111'1111u},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u}, {1, 0x1111'1111u},  {7, 0x7777'7777u},
      {12, 0x1212'1212u}, {62, 0x6262'6262u}, {1, 0x1111'1111u},
      {7, 0x7777'7777u},  {12, 0x1212'1212u}, {62, 0x6262'6262u},
      {1, 0x1111'1111u},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u},
  };
}

FHSSDecodeConfig ValidDecodeConfig() {
  FHSSDecodeConfig config{};
  config.frequency = ValidFrequencyConfig();
  config.active_frequency_indices = ValidActiveFrequencies();
  config.preamble_pulses = ValidPreamble();
  config.payload_frequency_indices = {1, 62, 7, 12, 1};
  config.payload_random.rng_seed = 0x1234u;
  config.payload_random.deterministic = true;
  return config;
}

TEST(FHSSProtocolTest, TimingModelDerivesSelectedFixtureCounts) {
  const auto timing = dsp::fhss::DeriveTimingModel({});

  ASSERT_TRUE(timing.has_value()) << timing.error().message;
  EXPECT_EQ(timing->samples_per_symbol, 100u);
  EXPECT_EQ(timing->pulse_width_samples, 3200u);
  EXPECT_EQ(timing->pulse_gap_samples, 3300u);
  EXPECT_EQ(timing->pulse_period_samples, 6500u);
}

TEST(FHSSProtocolTest, TimingValidationRejectsNonSelectedSampleRate) {
  dsp::fhss::FHSSTimingConfig timing{};
  timing.sample_rate_hz = 80'000'000.0;

  const auto result = dsp::fhss::DeriveTimingModel(timing);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidTiming);
}

TEST(FHSSProtocolTest, FrequencyMapDerivesSixtyFourRfMetadataEntries) {
  const auto map = dsp::fhss::BuildFrequencyMap(ValidFrequencyConfig());

  ASSERT_TRUE(map.has_value()) << map.error().message;
  ASSERT_EQ(map->size(), FHSSProtocolConstants::kFrequencyCount);
  EXPECT_EQ((*map)[0].index, 0u);
  EXPECT_DOUBLE_EQ((*map)[0].rf_frequency_hz, 1'000'000'000.0);
  EXPECT_EQ((*map)[63].index, 63u);
  EXPECT_DOUBLE_EQ((*map)[63].rf_frequency_hz, 1'504'000'000.0);
  EXPECT_DOUBLE_EQ((*map)[7].rf_frequency_hz, 1'056'000'000.0);
  EXPECT_DOUBLE_EQ((*map)[7].iq_offset_frequency_hz, -32'000'000.0);
}

TEST(FHSSProtocolTest, FrequencyConfigRejectsStaleTableShape) {
  FHSSFrequencyConfig config = ValidFrequencyConfig();
  config.frequency_count = 128;

  auto result = dsp::fhss::ValidateFrequencyConfig(config);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidFrequencyCount);

  config = ValidFrequencyConfig();
  config.frequency_spacing_hz = 4'000'000.0;
  result = dsp::fhss::ValidateFrequencyConfig(config);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidFrequencyTable);
}

TEST(FHSSProtocolTest,
     FrequencyIndexValidationRejectsOutsideTableAndReservedEdges) {
  EXPECT_TRUE(dsp::fhss::ValidateFrequencyIndex(0).has_value());
  EXPECT_TRUE(dsp::fhss::ValidateFrequencyIndex(63).has_value());

  auto result = dsp::fhss::ValidateFrequencyIndex(64);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidFrequencyIndex);

  result = dsp::fhss::ValidateSelectableFrequencyIndex(0);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::ReservedFrequencyIndex);

  result = dsp::fhss::ValidateSelectableFrequencyIndex(63);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::ReservedFrequencyIndex);

  EXPECT_TRUE(dsp::fhss::ValidateSelectableFrequencyIndex(1).has_value());
  EXPECT_TRUE(dsp::fhss::ValidateSelectableFrequencyIndex(62).has_value());
}

TEST(FHSSProtocolTest, ActiveSetMustContainFourDistinctSelectableFrequencies) {
  EXPECT_TRUE(
      dsp::fhss::ValidateActiveFrequencySet({1, 7, 12, 62}).has_value());

  auto result = dsp::fhss::ValidateActiveFrequencySet({1, 7, 12});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidActiveFrequencySet);

  result = dsp::fhss::ValidateActiveFrequencySet({1, 7, 12, 12});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidActiveFrequencySet);

  result = dsp::fhss::ValidateActiveFrequencySet({0, 7, 12, 62});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::ReservedFrequencyIndex);
}

TEST(FHSSProtocolTest, PreambleRequiresSixteenEntriesInsideActiveSet) {
  auto preamble = ValidPreamble();
  EXPECT_TRUE(
      dsp::fhss::ValidatePreamblePattern(preamble, ValidActiveFrequencies())
          .has_value());

  preamble.pop_back();
  auto result =
      dsp::fhss::ValidatePreamblePattern(preamble, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidPreambleLength);

  preamble = ValidPreamble();
  preamble[3].frequency_index = 15;
  result =
      dsp::fhss::ValidatePreamblePattern(preamble, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidPreambleFrequency);
}

TEST(FHSSProtocolTest, IdenticalPreambleFrequenciesRequireIdenticalWords) {
  auto preamble = ValidPreamble();
  EXPECT_TRUE(dsp::fhss::ValidatePreambleWordConsistency(preamble).has_value());

  preamble[4].word_value ^= 0x1u;
  const auto result = dsp::fhss::ValidatePreambleWordConsistency(preamble);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::PreambleWordMismatch);
}

TEST(FHSSProtocolTest, PayloadFrequenciesMustComeFromActiveSet) {
  EXPECT_TRUE(dsp::fhss::ValidatePayloadFrequencies({1, 62, 7},
                                                    ValidActiveFrequencies())
                  .has_value());

  auto result =
      dsp::fhss::ValidatePayloadFrequencies({1, 15}, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidPayloadFrequency);

  result =
      dsp::fhss::ValidatePayloadFrequencies({63}, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::ReservedFrequencyIndex);
}

TEST(FHSSProtocolTest, MessageLengthIncludesPreambleAndPayload) {
  EXPECT_TRUE(dsp::fhss::ValidateMessageLength(16, 240).has_value());

  const auto result = dsp::fhss::ValidateMessageLength(16, 241);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidMessageLength);
}

TEST(FHSSProtocolTest, IqOffsetsAreSeparateFromRfMetadataAndGuardedByNyquist) {
  const auto config = ValidFrequencyConfig();
  EXPECT_TRUE(dsp::fhss::ValidateIqOffsets(config, ValidActiveFrequencies())
                  .has_value());

  auto rf_as_iq = config;
  for (const auto index : ValidActiveFrequencies()) {
    rf_as_iq.iq_offset_frequency_hz[index] =
        dsp::fhss::RfFrequencyHz(index, rf_as_iq);
  }

  auto result =
      dsp::fhss::ValidateIqOffsets(rf_as_iq, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidIqOffset);

  auto duplicate = config;
  duplicate.iq_offset_frequency_hz[7] = duplicate.iq_offset_frequency_hz[1];
  result = dsp::fhss::ValidateIqOffsets(duplicate, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::DuplicateIqOffset);

  auto outside_guard = config;
  outside_guard.iq_offset_frequency_hz[1] = 249'000'000.0;
  outside_guard.occupied_bandwidth_hz = 4'000'000.0;
  outside_guard.max_abs_cfo_hz = 1.0;
  result =
      dsp::fhss::ValidateIqOffsets(outside_guard, ValidActiveFrequencies());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidIqOffset);
}

TEST(FHSSProtocolTest, DecodeConfigValidatesCompletePr1FixtureSchema) {
  auto config = ValidDecodeConfig();
  EXPECT_TRUE(dsp::fhss::ValidateDecodeConfig(config).has_value());

  config.payload_random.deterministic = false;
  auto result = dsp::fhss::ValidateDecodeConfig(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidPayloadFrequency);

  config = ValidDecodeConfig();
  config.payload_frequency_indices.assign(241, 1);
  result = dsp::fhss::ValidateDecodeConfig(config);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidMessageLength);
}

} // namespace
