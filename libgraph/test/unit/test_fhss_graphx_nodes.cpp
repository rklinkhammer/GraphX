// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/fhss/CPSMBranchMetricNode.hpp"
#include "dsp/fhss/CPSMViterbiDecoderNode.hpp"
#include "dsp/fhss/ChannelizerNode.hpp"
#include "dsp/fhss/FHSSCorrelatorBankDetectorNode.hpp"
#include "dsp/fhss/FHSSDownconverterNode.hpp"
#include "dsp/fhss/FHSSMessageAssemblerNode.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "dsp/fhss/FHSSPreambleDetectorNode.hpp"
#include "dsp/fhss/FHSSPulseCandidateNode.hpp"
#include "dsp/fhss/FHSSPulseMergeNode.hpp"
#include "dsp/fhss/FHSSPulseWordDecoderNode.hpp"
#include "dsp/fhss/FHSSSyntheticIqSourceNode.hpp"
#include "graph/RegisteredNodeProvider.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

using namespace dsp::fhss;

#ifdef __APPLE__
constexpr const char *kSharedLibraryExtension = ".dylib";
#else
constexpr const char *kSharedLibraryExtension = ".so";
#endif

template <typename T> struct IsControlToken : std::false_type {};

template <typename SidecarT>
struct IsControlToken<graph::gpu::accel::ControlToken<SidecarT>>
    : std::true_type {};

template <typename T>
inline constexpr bool IsControlTokenV = IsControlToken<T>::value;

template <typename TokenT> struct TokenSidecar;

template <typename SidecarT>
struct TokenSidecar<graph::gpu::accel::ControlToken<SidecarT>> {
  using type = SidecarT;
};

FHSSFrequencyConfig FrequencyConfig() {
  FHSSFrequencyConfig config{};
  config.occupied_bandwidth_hz = 5'000'000.0;
  config.max_abs_cfo_hz = 1'000.0;
  config.iq_offset_frequency_hz[1] = 0.0;
  config.iq_offset_frequency_hz[7] = -64'000'000.0;
  config.iq_offset_frequency_hz[12] = 48'000'000.0;
  config.iq_offset_frequency_hz[62] = 112'000'000.0;
  return config;
}

FHSSFrequencyConfig FullReceiverFrequencyConfig() {
  FHSSFrequencyConfig config{};
  config.occupied_bandwidth_hz = 5'000'000.0;
  config.max_abs_cfo_hz = 1'000.0;
  const double iq_center_hz = 1'252'000'000.0;
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    config.iq_offset_frequency_hz[index] = RfFrequencyHz(index, config) -
                                           iq_center_hz;
  }
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
  config.frequency = FrequencyConfig();
  config.active_frequency_indices = ActiveFrequencies();
  config.preamble_pulses = Preamble();
  config.payload_random.rng_seed = 0x7b;
  config.payload_random.deterministic = true;
  return config;
}

FHSSSyntheticIqGeneratorConfig GeneratorConfig() {
  FHSSSyntheticIqGeneratorConfig config{};
  config.decode_config = DecodeConfig();
  FHSSScheduledMessageSpec message{};
  message.message_id = 1;
  for (const auto &pulse : Preamble()) {
    message.pulses.push_back(FHSSMessagePulseSpec{
        .frequency_index = pulse.frequency_index,
        .value = pulse.word_value,
        .role = FHSSMessagePulseRole::Preamble});
  }
  message.pulses.push_back(FHSSMessagePulseSpec{
      .frequency_index = 1,
      .value = 0x0102'0304u,
      .role = FHSSMessagePulseRole::Body});
  message.pulses.push_back(FHSSMessagePulseSpec{
      .frequency_index = 7,
      .value = 0xA5A5'5A5Au,
      .role = FHSSMessagePulseRole::Body});
  config.messages.push_back(std::move(message));
  return config;
}

FHSSCorrelatorBankDetectorConfig DetectorConfig() {
  FHSSCorrelatorBankDetectorConfig config{};
  config.decode_config = DecodeConfig();
  config.detector_id = 7;
  config.packet_sequence = 9;
  return config;
}

FHSSDownconverterConfig PassthroughDownconverterConfig() {
  FHSSDownconverterConfig config{};
  config.input_iq_center_frequency_hz = 1'252'000'000.0;
  config.input_reference_frequency_hz = 1'252'000'000.0;
  config.output_iq_center_frequency_hz = 1'252'000'000.0;
  config.output_reference_frequency_hz = 1'252'000'000.0;
  config.translation_frequency_hz = 0.0;
  config.passthrough = true;
  config.phase_convention =
      FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation;
  return config;
}

FHSSDownconverterConfig TranslationDownconverterConfig() {
  FHSSDownconverterConfig config{};
  config.input_iq_center_frequency_hz = 1'252'000'000.0;
  config.input_reference_frequency_hz = 1'252'000'000.0;
  config.output_iq_center_frequency_hz = 1'260'000'000.0;
  config.output_reference_frequency_hz = 1'260'000'000.0;
  config.translation_frequency_hz = 8'000'000.0;
  config.passthrough = false;
  config.phase_convention =
      FHSSGraphXDownconverterPhaseConvention::
          OutputTimesExpNegativeJTwoPiTranslationT;
  return config;
}

FHSSChannelizerConfig ChannelizerConfig() {
  FHSSChannelizerConfig config{};
  config.frequency = FullReceiverFrequencyConfig();
  config.receiver_frequency_indices = FHSSAllFrequencyIndices();
  config.channel_ids = config.receiver_frequency_indices;
  config.transmitted_active_frequency_indices = {1, 7, 12, 62};
  config.transmitted_pulse_frequency_indices = {1, 7, 12, 62};
  config.decimation_factor = 2;
  config.channel_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz / 2.0;
  config.filter_group_delay_input_samples = 0;
  return config;
}

FHSSSyntheticIqToken SyntheticTokenFromSamples(
    std::shared_ptr<const std::vector<std::complex<double>>> samples,
    std::uint64_t global_start_sample = 0) {
  FHSSSyntheticIqToken token{};
  token.token_id = 123;
  FHSSGraphXSampleTimeMap map{};
  map.input_packet_global_start_sample = global_start_sample;
  token.sidecar.iq =
      FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), map);
  return token;
}

FHSSMessageAssemblerConfig AssemblerConfig(
    const std::vector<FHSSTruthPulse> &truth_pulses = {}) {
  FHSSMessageAssemblerConfig config{};
  config.preamble_pulses = Preamble();
  config.truth_pulses = truth_pulses;
  return config;
}

FHSSGraphXPulseMetadata MetadataFromTruth(const FHSSTruthPulse &truth) {
  FHSSDetectedPulse detected{};
  detected.global_start_sample = truth.global_start_sample;
  detected.global_end_sample =
      truth.global_start_sample + truth.duration_samples;
  detected.duration_samples = truth.duration_samples;
  detected.channel_start_sample = truth.global_start_sample;
  detected.channel_id = truth.frequency_index;
  detected.frequency_index = truth.frequency_index;
  detected.rf_frequency_hz = truth.rf_frequency_hz;
  detected.iq_offset_frequency_hz = truth.iq_offset_frequency_hz;
  detected.estimated_center_frequency_hz = truth.iq_offset_frequency_hz;
  detected.confidence = 1.0;
  return FHSSGraphXPulseMetadataFromDetectedPulse(detected);
}

FHSSDecodedPulseWordsToken DecodedWordsFromTruth(
    const FHSSSyntheticIqOutputPacket &fixture) {
  FHSSDecodedPulseWordsToken token{};
  token.token_id = 99;
  token.sidecar.globally_ordered = true;
  token.sidecar.truth_metadata_required_for_decision = false;
  token.sidecar.decoded_pulses.reserve(fixture.truth_pulses.size());
  for (const auto &truth : fixture.truth_pulses) {
    token.sidecar.decoded_pulses.push_back(FHSSDecodedPulseWordPacket{
        .pulse = MetadataFromTruth(truth),
        .decoded_value = truth.value,
        .confidence = 1.0,
        .viterbi_path_metric = 0.0,
        .status = FHSSGraphXDecodeStatus::Ok,
        .status_message = "Ok",
        .truth_metadata_required_for_decision = false});
  }
  return token;
}

std::string PluginFilename(const std::string &target_name) {
  return "lib" + target_name + kSharedLibraryExtension;
}

struct FHSSPluginExpectation {
  const char *type_name;
  const char *target_name;
};

const std::vector<FHSSPluginExpectation> &FHSSPluginExpectations() {
  static const std::vector<FHSSPluginExpectation> expectations{
      {"FHSSSyntheticIqSourceNode", "fhss_synthetic_iq_source_node"},
      {"FHSSCorrelatorBankDetectorNode",
       "fhss_correlator_bank_detector_node"},
      {"FHSSDownconverterNode", "fhss_downconverter_node"},
      {"ChannelizerNode", "channelizer_node"},
      {"FHSSPulseMergeNode", "fhss_pulse_merge_node"},
      {"FHSSPulseCandidateNode", "fhss_pulse_candidate_node"},
      {"CPSMBranchMetricNode", "cpsm_branch_metric_node"},
      {"CPSMViterbiDecoderNode", "cpsm_viterbi_decoder_node"},
      {"FHSSPulseWordDecoderNode", "fhss_pulse_word_decoder_node"},
      {"FHSSPreambleDetectorNode", "fhss_preamble_detector_node"},
      {"FHSSMessageAssemblerNode", "fhss_message_assembler_node"},
      {"FHSSMessageSinkNode", "fhss_message_sink_node"},
  };
  return expectations;
}

TEST(FHSSGraphXNodeTest, EveryNodePortUsesAccelControlTokenSidecars) {
  static_assert(
      IsControlTokenV<FHSSSyntheticIqSourceNode::OutputType<0>>);
  static_assert(
      IsControlTokenV<FHSSCorrelatorBankDetectorNode::InputType<0>>);
  static_assert(
      IsControlTokenV<FHSSCorrelatorBankDetectorNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSDownconverterNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSDownconverterNode::OutputType<0>>);
  static_assert(IsControlTokenV<ChannelizerNode::InputType<0>>);
  static_assert(IsControlTokenV<ChannelizerNode::OutputType<0>>);
  static_assert(IsControlTokenV<ChannelizerNode::OutputType<1>>);
  static_assert(IsControlTokenV<ChannelizerNode::OutputType<62>>);
  static_assert(IsControlTokenV<ChannelizerNode::OutputType<63>>);
  static_assert(ChannelizerNode::NOutputs ==
                FHSSProtocolConstants::kFrequencyCount);
  static_assert(IsControlTokenV<FHSSPulseMergeNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSPulseMergeNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSPulseCandidateNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSPulseCandidateNode::OutputType<0>>);
  static_assert(IsControlTokenV<CPSMBranchMetricNode::InputType<0>>);
  static_assert(IsControlTokenV<CPSMBranchMetricNode::OutputType<0>>);
  static_assert(IsControlTokenV<CPSMViterbiDecoderNode::InputType<0>>);
  static_assert(IsControlTokenV<CPSMViterbiDecoderNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSPulseWordDecoderNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSPulseWordDecoderNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSPreambleDetectorNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSPreambleDetectorNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSMessageAssemblerNode::InputType<0>>);
  static_assert(IsControlTokenV<FHSSMessageAssemblerNode::OutputType<0>>);
  static_assert(IsControlTokenV<FHSSMessageSinkNode::InputType<0>>);

  static_assert(std::is_same_v<
                typename TokenSidecar<
                    FHSSSyntheticIqSourceNode::OutputType<0>>::type,
                FHSSSyntheticIqOutputPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<
                    FHSSCorrelatorBankDetectorNode::OutputType<0>>::type,
                FHSSDetectedPulseEvidencePacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<
                    FHSSDownconverterNode::OutputType<0>>::type,
                FHSSDownconvertedIqPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<ChannelizerNode::OutputType<0>>::type,
                FHSSChannelizedIqPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<ChannelizerNode::OutputType<1>>::type,
                FHSSChannelizedIqPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<ChannelizerNode::OutputType<62>>::type,
                FHSSChannelizedIqPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<ChannelizerNode::OutputType<63>>::type,
                FHSSChannelizedIqPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<FHSSPulseMergeNode::OutputType<0>>::type,
                FHSSPulseCandidateEvidencePacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<CPSMViterbiDecoderNode::OutputType<0>>::type,
                FHSSCpsmSymbolDecisionPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<
                    FHSSPulseWordDecoderNode::OutputType<0>>::type,
                FHSSDecodedPulseWordsPacket>);
  static_assert(std::is_same_v<
                typename TokenSidecar<
                    FHSSMessageAssemblerNode::OutputType<0>>::type,
                FHSSAssembledMessagePacket>);

  static_assert(!std::is_same_v<FHSSPulseMergeNode::InputType<0>,
                                FHSSDetectedPulseEvidencePacket>);
}

TEST(FHSSGraphXNodeTest, CpuLaneDecodesFirstPulseThroughGraphXNodeApi) {
  FHSSSyntheticIqSourceNode source(GeneratorConfig());
  auto synthetic =
      source.Produce(std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(synthetic.has_value());
  ASSERT_TRUE(FHSSGraphXEvidenceHasHostComplexIq(synthetic->sidecar.iq));
  ASSERT_EQ(synthetic->sidecar.truth_pulses.size(), 18u);

  FHSSCorrelatorBankDetectorNode detector(DetectorConfig());
  auto detected =
      detector.Transfer(*synthetic, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(detected.has_value());
  ASSERT_EQ(detected->sidecar.detected_pulses.size(),
            synthetic->sidecar.truth_pulses.size());
  ASSERT_EQ(detected->sidecar.pulse_evidence.size(),
            detected->sidecar.detected_pulses.size());

  FHSSPulseMergeNode merge;
  auto candidates =
      merge.Transfer(*detected, std::integral_constant<std::size_t, 0>{},
                     std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(candidates.has_value());
  ASSERT_FALSE(candidates->sidecar.ordered_candidates.empty());
  EXPECT_TRUE(candidates->sidecar.globally_ordered);

  FHSSPulseCandidateNode candidate_boundary;
  auto candidate_stream = candidate_boundary.Transfer(
      *candidates, std::integral_constant<std::size_t, 0>{},
      std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(candidate_stream.has_value());

  CPSMBranchMetricNode branch_metric;
  auto metrics = branch_metric.Transfer(
      *candidate_stream, std::integral_constant<std::size_t, 0>{},
      std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(metrics.has_value());
  EXPECT_EQ(metrics->sidecar.trellis_state_count, 4u);
  EXPECT_FALSE(metrics->sidecar.branch_costs.empty());
  EXPECT_EQ(metrics->sidecar.pulse_metrics.size(),
            candidate_stream->sidecar.ordered_candidates.size());

  CPSMViterbiDecoderNode viterbi;
  auto symbols = viterbi.Transfer(*metrics,
                                  std::integral_constant<std::size_t, 0>{},
                                  std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(symbols.has_value());
  EXPECT_EQ(symbols->sidecar.symbols.size(),
            FHSSProtocolConstants::kBitsPerPulse);
  EXPECT_EQ(symbols->sidecar.pulse_decisions.size(),
            candidate_stream->sidecar.ordered_candidates.size());
  EXPECT_FALSE(symbols->sidecar.truth_metadata_required_for_decision);

  FHSSPulseWordDecoderNode word_decoder;
  auto decoded = word_decoder.Transfer(
      *symbols, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(decoded.has_value());
  ASSERT_FALSE(decoded->sidecar.decoded_pulses.empty());
  EXPECT_EQ(decoded->sidecar.decoded_pulses.front().status,
            FHSSGraphXDecodeStatus::Ok);
  EXPECT_EQ(decoded->sidecar.decoded_pulses.front().decoded_value,
            synthetic->sidecar.truth_pulses.front().value);
  EXPECT_EQ(decoded->sidecar.decoded_pulses.front()
                .pulse.frequency.frequency_index,
            synthetic->sidecar.truth_pulses.front().frequency_index);
  EXPECT_FALSE(decoded->sidecar.truth_metadata_required_for_decision);
}

TEST(FHSSGraphXNodeTest,
     DownconverterPassthroughPreservesSamplesAndGlobalTiming) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>{{1.0, 0.0}, {0.0, 1.0},
                                        {-1.0, 0.0}});
  auto input = SyntheticTokenFromSamples(samples, 42'000);

  FHSSDownconverterNode downconverter(PassthroughDownconverterConfig());
  auto output = downconverter.Transfer(input,
                                       std::integral_constant<std::size_t, 0>{},
                                       std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(output->token_id, input.token_id);
  EXPECT_EQ(output->sidecar.iq.host_complex64_samples, samples);
  EXPECT_EQ(output->sidecar.iq.sample_count, samples->size());
  EXPECT_EQ(output->sidecar.iq.sample_time_map.input_packet_global_start_sample,
            42'000u);
  EXPECT_TRUE(output->sidecar.downconverter.passthrough);
  EXPECT_DOUBLE_EQ(output->sidecar.downconverter.translation_frequency_hz, 0.0);
  EXPECT_EQ(output->sidecar.downconverter.input_global_start_sample, 42'000u);
  EXPECT_EQ(output->sidecar.downconverter.output_global_start_sample, 42'000u);
}

TEST(FHSSGraphXNodeTest,
     DownconverterTranslatesByDeclaredDeltaNotAbsoluteRfMetadata) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(4, {1.0, 0.0}));
  auto input = SyntheticTokenFromSamples(samples, 0);

  FHSSDownconverterNode downconverter(TranslationDownconverterConfig());
  auto output = downconverter.Transfer(input,
                                       std::integral_constant<std::size_t, 0>{},
                                       std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(output.has_value());
  ASSERT_TRUE(FHSSGraphXEvidenceHasHostComplexIq(output->sidecar.iq));
  ASSERT_EQ(output->sidecar.iq.host_complex64_samples->size(), 4u);

  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double radians_per_sample =
      -kTwoPi * 8'000'000.0 / FHSSProtocolConstants::kSampleRateHz;
  for (std::size_t i = 0; i < output->sidecar.iq.host_complex64_samples->size();
       ++i) {
    const auto expected =
        std::complex<double>(std::cos(radians_per_sample *
                                      static_cast<double>(i)),
                             std::sin(radians_per_sample *
                                      static_cast<double>(i)));
    const auto actual = output->sidecar.iq.host_complex64_samples->at(i);
    EXPECT_NEAR(actual.real(), expected.real(), 1.0e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1.0e-12);
  }
  EXPECT_FALSE(output->sidecar.downconverter.passthrough);
  EXPECT_DOUBLE_EQ(output->sidecar.downconverter.translation_frequency_hz,
                   8'000'000.0);
}

TEST(FHSSGraphXNodeTest,
     DownconverterRejectsImplicitFrequencyFrameMismatch) {
  auto bad = TranslationDownconverterConfig();
  bad.translation_frequency_hz = 1'000'000'000.0;
  FHSSDownconverterNode downconverter(bad);
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(4, {1.0, 0.0}));
  auto input = SyntheticTokenFromSamples(samples);

  auto output = downconverter.Transfer(input,
                                       std::integral_constant<std::size_t, 0>{},
                                       std::integral_constant<std::size_t, 0>{});
  EXPECT_FALSE(output.has_value());
}

TEST(FHSSGraphXNodeTest,
     ChannelizerOutputPortsMapDirectlyToFrequencyIndexAndChannelId) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(16, {1.0, 0.0}));
  auto synthetic = SyntheticTokenFromSamples(samples, 10'000);
  FHSSDownconverterNode downconverter(PassthroughDownconverterConfig());
  auto downconverted = downconverter.Transfer(
      synthetic, std::integral_constant<std::size_t, 0>{},
      std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(downconverted.has_value());

  ChannelizerNode channelizer(ChannelizerConfig());
  EXPECT_EQ(channelizer.GetOutputPortCount(),
            static_cast<int>(FHSSProtocolConstants::kFrequencyCount));

  ASSERT_TRUE(
      channelizer.Consume(*downconverted,
                          std::integral_constant<std::size_t, 0>{}));
  auto channel0 = channelizer.Produce(std::integral_constant<std::size_t, 0>{});
  auto channel1 = channelizer.Produce(std::integral_constant<std::size_t, 1>{});
  auto channel24 =
      channelizer.Produce(std::integral_constant<std::size_t, 24>{});
  auto channel62 =
      channelizer.Produce(std::integral_constant<std::size_t, 62>{});
  auto channel63 =
      channelizer.Produce(std::integral_constant<std::size_t, 63>{});
  ASSERT_TRUE(channel0.has_value());
  ASSERT_TRUE(channel1.has_value());
  ASSERT_TRUE(channel24.has_value());
  ASSERT_TRUE(channel62.has_value());
  ASSERT_TRUE(channel63.has_value());

  EXPECT_EQ(channel0->sidecar.channel.channel_id, 0u);
  EXPECT_EQ(channel0->sidecar.channel.frequency_index, 0u);
  EXPECT_TRUE(channel0->sidecar.receiver_guard_or_metadata_channel);
  EXPECT_EQ(channel1->sidecar.channel.channel_id, 1u);
  EXPECT_EQ(channel1->sidecar.channel.frequency_index, 1u);
  EXPECT_FALSE(channel1->sidecar.receiver_guard_or_metadata_channel);
  EXPECT_EQ(channel62->sidecar.channel.channel_id, 62u);
  EXPECT_EQ(channel62->sidecar.channel.frequency_index, 62u);
  EXPECT_FALSE(channel62->sidecar.receiver_guard_or_metadata_channel);
  EXPECT_EQ(channel63->sidecar.channel.channel_id, 63u);
  EXPECT_EQ(channel63->sidecar.channel.frequency_index, 63u);
  EXPECT_TRUE(channel63->sidecar.receiver_guard_or_metadata_channel);

  const auto &channel = channel24->sidecar;
  EXPECT_EQ(channel.channel.channel_id, 24u);
  EXPECT_EQ(channel.channel.frequency_index, 24u);
  EXPECT_DOUBLE_EQ(channel.channel.rf_frequency_hz, RfFrequencyHz(24));
  EXPECT_DOUBLE_EQ(channel.channel.iq_offset_frequency_hz,
                   FullReceiverFrequencyConfig().iq_offset_frequency_hz[24]);
  EXPECT_DOUBLE_EQ(channel.channel.channel_sample_rate_hz,
                   FHSSProtocolConstants::kSampleRateHz / 2.0);
  EXPECT_EQ(channel.channel.decimation_factor, 2u);
  EXPECT_EQ(channel.channel.filter_group_delay_input_samples, 0);
  EXPECT_EQ(channel.channel.input_global_start_sample, 10'000u);
  EXPECT_TRUE(FHSSGraphXEvidenceHasHostComplexIq(channel.iq));
  EXPECT_EQ(channel.iq.host_complex64_samples->size(), 8u);
  EXPECT_FALSE(channel.truth_metadata_required_for_decision);
}

TEST(FHSSGraphXNodeTest,
     ChannelizerRejectsReservedTransmitAndDuplicateReceiverConfiguration) {
  auto config = ChannelizerConfig();
  config.transmitted_active_frequency_indices = {0, 1, 7, 12};
  EXPECT_FALSE(ValidateFHSSChannelizerConfig(config).has_value());

  config = ChannelizerConfig();
  config.transmitted_pulse_frequency_indices = {1, 7, 12, 63};
  EXPECT_FALSE(ValidateFHSSChannelizerConfig(config).has_value());

  config = ChannelizerConfig();
  config.receiver_frequency_indices[5] = config.receiver_frequency_indices[4];
  EXPECT_FALSE(ValidateFHSSChannelizerConfig(config).has_value());

  config = ChannelizerConfig();
  config.channel_ids[5] = config.channel_ids[4];
  EXPECT_FALSE(ValidateFHSSChannelizerConfig(config).has_value());
}

TEST(FHSSGraphXNodeTest,
     PreambleAssemblerAndSinkOperateOnTokenWrappedDecodedWords) {
  FHSSSyntheticIqSourceNode source(GeneratorConfig());
  auto synthetic =
      source.Produce(std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(synthetic.has_value());
  auto decoded_words = DecodedWordsFromTruth(synthetic->sidecar);

  FHSSPreambleDetectorNode preamble_detector(Preamble());
  auto preamble = preamble_detector.Transfer(
      decoded_words, std::integral_constant<std::size_t, 0>{},
      std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(preamble.has_value());
  EXPECT_TRUE(preamble->sidecar.preamble_lock);
  EXPECT_EQ(preamble->sidecar.active_frequency_indices,
            (std::vector<std::uint32_t>{1, 7, 12, 62}));

  FHSSMessageAssemblerNode assembler(
      AssemblerConfig(synthetic->sidecar.truth_pulses));
  auto message = assembler.Transfer(*preamble,
                                    std::integral_constant<std::size_t, 0>{},
                                    std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message->sidecar.status, FHSSGraphXDecodeStatus::Ok);
  EXPECT_TRUE(message->sidecar.preamble_lock);
  EXPECT_EQ(message->sidecar.diagnostics.pulse_count,
            decoded_words.sidecar.decoded_pulses.size());
  EXPECT_EQ(message->sidecar.diagnostics.truth_mismatch_count, 0u);

  FHSSMessageSinkNode sink;
  EXPECT_TRUE(sink.Consume(*message, std::integral_constant<std::size_t, 0>{}));
  EXPECT_TRUE(sink.last_diagnostics().preamble_lock);
  EXPECT_EQ(sink.last_diagnostics().pulse_count,
            decoded_words.sidecar.decoded_pulses.size());
}

TEST(FHSSGraphXNodeTest, EveryNodeIsRegisteredAndDynamicallyLoadable) {
  const auto registry = std::make_shared<graph::PluginRegistry>();
  graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

  for (const auto &expectation : FHSSPluginExpectations()) {
    const auto filename = PluginFilename(expectation.target_name);
    ASSERT_TRUE(std::filesystem::exists(
        std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY) / filename))
        << filename;
    auto loaded = loader.LoadPluginSafe(filename);
    ASSERT_TRUE(loaded.has_value()) << expectation.type_name;
    EXPECT_TRUE(registry->HasNodeType(expectation.type_name))
        << expectation.type_name;
  }

  graph::RegisteredNodeProvider provider(registry);
  for (const auto &expectation : FHSSPluginExpectations()) {
    EXPECT_TRUE(provider.IsNodeTypeAvailable(expectation.type_name))
        << expectation.type_name;
    auto node = provider.CreateNodeExpected(expectation.type_name);
    EXPECT_TRUE(node.has_value()) << expectation.type_name;
  }
}

} // namespace
