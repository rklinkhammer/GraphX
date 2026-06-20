// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

namespace {

using dsp::fhss::FHSSDecodeConfig;
using dsp::fhss::FHSSFrequencyConfig;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
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
  config.decode_config.payload_random.rng_seed = 0x5eedu;
  config.decode_config.payload_random.deterministic = true;
  config.payload_values = {0x0102'0304u, 0xA5A5'5A5Au, 0xFFFF'0000u};
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

  const auto &first_payload = fixture->truth_pulses[16];
  EXPECT_EQ(first_payload.global_start_sample,
            16u * FHSSProtocolConstants::kPulsePeriodSamples);
  EXPECT_EQ(first_payload.duration_samples,
            FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_FALSE(first_payload.is_preamble);
  EXPECT_EQ(first_payload.value, 0x0102'0304u);
}

TEST(FHSSSyntheticIqGeneratorTest, PayloadFrequencySelectionIsDeterministic) {
  const auto first =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());
  const auto second =
      dsp::fhss::GenerateSyntheticIqFixture(ValidGeneratorConfig());

  ASSERT_TRUE(first.has_value()) << first.error().message;
  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_EQ(first->payload_frequency_indices,
            second->payload_frequency_indices);
  ASSERT_EQ(first->payload_frequency_indices.size(), 3u);

  for (const auto index : first->payload_frequency_indices) {
    EXPECT_TRUE(dsp::fhss::ContainsIndex(ValidActiveFrequencies(), index));
    EXPECT_FALSE(dsp::fhss::IsReservedFrequencyIndex(index));
  }
  for (std::size_t i = 0; i < first->payload_frequency_indices.size(); ++i) {
    EXPECT_EQ(first->truth_pulses[16u + i].frequency_index,
              first->payload_frequency_indices[i]);
  }
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
  config.decode_config.preamble_pulses[4].word_value ^= 0x1u;

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
  config.payload_values.assign(241, 0x1234'5678u);

  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(config);

  ASSERT_FALSE(fixture.has_value());
  EXPECT_EQ(fixture.error().code, FHSSValidationCode::InvalidMessageLength);
}

} // namespace
