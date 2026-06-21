// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

namespace {

using dsp::fhss::CPSMBranchMetricKernel;
using dsp::fhss::CPSMDecoderConfig;
using dsp::fhss::CPSMViterbiDecoderKernel;
using dsp::fhss::FHSSCorrelatorBankDetectorConfig;
using dsp::fhss::FHSSCorrelatorBankDetectorKernel;
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
  config.payload_random.rng_seed = 0x5eedu;
  config.payload_random.deterministic = true;
  return config;
}

FHSSSyntheticIqGeneratorConfig GeneratorConfig(std::uint32_t value) {
  FHSSSyntheticIqGeneratorConfig config{};
  config.decode_config = DecodeConfig();
  config.decode_config.preamble_pulses[0].word_value = value;
  config.decode_config.preamble_pulses[4].word_value = value;
  config.decode_config.preamble_pulses[8].word_value = value;
  config.decode_config.preamble_pulses[12].word_value = value;
  FHSSScheduledMessageSpec message{};
  message.message_id = 1;
  for (const auto &pulse : config.decode_config.preamble_pulses) {
    message.pulses.push_back(FHSSMessagePulseSpec{
        .frequency_index = pulse.frequency_index,
        .value = pulse.word_value,
        .role = FHSSMessagePulseRole::Preamble});
  }
  config.messages.push_back(std::move(message));
  return config;
}

std::vector<std::complex<double>> MakeCpsmEvidence(
    const std::vector<double> &symbols) {
  CPSMDecoderConfig config{};
  const auto timing = dsp::fhss::DeriveTimingModel(config.timing).value();

  std::vector<std::complex<double>> samples;
  samples.reserve(symbols.size() * timing.samples_per_symbol);
  std::uint32_t state = 0;
  for (const double symbol : symbols) {
    for (std::uint32_t sample = 0; sample < timing.samples_per_symbol;
         ++sample) {
      samples.push_back(dsp::fhss::CPSMPredictedSample(
          state, symbol, sample, timing.samples_per_symbol,
          config.modulation_index));
    }
    state = dsp::fhss::CPSMTransitionState(state, symbol);
  }
  return samples;
}

std::vector<double> ExpectedSymbolsForWord(std::uint32_t value) {
  std::vector<double> expected;
  expected.reserve(FHSSProtocolConstants::kBitsPerPulse);
  for (std::uint32_t i = 0; i < FHSSProtocolConstants::kBitsPerPulse; ++i) {
    expected.push_back(dsp::fhss::CpsmSymbolForBit(value, i));
  }
  return expected;
}

double SequenceMetric(const std::vector<std::complex<double>> &samples,
                      const std::vector<double> &symbols) {
  CPSMDecoderConfig config{};
  config.symbol_count = static_cast<std::uint32_t>(symbols.size());

  std::uint32_t state = config.initial_phase_state;
  double metric = 0.0;
  for (std::uint32_t i = 0; i < symbols.size(); ++i) {
    const auto branch =
        CPSMBranchMetricKernel::ScoreBranch(samples, i, state, symbols[i],
                                          config);
    metric += branch.cost;
    state = branch.to_state;
  }
  return metric;
}

std::vector<double> BruteForceOracleReduced(
    const std::vector<std::complex<double>> &samples,
    std::uint32_t symbol_count) {
  std::vector<double> best_symbols;
  double best_metric = std::numeric_limits<double>::infinity();
  const std::uint32_t combinations = 1u << symbol_count;

  for (std::uint32_t mask = 0; mask < combinations; ++mask) {
    std::vector<double> symbols;
    symbols.reserve(symbol_count);
    for (std::uint32_t i = 0; i < symbol_count; ++i) {
      symbols.push_back(((mask >> i) & 0x1u) == 0u ? 1.0 : -1.0);
    }
    const double metric = SequenceMetric(samples, symbols);
    if (metric < best_metric) {
      best_metric = metric;
      best_symbols = std::move(symbols);
    }
  }
  return best_symbols;
}

TEST(FHSSCpsmDecoderTest, RectangularFullResponseThetaIsContinuous) {
  const std::vector<double> symbols{1.0, -1.0, -1.0, 1.0};
  const auto samples = MakeCpsmEvidence(symbols);

  double max_step = 0.0;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    max_step = std::max(max_step, std::abs(std::arg(samples[i] *
                                                   std::conj(samples[i - 1]))));
  }

  EXPECT_LT(max_step, 0.1);
}

TEST(FHSSCpsmDecoderTest, TrellisTransitionsAreAccumulatedPhaseModuloTwoPi) {
  const auto transitions = dsp::fhss::BuildCPSMTrellisTransitions();

  ASSERT_EQ(transitions.size(), 8u);
  EXPECT_EQ(dsp::fhss::CPSMPhaseStateCount(), 4u);
  EXPECT_EQ(dsp::fhss::CPSMTransitionState(0, 1.0), 1u);
  EXPECT_EQ(dsp::fhss::CPSMTransitionState(0, -1.0), 3u);
  EXPECT_EQ(dsp::fhss::CPSMTransitionState(3, 1.0), 0u);
  EXPECT_EQ(dsp::fhss::CPSMTransitionState(0, -1.0), 3u);
  EXPECT_NEAR(dsp::fhss::CPSMStatePhaseRad(3), 1.5 * std::numbers::pi,
              1.0e-12);
}

TEST(FHSSCpsmDecoderTest, BranchMetricFavorsMatchingSymbolAndState) {
  const auto samples = MakeCpsmEvidence({1.0, -1.0});
  CPSMDecoderConfig config{};
  config.symbol_count = 2;

  const auto matching =
      CPSMBranchMetricKernel::ScoreBranch(samples, 0, 0, 1.0, config);
  const auto wrong =
      CPSMBranchMetricKernel::ScoreBranch(samples, 0, 0, -1.0, config);

  EXPECT_LT(matching.cost, 1.0e-12);
  EXPECT_GT(wrong.cost, matching.cost + 0.5);
  EXPECT_GT(matching.correlation, wrong.correlation);
}

TEST(FHSSCpsmDecoderTest, KnownGeneratedPulseDecodesToSymbols) {
  constexpr std::uint32_t kValue = 0xA5A5'5A5Au;
  const auto fixture =
      dsp::fhss::GenerateSyntheticIqFixture(GeneratorConfig(kValue));
  ASSERT_TRUE(fixture.has_value()) << fixture.error().message;

  FHSSCorrelatorBankDetectorConfig detector_config{};
  detector_config.decode_config = DecodeConfig();
  const auto detected =
      FHSSCorrelatorBankDetectorKernel::Detect(fixture->samples, detector_config);
  ASSERT_TRUE(detected.has_value()) << detected.error().message;
  ASSERT_FALSE(detected->local_detections.empty());
  ASSERT_TRUE(detected->local_detections.front().complex_evidence.samples);

  const auto decoded = CPSMViterbiDecoderKernel::Decode(
      *detected->local_detections.front().complex_evidence.samples);

  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_EQ(decoded->symbols, ExpectedSymbolsForWord(kValue));
  EXPECT_EQ(decoded->phase_states.front(), 0u);
  EXPECT_GT(decoded->confidence, 0.0);
}

TEST(FHSSCpsmDecoderTest, ViterbiMatchesReducedBruteForceOracle) {
  const std::vector<double> expected{1.0, -1.0, 1.0, 1.0, -1.0, -1.0};
  const auto samples = MakeCpsmEvidence(expected);
  CPSMDecoderConfig config{};
  config.symbol_count = static_cast<std::uint32_t>(expected.size());

  const auto decoded = CPSMViterbiDecoderKernel::Decode(samples, config);
  const auto oracle =
      BruteForceOracleReduced(samples, static_cast<std::uint32_t>(
                                           expected.size()));

  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_EQ(decoded->symbols, oracle);
  EXPECT_EQ(decoded->symbols, expected);
}

TEST(FHSSCpsmDecoderTest, TerminalPhasePolicyCanBeCheckedOrUnconstrained) {
  const std::vector<double> symbols{1.0, 1.0, -1.0};
  const auto samples = MakeCpsmEvidence(symbols);
  CPSMDecoderConfig config{};
  config.symbol_count = 3;

  auto decoded = CPSMViterbiDecoderKernel::Decode(samples, config);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_FALSE(decoded->terminal_phase_checked);
  EXPECT_EQ(decoded->terminal_phase_state, 1u);

  config.check_terminal_phase = true;
  config.expected_terminal_phase_state = 1;
  decoded = CPSMViterbiDecoderKernel::Decode(samples, config);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  EXPECT_TRUE(decoded->terminal_phase_checked);

  config.expected_terminal_phase_state = 2;
  decoded = CPSMViterbiDecoderKernel::Decode(samples, config);
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().code, FHSSValidationCode::InvalidTiming);
}

TEST(FHSSCpsmDecoderTest, MagnitudeOnlyInputIsImpossibleByDecoderType) {
  static_assert(std::is_invocable_v<decltype(&CPSMViterbiDecoderKernel::Decode),
                                    const std::vector<std::complex<double>> &,
                                    const CPSMDecoderConfig &>);
  static_assert(!std::is_invocable_v<decltype(&CPSMViterbiDecoderKernel::Decode),
                                     const std::vector<double> &,
                                     const CPSMDecoderConfig &>);
}

TEST(FHSSCpsmDecoderTest, InvalidEvidenceLengthIsRejected) {
  const std::vector<std::complex<double>> magnitude_like_samples(32, {1.0,
                                                                      0.0});

  const auto decoded = CPSMViterbiDecoderKernel::Decode(magnitude_like_samples);

  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error().code, FHSSValidationCode::InvalidGlobalTiming);
}

} // namespace
