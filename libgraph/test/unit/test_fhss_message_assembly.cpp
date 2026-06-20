// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "dsp/fhss/FHSSMessageAssembly.hpp"

namespace {

using dsp::fhss::FHSSAssembledMessage;
using dsp::fhss::FHSSDecodedPulseWord;
using dsp::fhss::FHSSMessageAssemblerConfig;
using dsp::fhss::FHSSMessageAssemblerKernel;
using dsp::fhss::FHSSMessageAssemblyStatus;
using dsp::fhss::FHSSMessageSinkKernel;
using dsp::fhss::FHSSPreambleDetectorKernel;
using dsp::fhss::FHSSPreamblePulseSpec;
using dsp::fhss::FHSSProtocolConstants;
using dsp::fhss::FHSSTruthMismatchKind;
using dsp::fhss::FHSSTruthPulse;

std::vector<FHSSPreamblePulseSpec> Preamble() {
  return {
      {1, 0x1111'1111u},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u}, {1, 0x1111'1111u},  {7, 0x7777'7777u},
      {12, 0x1212'1212u}, {62, 0x6262'6262u}, {1, 0x1111'1111u},
      {7, 0x7777'7777u},  {12, 0x1212'1212u}, {62, 0x6262'6262u},
      {1, 0x1111'1111u},  {7, 0x7777'7777u},  {12, 0x1212'1212u},
      {62, 0x6262'6262u},
  };
}

FHSSDecodedPulseWord Pulse(std::uint64_t slot, std::uint32_t frequency_index,
                           std::uint32_t value = 0xABCD'0000u) {
  FHSSDecodedPulseWord pulse{};
  pulse.candidate.detected_pulse.global_start_sample =
      slot * FHSSProtocolConstants::kPulsePeriodSamples;
  pulse.candidate.detected_pulse.global_end_sample =
      pulse.candidate.detected_pulse.global_start_sample +
      FHSSProtocolConstants::kPulseWidthSamples;
  pulse.candidate.detected_pulse.duration_samples =
      FHSSProtocolConstants::kPulseWidthSamples;
  pulse.candidate.detected_pulse.frequency_index = frequency_index;
  pulse.candidate.detected_pulse.rf_frequency_hz =
      1'000'000'000.0 + static_cast<double>(frequency_index) * 8'000'000.0;
  pulse.candidate.detected_pulse.iq_offset_frequency_hz =
      static_cast<double>(frequency_index) * 1'000'000.0;
  pulse.decoded_value = value;
  pulse.confidence = 0.9;
  pulse.viterbi_path_metric = 0.1;
  return pulse;
}

std::vector<FHSSDecodedPulseWord>
MessagePulses(std::size_t payload_count = 2) {
  std::vector<FHSSDecodedPulseWord> pulses;
  pulses.reserve(FHSSProtocolConstants::kPreamblePulseCount + payload_count);
  const auto preamble = Preamble();
  for (std::uint64_t i = 0; i < preamble.size(); ++i) {
    pulses.push_back(Pulse(i, preamble[i].frequency_index,
                           preamble[i].word_value));
  }
  for (std::uint64_t i = 0; i < payload_count; ++i) {
    const auto frequency = preamble[i % preamble.size()].frequency_index;
    pulses.push_back(Pulse(preamble.size() + i, frequency,
                           0xCAFE'0000u + static_cast<std::uint32_t>(i)));
  }
  return pulses;
}

std::vector<FHSSTruthPulse>
TruthFromDecoded(const std::vector<FHSSDecodedPulseWord> &pulses) {
  std::vector<FHSSTruthPulse> truth;
  truth.reserve(pulses.size());
  for (std::size_t i = 0; i < pulses.size(); ++i) {
    const auto &decoded = pulses[i];
    const auto &detected = decoded.candidate.detected_pulse;
    truth.push_back(FHSSTruthPulse{
        .global_start_sample = detected.global_start_sample,
        .duration_samples = detected.duration_samples,
        .frequency_index = detected.frequency_index,
        .rf_frequency_hz = detected.rf_frequency_hz,
        .iq_offset_frequency_hz = detected.iq_offset_frequency_hz,
        .value = decoded.decoded_value,
        .is_preamble = i < FHSSProtocolConstants::kPreamblePulseCount});
  }
  return truth;
}

FHSSMessageAssemblerConfig Config(
    const std::vector<FHSSDecodedPulseWord> &truth_source = {}) {
  FHSSMessageAssemblerConfig config{};
  config.preamble_pulses = Preamble();
  if (!truth_source.empty()) {
    config.truth_pulses = TruthFromDecoded(truth_source);
  }
  return config;
}

TEST(FHSSMessageAssemblyTest, LocksHopOnlyPreambleOverSixteenPulses) {
  auto pulses = MessagePulses();

  const auto lock = FHSSPreambleDetectorKernel::Detect(pulses, Preamble());

  EXPECT_TRUE(lock.preamble_lock);
  EXPECT_EQ(lock.status, FHSSMessageAssemblyStatus::Ok);
  EXPECT_EQ(lock.active_frequency_indices,
            (std::vector<std::uint32_t>{1, 7, 12, 62}));
}

TEST(FHSSMessageAssemblyTest, WordMismatchesDoNotPreventHopOnlyLock) {
  auto pulses = MessagePulses();
  for (std::size_t i = 0; i < FHSSProtocolConstants::kPreamblePulseCount; ++i) {
    pulses[i].decoded_value ^= 0xFFFF'FFFFu;
  }

  const auto assembled =
      FHSSMessageAssemblerKernel::Assemble(pulses, Config(MessagePulses()));

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::Ok);
  EXPECT_TRUE(assembled.diagnostics.preamble_lock);
  EXPECT_GT(assembled.diagnostics.truth_mismatch_count, 0u);
}

TEST(FHSSMessageAssemblyTest,
     IdenticalPreambleFrequenciesRequireIdenticalFixtureWords) {
  auto preamble = Preamble();
  preamble[4].word_value ^= 0x1u;

  const auto lock =
      FHSSPreambleDetectorKernel::Detect(MessagePulses(), preamble);

  EXPECT_FALSE(lock.preamble_lock);
  EXPECT_EQ(lock.status, FHSSMessageAssemblyStatus::InvalidPreambleFixture);
}

TEST(FHSSMessageAssemblyTest, RejectsInvalidActiveSetAfterLock) {
  auto preamble = Preamble();
  for (auto &pulse : preamble) {
    if (pulse.frequency_index == 62) {
      pulse.frequency_index = 12;
      pulse.word_value = 0x1212'1212u;
    }
  }
  auto pulses = MessagePulses();
  for (auto &pulse : pulses) {
    if (pulse.candidate.detected_pulse.frequency_index == 62) {
      pulse.candidate.detected_pulse.frequency_index = 12;
    }
  }

  const auto lock = FHSSPreambleDetectorKernel::Detect(pulses, preamble);

  EXPECT_FALSE(lock.preamble_lock);
  EXPECT_EQ(lock.status, FHSSMessageAssemblyStatus::InvalidActiveSet);
}

TEST(FHSSMessageAssemblyTest, RejectsPayloadFrequencyOutsideActiveSet) {
  auto pulses = MessagePulses();
  pulses.push_back(Pulse(18, 20, 0x2020'2020u));

  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::PayloadFrequencyRejected);
  EXPECT_EQ(assembled.diagnostics.rejected_count, pulses.size());
}

TEST(FHSSMessageAssemblyTest, RejectsMessageLongerThanTwoHundredFiftySixPulses) {
  auto pulses = MessagePulses(241);

  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::MessageTooLong);
  EXPECT_EQ(assembled.diagnostics.pulse_count, 257u);
}

TEST(FHSSMessageAssemblyTest, RejectsMissingPreamble) {
  auto pulses = MessagePulses();
  pulses[0].candidate.detected_pulse.frequency_index = 7;

  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::MissingPreamble);
  EXPECT_FALSE(assembled.diagnostics.preamble_lock);
}

TEST(FHSSMessageAssemblyTest, OperatesOnGloballyOrderedDecodedPulses) {
  auto pulses = MessagePulses();
  std::reverse(pulses.begin(), pulses.end());

  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::Ok);
  ASSERT_GE(assembled.ordered_pulses.size(), 2u);
  EXPECT_LT(assembled.ordered_pulses[0]
                .candidate.detected_pulse.global_start_sample,
            assembled.ordered_pulses[1]
                .candidate.detected_pulse.global_start_sample);
  EXPECT_TRUE(assembled.diagnostics.preamble_lock);
}

TEST(FHSSMessageAssemblyTest,
     TruthComparatorReportsStartDurationFrequencyAndValueMismatches) {
  auto pulses = MessagePulses();
  auto truth = TruthFromDecoded(pulses);
  truth[0].global_start_sample += 1;
  truth[0].duration_samples += 1;
  truth[0].frequency_index = 7;
  truth[0].value ^= 0x1u;

  FHSSMessageAssemblerConfig config{};
  config.preamble_pulses = Preamble();
  config.truth_pulses = truth;
  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, config);

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::Ok);
  EXPECT_EQ(assembled.diagnostics.truth_mismatch_count, 4u);
  std::vector<FHSSTruthMismatchKind> kinds;
  for (const auto &mismatch : assembled.truth_mismatches) {
    kinds.push_back(mismatch.kind);
  }
  EXPECT_TRUE(std::ranges::find(kinds, FHSSTruthMismatchKind::StartSample) !=
              kinds.end());
  EXPECT_TRUE(std::ranges::find(kinds, FHSSTruthMismatchKind::Duration) !=
              kinds.end());
  EXPECT_TRUE(std::ranges::find(kinds, FHSSTruthMismatchKind::Frequency) !=
              kinds.end());
  EXPECT_TRUE(std::ranges::find(kinds, FHSSTruthMismatchKind::Value) !=
              kinds.end());
}

TEST(FHSSMessageAssemblyTest, SinkReportsMinimumDiagnostics) {
  auto pulses = MessagePulses();
  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  const auto diagnostics = FHSSMessageSinkKernel::Diagnostics(assembled);

  EXPECT_EQ(diagnostics.pulse_count, pulses.size());
  EXPECT_EQ(diagnostics.rejected_count, 0u);
  EXPECT_TRUE(diagnostics.preamble_lock);
  EXPECT_EQ(diagnostics.truth_mismatch_count, 0u);
}

TEST(FHSSMessageAssemblyTest, RejectsPr1OverlappedMessagesDeterministically) {
  auto pulses = MessagePulses();
  pulses[1].candidate.detected_pulse.global_start_sample =
      pulses[0].candidate.detected_pulse.global_start_sample + 10;
  pulses[1].candidate.detected_pulse.global_end_sample =
      pulses[1].candidate.detected_pulse.global_start_sample +
      FHSSProtocolConstants::kPulseWidthSamples;

  const auto assembled = FHSSMessageAssemblerKernel::Assemble(pulses, Config());

  EXPECT_EQ(assembled.status, FHSSMessageAssemblyStatus::UnsupportedOverlap);
  EXPECT_FALSE(assembled.diagnostics.preamble_lock);
}

} // namespace
