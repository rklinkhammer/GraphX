#include "dsp/fhss/FHSSGraphXPackets.hpp"

#include <gtest/gtest.h>

#include <complex>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using namespace dsp::fhss;

TEST(FHSSGraphXPacketContractTest, DefinesEveryTargetEdgePacketContract) {
  static_assert(std::is_default_constructible_v<FHSSSyntheticIqOutputPacket>);
  static_assert(std::is_default_constructible_v<FHSSDetectedPulseEvidencePacket>);
  static_assert(
      std::is_default_constructible_v<FHSSPulseCandidateEvidencePacket>);
  static_assert(std::is_default_constructible_v<FHSSCpsmBranchMetricPacket>);
  static_assert(std::is_default_constructible_v<FHSSCpsmSymbolDecisionPacket>);
  static_assert(std::is_default_constructible_v<FHSSDecodedPulseWordPacket>);
  static_assert(std::is_default_constructible_v<FHSSAssembledMessagePacket>);
  static_assert(std::is_default_constructible_v<FHSSDiagnosticsPacket>);

  ASSERT_EQ(kFHSSGraphXEdgeContracts.size(), 8u);
  for (const auto &contract : kFHSSGraphXEdgeContracts) {
    EXPECT_NE(contract.edge_name, nullptr);
    EXPECT_NE(contract.packet_type_name, nullptr);
    EXPECT_NE(std::string_view(contract.edge_name), "");
    EXPECT_NE(std::string_view(contract.packet_type_name), "");
    EXPECT_TRUE(contract.future_accel_sidecar_compatible);
  }
}

TEST(FHSSGraphXPacketContractTest,
     ComplexEvidenceUsesExplicitSharedOwnershipAndRange) {
  auto samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          std::vector<std::complex<double>>{{1.0, 0.0}, {0.0, 1.0},
                                            {-1.0, 0.0}, {0.0, -1.0}});

  FHSSGraphXComplexEvidence evidence{};
  evidence.host_complex64_samples = samples;
  evidence.sample_offset = 1;
  evidence.sample_count = 2;
  evidence.residency = FHSSGraphXPayloadResidency::HostSharedImmutable;
  evidence.sample_time_map.input_packet_global_start_sample = 1000;

  EXPECT_TRUE(FHSSGraphXEvidenceRangeIsValid(evidence));
  EXPECT_TRUE(FHSSGraphXEvidenceHasHostComplexIq(evidence));
  EXPECT_EQ(evidence.host_complex64_samples, samples);

  evidence.sample_offset = 3;
  evidence.sample_count = 2;
  EXPECT_FALSE(FHSSGraphXEvidenceRangeIsValid(evidence));
}

TEST(FHSSGraphXPacketContractTest,
     PreservesGlobalTimingFrequencyMetadataAndSampleMapping) {
  FHSSDetectedPulse detected{};
  detected.global_start_sample = 6500;
  detected.global_end_sample = 9700;
  detected.duration_samples = FHSSProtocolConstants::kPulseWidthSamples;
  detected.channel_start_sample = 42;
  detected.channel_id = 3;
  detected.frequency_index = 17;
  detected.rf_frequency_hz = 1'136'000'000.0;
  detected.iq_offset_frequency_hz = -24'000'000.0;
  detected.estimated_center_frequency_hz = -23'999'500.0;
  detected.frequency_error_hz = 500.0;
  detected.confidence = 0.875;

  FHSSGraphXSampleTimeMap map{};
  map.input_packet_global_start_sample = 6400;
  map.output_start_sample = 10;
  map.decimation_factor = 2;
  map.group_delay_input_samples = 4;
  map.output_sample_rate_hz = 250'000'000.0;

  auto metadata = FHSSGraphXPulseMetadataFromDetectedPulse(detected, map);
  FHSSGraphXPulseCandidate candidate{};
  candidate.pulse = metadata;
  candidate.provisional_slot_index = 1;

  FHSSPulseCandidateEvidencePacket packet{};
  packet.ordered_candidates.push_back(candidate);
  packet.globally_ordered = true;

  ASSERT_EQ(packet.ordered_candidates.size(), 1u);
  const auto &round_trip = packet.ordered_candidates.front().pulse;
  EXPECT_EQ(round_trip.timing.global_start_sample, 6500u);
  EXPECT_EQ(round_trip.timing.duration_samples,
            FHSSProtocolConstants::kPulseWidthSamples);
  EXPECT_EQ(round_trip.timing.channel_start_sample, 42u);
  EXPECT_EQ(round_trip.timing.channel_id, 3u);
  EXPECT_EQ(round_trip.timing.sample_time_map.input_packet_global_start_sample,
            6400u);
  EXPECT_EQ(round_trip.timing.sample_time_map.decimation_factor, 2u);
  EXPECT_DOUBLE_EQ(round_trip.frequency.rf_frequency_hz, 1'136'000'000.0);
  EXPECT_DOUBLE_EQ(round_trip.frequency.iq_offset_frequency_hz,
                   -24'000'000.0);
  EXPECT_DOUBLE_EQ(round_trip.frequency.estimated_center_frequency_hz,
                   -23'999'500.0);
  EXPECT_DOUBLE_EQ(round_trip.frequency.frequency_error_hz, 500.0);
}

TEST(FHSSGraphXPacketContractTest,
     FutureAccelSidecarBoundaryIsDocumentedWithoutGpuExecution) {
  FHSSFutureAccelSidecarContract contract{};
  EXPECT_EQ(contract.residency, FHSSGraphXPayloadResidency::FutureAccelTokenSidecar);
  EXPECT_TRUE(contract.carries_same_semantic_metadata);
  EXPECT_FALSE(contract.cpu_execution_required_by_contract);
  EXPECT_FALSE(contract.gpu_execution_added_by_pr7a);
  EXPECT_NE(std::string_view(contract.boundary).find("Future accelerator tokens"),
            std::string_view::npos);

  FHSSGraphXComplexEvidence evidence{};
  evidence.residency = FHSSGraphXPayloadResidency::FutureAccelTokenSidecar;
  evidence.sample_count = FHSSProtocolConstants::kPulseWidthSamples;
  evidence.sample_time_map.input_packet_global_start_sample = 1234;

  EXPECT_TRUE(FHSSGraphXEvidenceIsFutureAccelSidecarCompatible(evidence));
  EXPECT_FALSE(FHSSGraphXEvidenceHasHostComplexIq(evidence));
}

TEST(FHSSGraphXPacketContractTest,
     DecoderDecisionContractsDoNotRequireTruthMetadata) {
  FHSSCpsmBranchMetricPacket branch_metrics{};
  FHSSCpsmSymbolDecisionPacket symbol_decisions{};
  FHSSDecodedPulseWordPacket decoded_word{};

  branch_metrics.candidate.pulse.timing.global_start_sample = 0;
  symbol_decisions.symbols = std::vector<double>(32, 1.0);
  decoded_word.decoded_value = 0;

  EXPECT_FALSE(FHSSGraphXDecisionContractsRequireTruthMetadata(
      branch_metrics, symbol_decisions, decoded_word));

  FHSSSyntheticIqOutputPacket synthetic{};
  synthetic.truth_pulses.push_back(FHSSTruthPulse{});
  EXPECT_TRUE(synthetic.truth_is_validation_only);

  FHSSAssembledMessagePacket message{};
  message.truth_mismatches.push_back(FHSSGraphXTruthMismatch{
      .pulse_index = 0,
      .kind = FHSSGraphXTruthMismatchKind::Value,
      .message = "fixture value mismatch"});
  EXPECT_TRUE(message.truth_is_validation_only);
}

TEST(FHSSGraphXPacketContractTest,
     DiagnosticsCarryMinimumFieldsWithoutOwningDecoderTruth) {
  FHSSDiagnosticsPacket diagnostics{};
  diagnostics.pulse_count = 7;
  diagnostics.rejected_count = 1;
  diagnostics.preamble_lock = true;
  diagnostics.truth_mismatch_count = 2;
  diagnostics.global_start_sample = 6500;
  diagnostics.frequency_index = 12;
  diagnostics.confidence = 0.99;
  diagnostics.viterbi_path_metric = 0.25;
  diagnostics.decoded_value = 0x12345678u;

  EXPECT_EQ(diagnostics.pulse_count, 7u);
  EXPECT_EQ(diagnostics.rejected_count, 1u);
  EXPECT_TRUE(diagnostics.preamble_lock);
  EXPECT_EQ(diagnostics.truth_mismatch_count, 2u);
  EXPECT_EQ(*diagnostics.global_start_sample, 6500u);
  EXPECT_EQ(*diagnostics.frequency_index, 12u);
  EXPECT_DOUBLE_EQ(*diagnostics.confidence, 0.99);
  EXPECT_DOUBLE_EQ(*diagnostics.viterbi_path_metric, 0.25);
  EXPECT_EQ(*diagnostics.decoded_value, 0x12345678u);
  EXPECT_TRUE(diagnostics.truth_is_validation_only);
}

} // namespace
