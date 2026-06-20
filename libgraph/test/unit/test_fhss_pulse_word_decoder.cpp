// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSPulseWordDecoder.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

namespace {

using dsp::fhss::CPSMViterbiDecoderNode;
using dsp::fhss::CPSMViterbiResult;
using dsp::fhss::FHSSCorrelatorBankDetectorConfig;
using dsp::fhss::FHSSCorrelatorBankDetectorNode;
using dsp::fhss::FHSSDecodeConfig;
using dsp::fhss::FHSSFrequencyConfig;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSPulseCandidate;
using dsp::fhss::FHSSPulseWordDecodeStatus;
using dsp::fhss::FHSSPulseWordDecoderConfig;
using dsp::fhss::FHSSPulseWordDecoderNode;
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

std::vector<FHSSPreamblePulseSpec> Preamble(std::uint32_t first_word) {
  return {
      {1, first_word},     {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u}, {1, first_word},     {7, 0x7777'7777u},
      {12, 0x1212'1212u}, {62, 0x6262'6262u}, {1, first_word},
      {7, 0x7777'7777u},  {12, 0x1212'1212u}, {62, 0x6262'6262u},
      {1, first_word},     {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u},
  };
}

FHSSDecodeConfig DecodeConfig(std::uint32_t first_word = 0xA5A5'5A5Au) {
  FHSSDecodeConfig config{};
  config.frequency = ValidFrequencyConfig();
  config.active_frequency_indices = ActiveFrequencies();
  config.preamble_pulses = Preamble(first_word);
  config.payload_random.rng_seed = 0x600du;
  config.payload_random.deterministic = true;
  return config;
}

std::vector<double> SymbolsForWord(std::uint32_t value) {
  std::vector<double> symbols;
  symbols.reserve(FHSSProtocolConstants::kBitsPerPulse);
  for (std::uint32_t i = 0; i < FHSSProtocolConstants::kBitsPerPulse; ++i) {
    symbols.push_back(dsp::fhss::CpsmSymbolForBit(value, i));
  }
  return symbols;
}

CPSMViterbiResult ViterbiForSymbols(std::vector<double> symbols,
                                    double confidence = 0.75) {
  return CPSMViterbiResult{.symbols = std::move(symbols),
                           .phase_states = {},
                           .best_path_metric = 1.25,
                           .second_best_path_metric = 2.25,
                           .confidence = confidence,
                           .terminal_phase_checked = false,
                           .terminal_phase_state = 0};
}

FHSSPulseCandidate Candidate() {
  FHSSPulseCandidate candidate{};
  candidate.detected_pulse.global_start_sample = 12'345;
  candidate.detected_pulse.global_end_sample =
      12'345 + FHSSProtocolConstants::kPulseWidthSamples;
  candidate.detected_pulse.duration_samples =
      FHSSProtocolConstants::kPulseWidthSamples;
  candidate.detected_pulse.frequency_index = 12;
  candidate.detected_pulse.rf_frequency_hz = 1'096'000'000.0;
  candidate.detected_pulse.iq_offset_frequency_hz = 48'000'000.0;
  candidate.detected_pulse.confidence = 0.9;
  candidate.provisional_slot_index = 7;
  candidate.final_slot_index = 3;
  return candidate;
}

TEST(FHSSPulseWordDecoderTest, MapsCpsmSymbolsToBits) {
  const auto zero = dsp::fhss::FHSSCpsmSymbolToBit(1.0);
  const auto one = dsp::fhss::FHSSCpsmSymbolToBit(-1.0);
  const auto invalid = dsp::fhss::FHSSCpsmSymbolToBit(0.0);

  ASSERT_TRUE(zero.has_value());
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*zero, 0u);
  EXPECT_EQ(*one, 1u);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error().code, FHSSValidationCode::InvalidTiming);
}

TEST(FHSSPulseWordDecoderTest, AssemblesMsbFirstThirtyTwoBitWord) {
  const auto value =
      dsp::fhss::AssembleFHSSPulseWordMsbFirst(SymbolsForWord(0x8000'0001u));

  ASSERT_TRUE(value.has_value()) << value.error().message;
  EXPECT_EQ(*value, 0x8000'0001u);
}

TEST(FHSSPulseWordDecoderTest, KnownSymbolVectorsRecoverExpectedValues) {
  for (const auto expected :
       {0x0000'0000u, 0xFFFF'FFFFu, 0xA5A5'5A5Au, 0x1234'5678u}) {
    const auto value =
        dsp::fhss::AssembleFHSSPulseWordMsbFirst(SymbolsForWord(expected));
    ASSERT_TRUE(value.has_value()) << value.error().message;
    EXPECT_EQ(*value, expected);
  }
}

TEST(FHSSPulseWordDecoderTest, PreservesPulseMetadataAndConfidence) {
  const auto candidate = Candidate();
  const auto viterbi = ViterbiForSymbols(SymbolsForWord(0xDEAD'BEEFu), 0.82);

  const auto decoded = FHSSPulseWordDecoderNode::Decode(candidate, viterbi);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::Ok);
  EXPECT_EQ(decoded.decoded_value, 0xDEAD'BEEFu);
  EXPECT_DOUBLE_EQ(decoded.confidence, 0.82);
  EXPECT_DOUBLE_EQ(decoded.viterbi_path_metric, 1.25);
  EXPECT_EQ(decoded.candidate.detected_pulse.global_start_sample,
            candidate.detected_pulse.global_start_sample);
  EXPECT_EQ(decoded.candidate.detected_pulse.frequency_index,
            candidate.detected_pulse.frequency_index);
  EXPECT_DOUBLE_EQ(decoded.candidate.detected_pulse.rf_frequency_hz,
                   candidate.detected_pulse.rf_frequency_hz);
  EXPECT_DOUBLE_EQ(decoded.candidate.detected_pulse.iq_offset_frequency_hz,
                   candidate.detected_pulse.iq_offset_frequency_hz);
  ASSERT_TRUE(decoded.candidate.final_slot_index.has_value());
  EXPECT_EQ(*decoded.candidate.final_slot_index, 3u);
}

TEST(FHSSPulseWordDecoderTest, ReportsInvalidViterbiOutput) {
  auto short_viterbi = ViterbiForSymbols({1.0, -1.0});
  auto decoded = FHSSPulseWordDecoderNode::Decode(Candidate(), short_viterbi);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::InvalidSymbolCount);
  EXPECT_FALSE(decoded.status_message.empty());

  auto invalid_symbol = ViterbiForSymbols(SymbolsForWord(0x1234'5678u));
  invalid_symbol.symbols[5] = 0.25;
  decoded = FHSSPulseWordDecoderNode::Decode(Candidate(), invalid_symbol);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::InvalidSymbolDecision);
  EXPECT_FALSE(decoded.status_message.empty());

  auto invalid_metric = ViterbiForSymbols(SymbolsForWord(0x1234'5678u));
  invalid_metric.best_path_metric = std::numeric_limits<double>::infinity();
  decoded = FHSSPulseWordDecoderNode::Decode(Candidate(), invalid_metric);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::InvalidPathMetric);
  EXPECT_FALSE(decoded.status_message.empty());
}

TEST(FHSSPulseWordDecoderTest, ReportsLowConfidenceButPreservesValue) {
  FHSSPulseWordDecoderConfig config{};
  config.minimum_confidence = 0.5;
  const auto viterbi = ViterbiForSymbols(SymbolsForWord(0xCAFEBABEu), 0.1);

  const auto decoded =
      FHSSPulseWordDecoderNode::Decode(Candidate(), viterbi, config);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::LowConfidence);
  EXPECT_EQ(decoded.decoded_value, 0xCAFEBABEu);
  EXPECT_DOUBLE_EQ(decoded.confidence, 0.1);
  EXPECT_FALSE(decoded.status_message.empty());
}

TEST(FHSSPulseWordDecoderTest,
     DecodesFromComplexDerivedCpsmDecisionsNotTruthMetadata) {
  constexpr std::uint32_t kExpected = 0xA5A5'5A5Au;
  FHSSSyntheticIqGeneratorConfig generator_config{};
  generator_config.decode_config = DecodeConfig(kExpected);
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(generator_config);
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  FHSSCorrelatorBankDetectorConfig detector_config{};
  detector_config.decode_config = DecodeConfig(kExpected);
  const auto detected =
      FHSSCorrelatorBankDetectorNode::Detect(fixture->samples, detector_config);
  ASSERT_TRUE(detected.has_value()) << detected.error().message;
  ASSERT_FALSE(detected->local_detections.empty());
  ASSERT_TRUE(detected->local_detections.front().complex_evidence.samples);

  const auto viterbi = CPSMViterbiDecoderNode::Decode(
      *detected->local_detections.front().complex_evidence.samples);
  ASSERT_TRUE(viterbi.has_value()) << viterbi.error().message;

  FHSSPulseCandidate candidate{};
  candidate.detected_pulse.global_start_sample =
      detected->local_detections.front().local_start_offset;
  candidate.detected_pulse.frequency_index =
      detected->local_detections.front().frequency_index;
  const auto decoded =
      FHSSPulseWordDecoderNode::Decode(candidate, *viterbi);

  EXPECT_EQ(decoded.status, FHSSPulseWordDecodeStatus::Ok);
  EXPECT_EQ(decoded.decoded_value, kExpected);
}

} // namespace
