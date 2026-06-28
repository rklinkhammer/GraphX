// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

#include "dsp/fhss/FHSSPulseMerge.hpp"

namespace {

using dsp::fhss::FHSSComplexEvidence;
using dsp::fhss::FHSSLocalPulseDetection;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSPulseMergeConfig;
using dsp::fhss::FHSSPulseMergeKernel;
using dsp::fhss::FHSSPulseMergeRejectReason;
using dsp::fhss::FHSSSampleTimeMap;
using dsp::fhss::FHSSValidationCode;

std::shared_ptr<const std::vector<std::complex<double>>> EvidenceSamples() {
  return std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(
          FHSSProtocolConstants::kPulseWidthSamples, {1.0, 0.0}));
}

FHSSLocalPulseDetection LocalDetection(std::uint64_t input_global_start_sample,
                                       std::uint64_t local_start_offset,
                                       std::uint32_t frequency_index,
                                       double confidence,
                                       double snr_db = 10.0) {
  FHSSLocalPulseDetection detection{};
  detection.sample_time_map.input_packet_global_start_sample =
      input_global_start_sample;
  detection.local_start_offset = local_start_offset;
  detection.duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
  detection.channel_id = frequency_index;
  detection.frequency_index = frequency_index;
  detection.rf_frequency_hz =
      1'000'000'000.0 + static_cast<double>(frequency_index) * 8'000'000.0;
  detection.iq_offset_frequency_hz =
      static_cast<double>(frequency_index) * 1'000'000.0;
  detection.estimated_center_frequency_hz = detection.iq_offset_frequency_hz;
  detection.frequency_error_hz = 12.5;
  detection.amplitude = 1.0;
  detection.power_db = -3.0;
  detection.snr_db = snr_db;
  detection.noise_floor_db = -40.0;
  detection.phase_at_start_rad = 0.25;
  detection.phase_slope_rad_per_sample = 0.001;
  detection.cfo_hz = 125.0;
  detection.bandwidth_hz = 5'000'000.0;
  detection.confidence = confidence;
  detection.detector_id = 42;
  detection.packet_sequence = 99;
  detection.complex_evidence = FHSSComplexEvidence{
      .samples = EvidenceSamples(),
      .sample_offset = 0,
      .sample_count = FHSSProtocolConstants::kPulseWidthSamples};
  return detection;
}

TEST(FHSSPulseMergeTest, NormalizesLocalTimingToSharedGlobalSampleDomain) {
  const auto local = LocalDetection(10'000, 123, 7, 0.9);

  const auto normalized = dsp::fhss::NormalizeLocalDetection(local);

  ASSERT_TRUE(normalized.has_value()) << normalized.error().message;
  const auto &pulse = normalized->candidate.detected_pulse;
  EXPECT_EQ(pulse.global_start_sample, 10'123u);
  EXPECT_EQ(pulse.global_end_sample,
            10'123u + FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_EQ(pulse.duration_samples, FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_EQ(pulse.channel_start_sample, 123u);
  EXPECT_EQ(pulse.channel_id, 7u);
  EXPECT_EQ(pulse.frequency_index, 7u);
  EXPECT_DOUBLE_EQ(pulse.rf_frequency_hz, 1'056'000'000.0);
  EXPECT_DOUBLE_EQ(pulse.iq_offset_frequency_hz, 7'000'000.0);
  EXPECT_DOUBLE_EQ(pulse.estimated_center_frequency_hz, 7'000'000.0);
  EXPECT_DOUBLE_EQ(pulse.frequency_error_hz, 12.5);
  EXPECT_DOUBLE_EQ(pulse.amplitude, 1.0);
  EXPECT_DOUBLE_EQ(pulse.power_db, -3.0);
  EXPECT_DOUBLE_EQ(pulse.snr_db, 10.0);
  EXPECT_DOUBLE_EQ(pulse.noise_floor_db, -40.0);
  EXPECT_DOUBLE_EQ(pulse.phase_at_start_rad, 0.25);
  EXPECT_DOUBLE_EQ(pulse.phase_slope_rad_per_sample, 0.001);
  EXPECT_DOUBLE_EQ(pulse.cfo_hz, 125.0);
  EXPECT_DOUBLE_EQ(pulse.bandwidth_hz, 5'000'000.0);
  EXPECT_DOUBLE_EQ(pulse.confidence, 0.9);
  EXPECT_EQ(pulse.detector_id, 42u);
  EXPECT_EQ(pulse.packet_sequence, 99u);
}

TEST(FHSSPulseMergeTest, SupportsFutureDecimatedSampleTimeMapping) {
  auto local = LocalDetection(1'000, 5, 7, 0.9);
  local.sample_time_map.output_start_sample = 10;
  local.sample_time_map.decimation_factor = 4;
  local.sample_time_map.group_delay_input_samples = 8;

  const auto global = dsp::fhss::NormalizeToGlobalStartSample(
      local.sample_time_map, local.local_start_offset);

  ASSERT_TRUE(global.has_value()) << global.error().message;
  EXPECT_EQ(*global, 1'052u);
}

TEST(FHSSPulseMergeTest, RejectsMissingGlobalSampleTimingMetadata) {
  auto local = LocalDetection(1'000, 5, 7, 0.9);
  local.sample_time_map.has_input_global_start_sample = false;

  const auto normalized = dsp::fhss::NormalizeLocalDetection(local);

  ASSERT_FALSE(normalized.has_value());
  EXPECT_EQ(normalized.error().code, FHSSValidationCode::InvalidGlobalTiming);
}

TEST(FHSSPulseMergeTest, RejectsInvalidComplexEvidenceRange) {
  auto local = LocalDetection(1'000, 5, 7, 0.9);
  local.complex_evidence.sample_offset = local.complex_evidence.samples->size();
  local.complex_evidence.sample_count = 1;

  const auto normalized = dsp::fhss::NormalizeLocalDetection(local);

  ASSERT_FALSE(normalized.has_value());
  EXPECT_EQ(normalized.error().code, FHSSValidationCode::InvalidGlobalTiming);
}

TEST(FHSSPulseMergeTest, SortsDetectedPulsesByGlobalStartSample) {
  std::vector<FHSSLocalPulseDetection> detections{
      LocalDetection(10'000, 2 * FHSSProtocolConstants::kPulsePeriodSamples, 12,
                     0.7),
      LocalDetection(10'000, 0, 1, 0.9),
      LocalDetection(10'000, FHSSProtocolConstants::kPulsePeriodSamples, 7,
                     0.8)};

  const auto result = FHSSPulseMergeKernel::Merge(detections);

  ASSERT_EQ(result.ordered_candidates.size(), 3u);
  EXPECT_EQ(
      result.ordered_candidates[0].candidate.detected_pulse.global_start_sample,
      10'000u);
  EXPECT_EQ(
      result.ordered_candidates[1].candidate.detected_pulse.global_start_sample,
      10'000u + FHSSProtocolConstants::kPulsePeriodSamples);
  EXPECT_EQ(
      result.ordered_candidates[2].candidate.detected_pulse.global_start_sample,
      10'000u + 2u * FHSSProtocolConstants::kPulsePeriodSamples);
  EXPECT_TRUE(result.rejections.empty());
}

TEST(FHSSPulseMergeTest, DuplicateDetectionsKeepHigherConfidenceCandidate) {
  auto lower = LocalDetection(1'000, 100, 7, 0.2, 5.0);
  auto higher = LocalDetection(1'000, 110, 7, 0.9, 20.0);

  const auto result = FHSSPulseMergeKernel::Merge({lower, higher});

  ASSERT_EQ(result.ordered_candidates.size(), 1u);
  ASSERT_EQ(result.rejections.size(), 1u);
  EXPECT_EQ(
      result.ordered_candidates.front().candidate.detected_pulse.confidence,
      0.9);
  EXPECT_EQ(result.ordered_candidates.front()
                .candidate.detected_pulse.global_start_sample,
            1'110u);
  EXPECT_EQ(result.rejections.front().reason,
            FHSSPulseMergeRejectReason::DuplicateLowerConfidence);
  EXPECT_DOUBLE_EQ(result.rejections.front()
                       .rejected_candidate.candidate.detected_pulse.confidence,
                   0.2);
}

TEST(FHSSPulseMergeTest, CrossFrequencyCollisionsAreRejectedAsUnsupported) {
  auto first = LocalDetection(2'000, 0, 1, 0.9);
  auto overlapping = LocalDetection(2'000, 100, 7, 0.8);

  const auto result = FHSSPulseMergeKernel::Merge({first, overlapping});

  ASSERT_EQ(result.ordered_candidates.size(), 1u);
  ASSERT_EQ(result.rejections.size(), 1u);
  EXPECT_EQ(result.ordered_candidates.front()
                .candidate.detected_pulse.frequency_index,
            1u);
  EXPECT_EQ(result.rejections.front().reason,
            FHSSPulseMergeRejectReason::UnsupportedOverlap);
  EXPECT_EQ(result.rejections.front()
                .rejected_candidate.candidate.detected_pulse.frequency_index,
            7u);
}

TEST(FHSSPulseMergeTest, AssignsProvisionalAndFinalSlotIndices) {
  FHSSPulseMergeConfig config{};
  config.message_epoch_sample = 10'000;
  auto local = LocalDetection(
      10'000, 3 * FHSSProtocolConstants::kPulsePeriodSamples, 12, 0.8);

  const auto normalized = dsp::fhss::NormalizeLocalDetection(local, config);

  ASSERT_TRUE(normalized.has_value()) << normalized.error().message;
  ASSERT_TRUE(normalized->candidate.provisional_slot_index.has_value());
  ASSERT_TRUE(normalized->candidate.final_slot_index.has_value());
  EXPECT_EQ(*normalized->candidate.provisional_slot_index,
            (10'000u + 3u * FHSSProtocolConstants::kPulsePeriodSamples) /
                FHSSProtocolConstants::kPulsePeriodSamples);
  EXPECT_EQ(*normalized->candidate.final_slot_index, 3u);
}

TEST(FHSSPulseMergeTest, PreservesComplexEvidenceThroughCandidateStream) {
  auto local = LocalDetection(1'000, 0, 1, 0.9);
  const auto evidence = local.complex_evidence.samples;

  const auto result = FHSSPulseMergeKernel::Merge({local});

  ASSERT_EQ(result.ordered_candidates.size(), 1u);
  EXPECT_EQ(result.ordered_candidates.front().complex_evidence.samples,
            evidence);
  EXPECT_EQ(result.ordered_candidates.front().complex_evidence.sample_count,
            FHSSProtocolConstants::kPulseWidthSamples);
}

} // namespace
