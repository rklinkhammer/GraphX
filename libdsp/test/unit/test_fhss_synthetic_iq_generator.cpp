// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "config/JsonView.hpp"

#include <nlohmann/json.hpp>

namespace {

using dsp::fhss::FHSSDecodeConfig;
using dsp::fhss::FHSSFrequencyConfig;
using dsp::fhss::FHSSMessagePulseRole;
using dsp::fhss::FHSSMessagePulseSpec;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSScheduledMessageSpec;
using dsp::fhss::FHSSSyntheticIqGeneratorConfig;
using dsp::fhss::FHSSValidationCode;

FHSSFrequencyConfig ValidFrequencyConfig() {
  FHSSFrequencyConfig config{};
  config.occupied_bandwidth_hz = 5'000'000.0;
  config.max_abs_cfo_hz = 1'000.0;
  config.iq_offset_frequency_hz[1] = 0.0;
  config.iq_offset_frequency_hz[7] = -64'000'000.0;
  config.iq_offset_frequency_hz[12] = 48'000'000.0;
  config.iq_offset_frequency_hz[62] = 112'000'000.0;
  return config;
}

std::vector<std::uint32_t> ValidActiveFrequencies() { return {1, 7, 12, 62}; }

std::vector<FHSSPreamblePulseSpec> ValidPreamble() {
  return {
      {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u}, {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},
      {12, 0x1212'1212u}, {62, 0x6262'6262u}, {1, 0xAAAA'AAAAu},
      {7, 0x7777'7777u},  {12, 0x1212'1212u}, {62, 0x6262'6262u},
      {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u},
  };
}

FHSSSyntheticIqGeneratorConfig ValidGeneratorConfig() {
  FHSSSyntheticIqGeneratorConfig config{};
  config.decode_config.frequency = ValidFrequencyConfig();
  config.decode_config.active_frequency_indices = ValidActiveFrequencies();
  config.decode_config.preamble_pulses = ValidPreamble();
  FHSSScheduledMessageSpec message{};
  message.message_id = 42;
  message.transmit_start_sample = 0;
  for (const auto &pulse : ValidPreamble()) {
    message.pulses.push_back(FHSSMessagePulseSpec{
        .frequency_index = pulse.frequency_index,
        .value = pulse.word_value,
        .role = FHSSMessagePulseRole::Preamble});
  }
  message.pulses.push_back(FHSSMessagePulseSpec{
      .frequency_index = 1, .value = 0x0102'0304u, .role = FHSSMessagePulseRole::Body});
  message.pulses.push_back(FHSSMessagePulseSpec{
      .frequency_index = 7, .value = 0xA5A5'5A5Au, .role = FHSSMessagePulseRole::Body});
  message.pulses.push_back(FHSSMessagePulseSpec{
      .frequency_index = 12, .value = 0xFFFF'0000u, .role = FHSSMessagePulseRole::Body});
  config.messages.push_back(std::move(message));
  return config;
}

double WrappedPhaseDiff(std::complex<double> lhs, std::complex<double> rhs) {
  return std::arg(rhs * std::conj(lhs));
}

TEST(FHSSSyntheticIqGeneratorTest, EmitsExpectedSampleCountAndTruthMetadata) {
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());

  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;
  const auto total_pulses = 16u + 3u;
  EXPECT_EQ(fixture->samples.size(),
            total_pulses * FHSSProtocolConstants::kPulsePeriodSamples);
  ASSERT_EQ(fixture->truth_pulses.size(), total_pulses);

  const auto &first = fixture->truth_pulses.front();
  EXPECT_EQ(first.global_start_sample, 0u);
  EXPECT_EQ(first.duration_samples, FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_EQ(first.frequency_index, 1u);
  EXPECT_DOUBLE_EQ(first.rf_frequency_hz, 1'008'000'000.0);
  EXPECT_DOUBLE_EQ(first.iq_offset_frequency_hz, 0.0);
  EXPECT_EQ(first.value, 0xAAAA'AAAAu);
  EXPECT_TRUE(first.is_preamble);
  EXPECT_EQ(first.message_id, 42u);

  const auto &first_payload = fixture->truth_pulses[16];
  EXPECT_EQ(first_payload.global_start_sample,
            16u * FHSSProtocolConstants::kPulsePeriodSamples);
  EXPECT_EQ(first_payload.duration_samples,
            FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_FALSE(first_payload.is_preamble);
  EXPECT_EQ(first_payload.value, 0x0102'0304u);
  EXPECT_EQ(first_payload.frequency_index, 1u);
  EXPECT_EQ(first_payload.message_id, 42u);
}

TEST(FHSSSyntheticIqGeneratorTest, PayloadFrequenciesComeFromMessageSchedule) {
  const auto first =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());
  const auto second =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());

  ASSERT_TRUE(first.has_value()) << first.error().message;
  ASSERT_TRUE(second.has_value()) << second.error().message;
  ASSERT_EQ(first->truth_pulses.size(), second->truth_pulses.size());
  EXPECT_EQ(first->truth_pulses[16].frequency_index, 1u);
  EXPECT_EQ(first->truth_pulses[17].frequency_index, 7u);
  EXPECT_EQ(first->truth_pulses[18].frequency_index, 12u);
  EXPECT_EQ(second->truth_pulses[16].frequency_index, 1u);
  EXPECT_EQ(second->truth_pulses[17].frequency_index, 7u);
  EXPECT_EQ(second->truth_pulses[18].frequency_index, 12u);
}

TEST(FHSSSyntheticIqGeneratorTest, RejectsReservedPreambleFrequencies) {
  auto config = ValidGeneratorConfig();
  config.decode_config.active_frequency_indices = {0, 7, 12, 62};
  config.decode_config.preamble_pulses[0].frequency_index = 0;

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::ReservedFrequencyIndex);
}

TEST(FHSSSyntheticIqGeneratorTest, RejectsPreambleWordMismatch) {
  auto config = ValidGeneratorConfig();
  config.messages.front().pulses[4].value ^= 0x1u;

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::PreambleWordMismatch);
}

TEST(FHSSSyntheticIqGeneratorTest, RejectsUnsupportedImpairmentsAndOverlap) {
  auto config = ValidGeneratorConfig();
  config.enable_noise = true;

  auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);
  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::InvalidTiming);

  config = ValidGeneratorConfig();
  config.allow_overlap = true;
  fixture = dsp::fhss::GenerateSyntheticIqFixture(config);
  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::InvalidTiming);
}

TEST(FHSSSyntheticIqGeneratorTest,
     NoiseFreePulseHasConstantEnvelopeAndZeroGap) {
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  for (std::uint64_t i = 0; i < FHSSProtocolConstants::kPulseWidthSamples;
       ++i) {
    EXPECT_NEAR(std::abs(fixture->samples[i]), 1.0, 1.0e-12);
  }
  for (std::uint64_t i = FHSSProtocolConstants::kPulseWidthSamples;
       i < FHSSProtocolConstants::kPulsePeriodSamples; ++i) {
    EXPECT_DOUBLE_EQ(fixture->samples[i].real(), 0.0);
    EXPECT_DOUBLE_EQ(fixture->samples[i].imag(), 0.0);
  }
}

TEST(FHSSSyntheticIqGeneratorTest,
     UsesIqOffsetNotRfFrequencyInComplexExponential) {
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  const auto sample_in_symbol = 1u;
  const double q = dsp::fhss::RectangularFullResponsePhasePulse(
      sample_in_symbol, FHSSProtocolConstants::kSamplesPerSymbol);
  const double first_symbol = dsp::fhss::CpsmSymbolForBit(0xAAAA'AAAAu, 0);
  const double expected_theta = 2.0 * std::numbers::pi * 0.5 * first_symbol * q;
  const auto expected = std::exp(std::complex<double>(0.0, expected_theta));

  EXPECT_NEAR(fixture->samples[sample_in_symbol].real(), expected.real(),
              1.0e-12);
  EXPECT_NEAR(fixture->samples[sample_in_symbol].imag(), expected.imag(),
              1.0e-12);
}

TEST(FHSSSyntheticIqGeneratorTest, RectangularFullResponsePhaseIsContinuous) {
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  double max_step = 0.0;
  for (std::uint64_t i = 1; i < FHSSProtocolConstants::kPulseWidthSamples;
       ++i) {
    max_step =
        std::max(max_step, std::abs(WrappedPhaseDiff(fixture->samples[i - 1],
                                                     fixture->samples[i])));
  }
  EXPECT_LT(max_step, 0.1);
}

TEST(FHSSSyntheticIqGeneratorTest,
     RejectsMessageLongerThanTwoHundredFiftySixPulses) {
  auto config = ValidGeneratorConfig();
  config.messages.front().pulses.resize(257, FHSSMessagePulseSpec{
                                                 .frequency_index = 1,
                                                 .value = 0x1234'5678u,
                                                 .role = FHSSMessagePulseRole::Body});
  for (std::size_t i = 0; i < 16; ++i) {
    config.messages.front().pulses[i].role = FHSSMessagePulseRole::Preamble;
  }

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::InvalidMessageLength);
}

TEST(FHSSSyntheticIqGeneratorTest, SupportsNonzeroTransmitTimeAndIdleGaps) {
  auto config = ValidGeneratorConfig();
  config.messages.front().transmit_start_sample = 13'000;

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;
  EXPECT_EQ(fixture->truth_pulses.front().global_start_sample, 13'000u);
  ASSERT_GT(fixture->samples.size(), 13'000u);
  for (std::size_t i = 0; i < 13'000u; ++i) {
    EXPECT_DOUBLE_EQ(fixture->samples[i].real(), 0.0);
    EXPECT_DOUBLE_EQ(fixture->samples[i].imag(), 0.0);
  }
}

TEST(FHSSSyntheticIqGeneratorTest, SupportsZeroMessageIdleOutput) {
  auto config = ValidGeneratorConfig();
  config.messages.clear();
  config.idle_duration_samples = 4096;

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;
  EXPECT_EQ(fixture->truth_pulses.size(), 0u);
  ASSERT_EQ(fixture->samples.size(), 4096u);
  for (const auto sample : fixture->samples) {
    EXPECT_DOUBLE_EQ(sample.real(), 0.0);
    EXPECT_DOUBLE_EQ(sample.imag(), 0.0);
  }
}

TEST(FHSSSyntheticIqGeneratorTest, RejectsOverlappingScheduledMessages) {
  auto config = ValidGeneratorConfig();
  auto second = config.messages.front();
  second.message_id = 43;
  second.transmit_start_sample = FHSSProtocolConstants::kPulsePeriodSamples;
  config.messages.push_back(std::move(second));

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::UnsupportedOverlap);
}

TEST(FHSSSyntheticIqGeneratorTest,
     GraphXConfigParsesMessagesAndDerivesIqOffsetsFromCenter) {
  nlohmann::json json{
      {"active_frequency_indices",
       nlohmann::json::array({std::uint32_t{24}, std::uint32_t{28},
                              std::uint32_t{32}, std::uint32_t{36}})},
      {"iq_center_frequency_hz", 1'240'000'000.0},
      {"occupied_bandwidth_hz", 5'000'000.0},
      {"max_abs_cfo_hz", 1'000.0},
      {"idle_mode", "zero"},
      {"idle_duration_samples", std::uint64_t{0}},
      {"messages",
       nlohmann::json::array({nlohmann::json{
           {"message_id", std::uint64_t{99}},
           {"transmit_start_sample", std::uint64_t{6'500}},
           {"pulses", nlohmann::json::array()}}})}};
  const std::vector<FHSSPreamblePulseSpec> preamble{
      {24, 0xAAAA'AAAAu}, {28, 0x7777'7777u}, {32, 0x1212'1212u},
      {36, 0x6262'6262u}, {24, 0xAAAA'AAAAu}, {28, 0x7777'7777u},
      {32, 0x1212'1212u}, {36, 0x6262'6262u}, {24, 0xAAAA'AAAAu},
      {28, 0x7777'7777u}, {32, 0x1212'1212u}, {36, 0x6262'6262u},
      {24, 0xAAAA'AAAAu}, {28, 0x7777'7777u}, {32, 0x1212'1212u},
      {36, 0x6262'6262u}};
  for (const auto &pulse : preamble) {
    nlohmann::json pulse_json;
    pulse_json["frequency_index"] = pulse.frequency_index;
    pulse_json["value"] = pulse.word_value;
    pulse_json["role"] = "preamble";
    json["messages"][0]["pulses"].push_back(std::move(pulse_json));
  }
  nlohmann::json body_json;
  body_json["frequency_index"] = std::uint32_t{24};
  body_json["value"] = std::uint32_t{0x0102'0304u};
  body_json["role"] = "body";
  json["messages"][0]["pulses"].push_back(std::move(body_json));

  const auto config = dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
      graph::JsonView(json));

  ASSERT_EQ(config.messages.size(), 1u);
  EXPECT_EQ(config.messages.front().message_id, 99u);
  EXPECT_EQ(config.messages.front().transmit_start_sample, 6'500u);
  EXPECT_DOUBLE_EQ(config.decode_config.frequency.iq_offset_frequency_hz[24],
                   -48'000'000.0);
  EXPECT_DOUBLE_EQ(config.decode_config.frequency.iq_offset_frequency_hz[28],
                   -16'000'000.0);
  EXPECT_DOUBLE_EQ(config.decode_config.frequency.iq_offset_frequency_hz[32],
                   16'000'000.0);
  EXPECT_DOUBLE_EQ(config.decode_config.frequency.iq_offset_frequency_hz[36],
                   48'000'000.0);

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;
  ASSERT_FALSE(fixture->truth_pulses.empty());
  EXPECT_EQ(fixture->truth_pulses.front().message_id, 99u);
  EXPECT_EQ(fixture->truth_pulses.front().global_start_sample, 6'500u);
}

} // namespace
