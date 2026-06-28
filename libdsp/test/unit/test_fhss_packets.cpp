#include "dsp/fhss/FHSSPackets.hpp"
#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

#include <gtest/gtest.h>

#include <complex>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

using namespace dsp::fhss;

template <typename T> struct IsControlToken : std::false_type {};

template <typename SidecarT>
struct IsControlToken<graph::gpu::accel::ControlToken<SidecarT>>
    : std::true_type {};

template <typename T>
inline constexpr bool IsControlTokenV = IsControlToken<T>::value;

template <typename T>
concept HasTruthPulses = requires(T value) { value.truth_pulses; };

template <typename T>
concept HasTruthMismatches = requires(T value) { value.truth_mismatches; };

template <typename T>
concept HasTruthMismatchCount = requires(T value) {
  value.truth_mismatch_count;
};

TEST(FHSSPacketContractTest, DefinesEveryTargetEdgePacketContract) {
  static_assert(std::is_default_constructible_v<FHSSSyntheticIqOutputPacket>);
  static_assert(std::is_default_constructible_v<FHSSDownconvertedIqPacket>);
  static_assert(std::is_default_constructible_v<FHSSChannelizedIqPacket>);
  static_assert(
      std::is_default_constructible_v<FHSSPerChannelPulseEvidencePacket>);
  static_assert(std::is_default_constructible_v<FHSSDetectedPulseEvidencePacket>);
  static_assert(
      std::is_default_constructible_v<FHSSPulseCandidateEvidencePacket>);
  static_assert(std::is_default_constructible_v<FHSSCpsmBranchMetricPacket>);
  static_assert(std::is_default_constructible_v<FHSSCpsmSymbolDecisionPacket>);
  static_assert(std::is_default_constructible_v<FHSSDecodedPulseWordPacket>);
  static_assert(std::is_default_constructible_v<FHSSDecodedPulseWordsPacket>);
  static_assert(std::is_default_constructible_v<FHSSAssembledMessagePacket>);
  static_assert(std::is_default_constructible_v<FHSSDiagnosticsPacket>);

  ASSERT_EQ(kFHSSGraphXEdgeContracts.size(), 11u);
  for (const auto &contract : kFHSSGraphXEdgeContracts) {
    EXPECT_NE(contract.edge_name, nullptr);
    EXPECT_NE(contract.packet_type_name, nullptr);
    EXPECT_NE(std::string_view(contract.edge_name), "");
    EXPECT_NE(std::string_view(contract.packet_type_name), "");
    EXPECT_TRUE(contract.future_accel_sidecar_compatible);
  }
}

TEST(FHSSPacketContractTest, NewChannelizedEdgesAreAccelTokenSidecars) {
  static_assert(IsControlTokenV<FHSSSyntheticIqToken>);
  static_assert(IsControlTokenV<FHSSDownconvertedIqToken>);
  static_assert(IsControlTokenV<FHSSChannelizedIqToken>);
  static_assert(IsControlTokenV<FHSSPerChannelPulseEvidenceToken>);

  static_assert(std::is_same_v<FHSSSyntheticIqToken,
                               graph::gpu::accel::ControlToken<
                                   FHSSSyntheticIqOutputPacket>>);
  static_assert(std::is_same_v<FHSSDownconvertedIqToken,
                               graph::gpu::accel::ControlToken<
                                   FHSSDownconvertedIqPacket>>);
  static_assert(std::is_same_v<FHSSChannelizedIqToken,
                               graph::gpu::accel::ControlToken<
                                   FHSSChannelizedIqPacket>>);
  static_assert(std::is_same_v<FHSSPerChannelPulseEvidenceToken,
                               graph::gpu::accel::ControlToken<
                                   FHSSPerChannelPulseEvidencePacket>>);
  static_assert(!IsControlTokenV<FHSSSyntheticIqOutputPacket>);
  static_assert(!IsControlTokenV<FHSSSyntheticIqFixture>);
}

TEST(FHSSPacketContractTest,
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

TEST(FHSSPacketContractTest,
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

TEST(FHSSPacketContractTest,
     DownconverterContractCarriesReferenceFrameMetadata) {
  FHSSDownconvertedIqPacket passthrough{};
  passthrough.downconverter.input_iq_center_frequency_hz = 1'240'000'000.0;
  passthrough.downconverter.input_reference_frequency_hz = 1'240'000'000.0;
  passthrough.downconverter.output_iq_center_frequency_hz = 1'240'000'000.0;
  passthrough.downconverter.output_reference_frequency_hz = 1'240'000'000.0;
  passthrough.downconverter.translation_frequency_hz = 0.0;
  passthrough.downconverter.passthrough = true;
  passthrough.downconverter.phase_convention =
      FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation;
  passthrough.downconverter.sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz;
  passthrough.downconverter.input_global_start_sample = 13'000;
  passthrough.downconverter.output_global_start_sample = 13'000;
  passthrough.downconverter.sample_time_map.input_packet_global_start_sample =
      13'000;

  EXPECT_TRUE(FHSSGraphXDownconverterMetadataIsValid(
      passthrough.downconverter));
  EXPECT_TRUE(passthrough.downconverter.passthrough);
  EXPECT_EQ(passthrough.downconverter.input_global_start_sample, 13'000u);
  EXPECT_EQ(passthrough.downconverter.output_global_start_sample, 13'000u);

  FHSSDownconvertedIqPacket translated = passthrough;
  translated.downconverter.output_iq_center_frequency_hz = 1'232'000'000.0;
  translated.downconverter.translation_frequency_hz = 8'000'000.0;
  translated.downconverter.passthrough = false;
  translated.downconverter.phase_convention =
      FHSSGraphXDownconverterPhaseConvention::
          OutputTimesExpNegativeJTwoPiTranslationT;
  EXPECT_TRUE(FHSSGraphXDownconverterMetadataIsValid(
      translated.downconverter));
}

TEST(FHSSPacketContractTest,
     ChannelizerContractMapsOneLogicalChannelPerFrequencyEntry) {
  FHSSFrequencyConfig config{};
  for (std::uint32_t i = 0; i < FHSSProtocolConstants::kFrequencyCount; ++i) {
    config.iq_offset_frequency_hz[i] =
        static_cast<double>(i) * FHSSProtocolConstants::kFrequencySpacingHz -
        252'000'000.0;
  }
  auto map_result = BuildFrequencyMap(config);
  ASSERT_TRUE(map_result.has_value()) << map_result.error().message;

  EXPECT_TRUE(FHSSGraphXChannelCountMatchesFrequencyTable(
      FHSSProtocolConstants::kFrequencyCount, config));
  EXPECT_FALSE(FHSSGraphXChannelCountMatchesFrequencyTable(4, config));

  std::vector<FHSSChannelizedIqPacket> channels;
  channels.reserve(FHSSProtocolConstants::kFrequencyCount);
  for (const auto &entry : *map_result) {
    FHSSChannelizedIqPacket packet{};
    packet.channel.channel_id = entry.index;
    packet.channel.frequency_index = entry.index;
    packet.channel.rf_frequency_hz = entry.rf_frequency_hz;
    packet.channel.iq_offset_frequency_hz = entry.iq_offset_frequency_hz;
    packet.channel.channel_sample_rate_hz =
        FHSSProtocolConstants::kSampleRateHz / 2.0;
    packet.channel.decimation_factor = 2;
    packet.channel.filter_group_delay_input_samples = 7;
    packet.channel.input_global_start_sample = 50'000;
    packet.channel.channel_global_start_sample = 50'007;
    packet.channel.sample_time_map.input_packet_global_start_sample = 50'000;
    packet.channel.sample_time_map.decimation_factor = 2;
    packet.channel.sample_time_map.group_delay_input_samples = 7;
    packet.channel.sample_time_map.input_sample_rate_hz =
        FHSSProtocolConstants::kSampleRateHz;
    packet.channel.sample_time_map.output_sample_rate_hz =
        FHSSProtocolConstants::kSampleRateHz / 2.0;
    packet.receiver_guard_or_metadata_channel =
        IsReservedFrequencyIndex(entry.index);
    channels.push_back(packet);
  }

  ASSERT_EQ(channels.size(), FHSSProtocolConstants::kFrequencyCount);
  EXPECT_TRUE(channels.front().receiver_guard_or_metadata_channel);
  EXPECT_TRUE(channels.back().receiver_guard_or_metadata_channel);
  EXPECT_FALSE(channels[1].receiver_guard_or_metadata_channel);
  EXPECT_FALSE(
      IsSelectableFrequencyIndex(channels.front().channel.frequency_index));
  EXPECT_FALSE(
      IsSelectableFrequencyIndex(channels.back().channel.frequency_index));

  for (const auto &packet : channels) {
    const auto &entry = (*map_result)[packet.channel.frequency_index];
    EXPECT_TRUE(FHSSGraphXChannelMetadataMatchesFrequencyEntry(packet.channel,
                                                              entry));
  }
}

TEST(FHSSPacketContractTest,
     ChannelizedContractsPreserveComplexEvidenceAndSampleTimeMapping) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(128, {1.0, 0.0}));

  FHSSChannelizedIqPacket channel{};
  channel.channel.channel_id = 24;
  channel.channel.frequency_index = 24;
  channel.channel.rf_frequency_hz = RfFrequencyHz(24);
  channel.channel.iq_offset_frequency_hz = -60'000'000.0;
  channel.channel.channel_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz / 4.0;
  channel.channel.decimation_factor = 4;
  channel.channel.filter_group_delay_input_samples = 11;
  channel.channel.input_global_start_sample = 10'000;
  channel.channel.channel_global_start_sample = 10'011;
  channel.channel.sample_time_map.input_packet_global_start_sample = 10'000;
  channel.channel.sample_time_map.decimation_factor = 4;
  channel.channel.sample_time_map.group_delay_input_samples = 11;
  channel.channel.sample_time_map.input_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz;
  channel.channel.sample_time_map.output_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz / 4.0;
  channel.iq.host_complex64_samples = samples;
  channel.iq.sample_count = samples->size();
  channel.iq.sample_time_map = channel.channel.sample_time_map;

  EXPECT_TRUE(FHSSGraphXEvidenceHasHostComplexIq(channel.iq));
  EXPECT_EQ(FHSSGraphXInputGlobalSampleForChannelSample(channel.channel, 0),
            10'011);
  EXPECT_EQ(FHSSGraphXInputGlobalSampleForChannelSample(channel.channel, 5),
            10'031);

  FHSSPerChannelPulseEvidencePacket evidence{};
  evidence.channel = channel.channel;
  evidence.channel_iq = channel.iq;
  evidence.detected_pulses.push_back(FHSSGraphXPulseMetadata{});
  evidence.detected_pulses.back().timing.global_start_sample = 10'011;
  evidence.detected_pulses.back().timing.channel_start_sample = 0;
  evidence.detected_pulses.back().timing.channel_id = 24;
  evidence.detected_pulses.back().frequency.frequency_index = 24;
  evidence.detected_pulses.back().frequency.rf_frequency_hz = RfFrequencyHz(24);
  evidence.detected_pulses.back().frequency.iq_offset_frequency_hz =
      -60'000'000.0;
  evidence.pulse_evidence.push_back(channel.iq);

  ASSERT_EQ(evidence.detected_pulses.size(), 1u);
  EXPECT_EQ(evidence.detected_pulses.front().timing.global_start_sample,
            10'011u);
  EXPECT_EQ(evidence.detected_pulses.front().timing.channel_id, 24u);
  EXPECT_EQ(evidence.pulse_evidence.front().host_complex64_samples, samples);
}

TEST(FHSSPacketContractTest,
     FutureAccelSidecarBoundaryIsDocumentedWithoutGpuExecution) {
  FHSSFutureAccelSidecarContract contract{};
  EXPECT_EQ(contract.residency, FHSSGraphXPayloadResidency::FutureAccelTokenSidecar);
  EXPECT_TRUE(contract.carries_same_semantic_metadata);
  EXPECT_FALSE(contract.cpu_execution_required_by_contract);
  EXPECT_NE(std::string_view(contract.boundary).find("Future accelerator tokens"),
            std::string_view::npos);

  FHSSGraphXComplexEvidence evidence{};
  evidence.residency = FHSSGraphXPayloadResidency::FutureAccelTokenSidecar;
  evidence.sample_count = FHSSProtocolConstants::kPulseWidthSamples;
  evidence.sample_time_map.input_packet_global_start_sample = 1234;

  EXPECT_TRUE(FHSSGraphXEvidenceIsFutureAccelSidecarCompatible(evidence));
  EXPECT_FALSE(FHSSGraphXEvidenceHasHostComplexIq(evidence));
}

TEST(FHSSPacketContractTest,
     RuntimePacketsAndAssemblerConfigContainNoFixtureTruth) {
  static_assert(HasTruthPulses<FHSSSyntheticIqFixture>);
  static_assert(!HasTruthPulses<FHSSSyntheticIqOutputPacket>);
  static_assert(!HasTruthPulses<FHSSMessageAssemblerConfig>);
  static_assert(!HasTruthMismatches<FHSSAssembledMessagePacket>);
  static_assert(!HasTruthMismatches<FHSSDiagnosticsPacket>);
  static_assert(!HasTruthMismatchCount<FHSSDiagnosticsPacket>);

  SUCCEED();
}

TEST(FHSSPacketContractTest,
     DiagnosticsCarryMinimumFieldsWithoutOwningDecoderTruth) {
  FHSSDiagnosticsPacket diagnostics{};
  diagnostics.pulse_count = 7;
  diagnostics.rejected_count = 1;
  diagnostics.preamble_lock = true;
  diagnostics.global_start_sample = 6500;
  diagnostics.frequency_index = 12;
  diagnostics.confidence = 0.99;
  diagnostics.viterbi_path_metric = 0.25;
  diagnostics.decoded_value = 0x12345678u;

  EXPECT_EQ(diagnostics.pulse_count, 7u);
  EXPECT_EQ(diagnostics.rejected_count, 1u);
  EXPECT_TRUE(diagnostics.preamble_lock);
  EXPECT_EQ(*diagnostics.global_start_sample, 6500u);
  EXPECT_EQ(*diagnostics.frequency_index, 12u);
  EXPECT_DOUBLE_EQ(*diagnostics.confidence, 0.99);
  EXPECT_DOUBLE_EQ(*diagnostics.viterbi_path_metric, 0.25);
  EXPECT_EQ(*diagnostics.decoded_value, 0x12345678u);
}

} // namespace
