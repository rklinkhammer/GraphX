// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <vector>

#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

namespace {

using dsp::fhss::FHSSCorrelatorBankDetectorConfig;
using dsp::fhss::FHSSCorrelatorBankDetectorKernel;
using dsp::fhss::FHSSDecodeConfig;
using dsp::fhss::FHSSFrequencyConfig;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSPulseMergeConfig;
using dsp::fhss::FHSSPulseMergeKernel;
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

std::vector<std::uint32_t> ActiveFrequencies() { return {1, 7, 12, 62}; }

std::vector<FHSSPreamblePulseSpec> Preamble() {
  return {
      {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u}, {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},
      {12, 0x1212'1212u}, {62, 0x6262'6262u}, {1, 0xAAAA'AAAAu},
      {7, 0x7777'7777u},  {12, 0x1212'1212u}, {62, 0x6262'6262u},
      {1, 0xAAAA'AAAAu},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u},
  };
}

FHSSDecodeConfig DecodeConfig() {
  FHSSDecodeConfig config{};
  config.frequency = ValidFrequencyConfig();
  config.active_frequency_indices = ActiveFrequencies();
  config.preamble_pulses = Preamble();
  config.payload_random.rng_seed = 0x4242u;
  config.payload_random.deterministic = true;
  return config;
}

FHSSSyntheticIqGeneratorConfig GeneratorConfig() {
  FHSSSyntheticIqGeneratorConfig config{};
  config.decode_config = DecodeConfig();
  config.payload_values = {0x0102'0304u, 0xA5A5'5A5Au};
  return config;
}

FHSSCorrelatorBankDetectorConfig DetectorConfig() {
  FHSSCorrelatorBankDetectorConfig config{};
  config.decode_config = DecodeConfig();
  config.input_packet_global_start_sample = 0;
  config.message_start_sample = 0;
  config.detector_id = 77;
  config.packet_sequence = 123;
  return config;
}

TEST(FHSSCorrelatorBankDetectorTest, RejectsReservedActiveFrequencyConfig) {
  auto config = DetectorConfig();
  config.decode_config.active_frequency_indices = {0, 7, 12, 62};
  config.decode_config.preamble_pulses[0].frequency_index = 0;

  const auto result = dsp::fhss::ValidateCorrelatorBankDetectorConfig(config);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::ReservedFrequencyIndex);
}

TEST(FHSSCorrelatorBankDetectorTest, RequiresKnownPr1MessageStartSample) {
  auto config = DetectorConfig();
  config.message_start_sample = 5;

  const auto result = dsp::fhss::ValidateCorrelatorBankDetectorConfig(config);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::InvalidGlobalTiming);
}

TEST(FHSSCorrelatorBankDetectorTest, RanksCorrectActiveFrequencyPerKnownSlot) {
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(GeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  const auto result = FHSSCorrelatorBankDetectorKernel::Detect(fixture->samples,
                                                             DetectorConfig());

  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->evaluated_frequency_count, 4u);
  ASSERT_EQ(result->local_detections.size(), fixture->truth_pulses.size());
  ASSERT_EQ(result->slot_scores.size(), fixture->truth_pulses.size());

  for (std::size_t i = 0; i < fixture->truth_pulses.size(); ++i) {
    EXPECT_EQ(result->local_detections[i].frequency_index,
              fixture->truth_pulses[i].frequency_index)
        << "slot " << i;
    EXPECT_EQ(result->slot_scores[i].size(), 4u);
  }
}

TEST(FHSSCorrelatorBankDetectorTest, EmitsMetadataAndGlobalTimingForMerge) {
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(GeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  auto detector_config = DetectorConfig();
  detector_config.input_packet_global_start_sample = 50'000;
  const auto result =
      FHSSCorrelatorBankDetectorKernel::Detect(fixture->samples, detector_config);

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_FALSE(result->local_detections.empty());

  const auto &first = result->local_detections.front();
  EXPECT_EQ(first.sample_time_map.input_packet_global_start_sample, 50'000u);
  EXPECT_EQ(first.local_start_offset, 0u);
  EXPECT_EQ(first.duration_samples, FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_EQ(first.frequency_index,
            fixture->truth_pulses.front().frequency_index);
  EXPECT_DOUBLE_EQ(first.rf_frequency_hz,
                   fixture->truth_pulses.front().rf_frequency_hz);
  EXPECT_DOUBLE_EQ(first.iq_offset_frequency_hz,
                   fixture->truth_pulses.front().iq_offset_frequency_hz);
  EXPECT_DOUBLE_EQ(first.estimated_center_frequency_hz,
                   fixture->truth_pulses.front().iq_offset_frequency_hz);
  EXPECT_DOUBLE_EQ(first.frequency_error_hz, 0.0);
  EXPECT_DOUBLE_EQ(first.cfo_hz, 0.0);
  EXPECT_GT(first.snr_db, 0.0);
  EXPECT_GT(first.confidence, 0.0);
  EXPECT_EQ(first.detector_id, 77u);
  EXPECT_EQ(first.packet_sequence, 123u);
}

TEST(FHSSCorrelatorBankDetectorTest, UsesIqOffsetForDehoppedEvidence) {
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(GeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  const auto result = FHSSCorrelatorBankDetectorKernel::Detect(fixture->samples,
                                                             DetectorConfig());

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_FALSE(result->local_detections.empty());

  const auto &first = result->local_detections.front();
  ASSERT_TRUE(first.complex_evidence.samples);
  ASSERT_GE(first.complex_evidence.samples->size(), 2u);
  EXPECT_DOUBLE_EQ(first.iq_offset_frequency_hz, 0.0);
  EXPECT_NE(first.rf_frequency_hz, first.iq_offset_frequency_hz);
  EXPECT_NEAR((*first.complex_evidence.samples)[1].real(),
              fixture->samples[1].real(), 1.0e-12);
  EXPECT_NEAR((*first.complex_evidence.samples)[1].imag(),
              fixture->samples[1].imag(), 1.0e-12);
}

TEST(FHSSCorrelatorBankDetectorTest,
     HandoffToPulseMergePreservesCandidateEvidence) {
  const auto fixture = dsp::fhss::GenerateSyntheticIqFixture(GeneratorConfig());
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  const auto detections = FHSSCorrelatorBankDetectorKernel::Detect(
      fixture->samples, DetectorConfig());
  ASSERT_TRUE(detections.has_value()) << detections.error().message;

  FHSSPulseMergeConfig merge_config{};
  const auto merged =
      FHSSPulseMergeKernel::Merge(detections->local_detections, merge_config);

  ASSERT_EQ(merged.ordered_candidates.size(), fixture->truth_pulses.size());
  ASSERT_TRUE(merged.rejections.empty());
  const auto &first = merged.ordered_candidates.front();
  EXPECT_EQ(first.candidate.detected_pulse.global_start_sample, 0u);
  EXPECT_EQ(first.candidate.detected_pulse.frequency_index,
            fixture->truth_pulses.front().frequency_index);
  ASSERT_TRUE(first.complex_evidence.samples);
  EXPECT_EQ(first.complex_evidence.sample_count,
            FHSSProtocolConstants::kPulseWidthSamples);
}

TEST(FHSSCorrelatorBankDetectorTest, RejectsUnsupportedOverlapConfiguration) {
  auto config = DetectorConfig();
  config.allow_overlap = true;

  const auto result = dsp::fhss::ValidateCorrelatorBankDetectorConfig(config);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, FHSSValidationCode::UnsupportedOverlap);
}

} // namespace
