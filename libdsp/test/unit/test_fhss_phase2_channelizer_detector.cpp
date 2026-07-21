#include <gtest/gtest.h>

#include "dsp/fhss/CPSMBranchMetricNode.hpp"
#include "dsp/fhss/CPSMViterbiDecoderNode.hpp"
#include "dsp/fhss/FHSSAcquisitionPulseDetectorNode.hpp"
#include "dsp/fhss/FHSSProductionCandidateChannelizerNode.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace dsp::fhss {
namespace {

FHSSProductionChannelizerConfig ProductionConfig() {
  FHSSProductionChannelizerConfig config{};
  config.frequency.occupied_bandwidth_hz = 5'000'000.0;
  config.frequency.max_abs_cfo_hz = 1'000.0;
  config.receiver_frequency_indices = FHSSAllFrequencyIndices();
  config.channel_ids = FHSSAllFrequencyIndices();
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    config.frequency.iq_offset_frequency_hz[index] =
        -236'250'000.0 + 7'500'000.0 * index;
  }
  return config;
}

double IndependentFirMagnitude(const std::vector<double> &coefficients,
                               double frequency_hz, double sample_rate_hz) {
  std::complex<double> response{0.0, 0.0};
  for (std::size_t tap = 0; tap < coefficients.size(); ++tap) {
    const double phase =
        -2.0 * std::numbers::pi * frequency_hz * tap / sample_rate_hz;
    response += coefficients[tap] *
                std::complex<double>(std::cos(phase), std::sin(phase));
  }
  return std::abs(response);
}

std::vector<std::complex<double>> Tone(std::size_t count, double frequency_hz,
                                       double sample_rate_hz,
                                       double amplitude = 1.0,
                                       double phase_offset = 0.0) {
  std::vector<std::complex<double>> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const double phase = phase_offset + 2.0 * std::numbers::pi * frequency_hz *
                                            index / sample_rate_hz;
    result.emplace_back(amplitude * std::cos(phase),
                        amplitude * std::sin(phase));
  }
  return result;
}

double Rms(const std::vector<std::complex<double>> &samples) {
  const double power = std::accumulate(
      samples.begin(), samples.end(), 0.0,
      [](double sum, const auto &sample) { return sum + std::norm(sample); });
  return samples.empty()
             ? 0.0
             : std::sqrt(power / static_cast<double>(samples.size()));
}

std::vector<std::complex<double>>
IndependentFirDecimate(const std::vector<std::complex<double>> &input,
                       const std::vector<double> &coefficients,
                       std::uint32_t decimation) {
  std::vector<std::complex<double>> result;
  for (std::size_t sample = coefficients.size() - 1u; sample < input.size();
       ++sample) {
    if (sample % decimation != 0u) {
      continue;
    }
    std::complex<double> value{0.0, 0.0};
    for (std::size_t tap = 0; tap < coefficients.size(); ++tap) {
      value += coefficients[tap] * input[sample - tap];
    }
    result.push_back(value);
  }
  return result;
}

FHSSChannelizedIqToken
ChannelToken(std::shared_ptr<const std::vector<std::complex<double>>> samples,
             std::uint64_t input_global_start, bool terminal,
             std::uint32_t decimation = 10u) {
  FHSSChannelizedIqToken token{};
  token.token_id = input_global_start + 1u;
  if (terminal) {
    token.edge_control = graph::EdgeEndOfStream{};
  }
  token.sidecar.channel.channel_id = 24;
  token.sidecar.channel.frequency_index = 24;
  token.sidecar.channel.rf_frequency_hz = RfFrequencyHz(24);
  token.sidecar.channel.iq_offset_frequency_hz = 0.0;
  token.sidecar.channel.channel_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz / decimation;
  token.sidecar.channel.decimation_factor = decimation;
  token.sidecar.channel.input_global_start_sample = input_global_start;
  token.sidecar.channel.channel_global_start_sample = input_global_start;
  token.sidecar.channel.sample_time_map.input_packet_global_start_sample =
      input_global_start;
  token.sidecar.channel.sample_time_map.decimation_factor = decimation;
  token.sidecar.channel.sample_time_map.input_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz;
  token.sidecar.channel.sample_time_map.output_sample_rate_hz =
      FHSSProtocolConstants::kSampleRateHz / decimation;
  const auto sample_count = samples->size();
  token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
      std::move(samples), sample_count, token.sidecar.channel.sample_time_map);
  return token;
}

FHSSDownconvertedIqToken DownconvertedToken(
    std::shared_ptr<const std::vector<std::complex<double>>> samples,
    std::uint64_t global_start, bool terminal) {
  FHSSDownconvertedIqToken token{};
  token.token_id = global_start + 1u;
  if (terminal) {
    token.edge_control = graph::EdgeEndOfStream{};
  }
  token.sidecar.downconverter.passthrough = true;
  auto map = token.sidecar.iq.sample_time_map;
  map.input_packet_global_start_sample = global_start;
  map.input_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  map.output_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  const auto count = samples->size();
  token.sidecar.iq =
      FHSSGraphXComplexEvidenceFromHostSamples(std::move(samples), count, map);
  return token;
}

template <std::size_t Target, std::size_t Port = 0>
std::optional<FHSSChannelizedIqToken>
DrainChannelizer(FHSSProductionCandidateChannelizerNode &channelizer) {
  auto current = channelizer.ProduceOutput<Port>();
  std::optional<FHSSChannelizedIqToken> selected;
  if constexpr (Port == Target) {
    selected = std::move(current);
  }
  if constexpr (Port + 1u < FHSSProtocolConstants::kFrequencyCount) {
    auto tail = DrainChannelizer<Target, Port + 1u>(channelizer);
    if (tail.has_value()) {
      selected = std::move(tail);
    }
  }
  return selected;
}

std::vector<std::complex<double>> CaptureWithPulses(
    const std::vector<std::uint64_t> &starts, std::uint64_t count = 2'000,
    std::uint32_t decimation = 10u, double amplitude = 1.0,
    double phase_offset = 0.0, double cfo_hz = 0.0, double noise_sigma = 0.0,
    std::uint64_t seed = 91001, std::uint32_t word = 0xA5C3'7E19u) {
  std::vector<std::complex<double>> samples(count, {0.0, 0.0});
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> normal(0.0, noise_sigma);
  for (auto &sample : samples) {
    sample = {normal(rng), normal(rng)};
  }
  const auto width = FHSSProtocolConstants::kPulseWidthSamples / decimation;
  const auto samples_per_symbol =
      FHSSProtocolConstants::kSamplesPerSymbol / decimation;
  for (const auto start : starts) {
    double phase = phase_offset;
    for (std::uint64_t offset = 0;
         offset < width && start + offset < samples.size(); ++offset) {
      const auto bit = static_cast<std::uint32_t>(offset / samples_per_symbol);
      const bool one = ((word >> (31u - bit)) & 1u) != 0u;
      const double symbol = one ? -1.0 : 1.0;
      phase += symbol * std::numbers::pi /
               (2.0 * static_cast<double>(samples_per_symbol));
      phase += 2.0 * std::numbers::pi * cfo_hz /
               (FHSSProtocolConstants::kSampleRateHz / decimation);
      samples[start + offset] +=
          amplitude * std::complex<double>(std::cos(phase), std::sin(phase));
    }
  }
  return samples;
}

TEST(FHSSPhase2ChannelizerTest,
     ProfileCentersAreDistinctFiniteAndInsideGuardedNyquist) {
  const auto config = ProductionConfig();
  ASSERT_TRUE(ValidateFHSSProductionChannelizerConfig(config).has_value());
  std::set<double> offsets;
  for (const auto offset : config.frequency.iq_offset_frequency_hz) {
    EXPECT_TRUE(std::isfinite(offset));
    EXPECT_LT(std::abs(offset), 242'500'000.0);
    EXPECT_TRUE(offsets.insert(offset).second);
  }
  EXPECT_EQ(offsets.size(), 64u);
}

TEST(FHSSPhase2ChannelizerTest,
     AnalyticalResponseMeetsProvisionalSelectivityAndDelayTargets) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  ASSERT_EQ(coefficients.size(), 241u);
  EXPECT_NEAR(std::accumulate(coefficients.begin(), coefficients.end(), 0.0),
              1.0, 1.0e-12);
  for (std::size_t index = 0; index < coefficients.size(); ++index) {
    EXPECT_NEAR(coefficients[index],
                coefficients[coefficients.size() - 1u - index], 1.0e-15);
  }
  const double center = IndependentFirMagnitude(
      coefficients, 0.0, config.frequency.sample_rate_hz);
  const double edge = IndependentFirMagnitude(
      coefficients, config.passband_edge_hz, config.frequency.sample_rate_hz);
  const double adjacent = IndependentFirMagnitude(
      coefficients, 7'500'000.0, config.frequency.sample_rate_hz);
  const double alternate = IndependentFirMagnitude(
      coefficients, 15'000'000.0, config.frequency.sample_rate_hz);
  EXPECT_LT(-20.0 * std::log10(edge / center), 1.75);
  EXPECT_GT(-20.0 * std::log10(adjacent / center), 50.0);
  EXPECT_GT(-20.0 * std::log10(alternate / center), 50.0);
  EXPECT_EQ((coefficients.size() - 1u) / 2u, 120u);
}

TEST(FHSSPhase2ChannelizerTest,
     StatefulFirPacketSplitsExactlyMatchOneShotReference) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  auto input =
      Tone(2'003, 1'250'000.0, config.frequency.sample_rate_hz, 0.7, 0.31);
  FHSSFirChannelizerKernel one_shot(coefficients, config.decimation_factor,
                                    1'250'000.0,
                                    config.frequency.sample_rate_hz);
  auto expected = one_shot.Process(input, 0);
  ASSERT_TRUE(expected.has_value());

  for (const std::size_t split : {1u, 59u, 60u, 121u, 157u, 997u, 2'002u}) {
    FHSSFirChannelizerKernel packetized(coefficients, config.decimation_factor,
                                        1'250'000.0,
                                        config.frequency.sample_rate_hz);
    const std::vector<std::complex<double>> first(input.begin(),
                                                  input.begin() + split);
    const std::vector<std::complex<double>> second(input.begin() + split,
                                                   input.end());
    auto lhs = packetized.Process(first, 0);
    auto rhs = packetized.Process(second, split);
    ASSERT_TRUE(lhs.has_value());
    ASSERT_TRUE(rhs.has_value());
    auto actual = lhs->samples;
    actual.insert(actual.end(), rhs->samples.begin(), rhs->samples.end());
    ASSERT_EQ(actual.size(), expected->samples.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
      EXPECT_EQ(actual[index], expected->samples[index]);
    }
  }
}

TEST(FHSSPhase2ChannelizerTest,
     ValidationSizedCaptureUsesFixedHistoryAndLinearInsertionWork) {
  constexpr std::size_t kValidationCaptureSamples = 188'876u;
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  const auto input = Tone(kValidationCaptureSamples, 3'750'000.0,
                          config.frequency.sample_rate_hz, 0.25, 0.37);
  FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor,
                                  3'750'000.0,
                                  config.frequency.sample_rate_hz);

  const auto started = std::chrono::steady_clock::now();
  const auto output = kernel.Process(input, 0u);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  ASSERT_TRUE(output.has_value());
  EXPECT_EQ(kernel.HistoryStorageSize(), coefficients.size());
  EXPECT_EQ(kernel.HistoryWriteCount(), input.size());
  EXPECT_EQ(kernel.HistoryOverwriteCount(),
            input.size() - coefficients.size());
  EXPECT_EQ(output->samples.size(), 18'864u);
  RecordProperty(
      "elapsed_microseconds",
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
  RecordProperty("history_storage_elements", kernel.HistoryStorageSize());
  RecordProperty("history_writes", kernel.HistoryWriteCount());
  RecordProperty("history_overwrites", kernel.HistoryOverwriteCount());
}

TEST(FHSSPhase2ChannelizerTest,
     ExactZeroShortcutMatchesIndependentNormalPathAndStreamingState) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  constexpr std::uint64_t kGlobalStart = 7u;
  const std::vector<std::complex<double>> zeros(12'345u, {0.0, 0.0});
  FHSSFirChannelizerKernel normal(coefficients, config.decimation_factor,
                                  3'750'000.0,
                                  config.frequency.sample_rate_hz);
  FHSSFirChannelizerKernel shortcut(coefficients, config.decimation_factor,
                                    3'750'000.0,
                                    config.frequency.sample_rate_hz);

  const auto expected = normal.Process(zeros, kGlobalStart);
  const auto actual =
      shortcut.ProcessZerosFromReset(zeros.size(), kGlobalStart);
  ASSERT_TRUE(expected.has_value());
  ASSERT_TRUE(actual.has_value());
  EXPECT_EQ(actual->has_output, expected->has_output);
  EXPECT_EQ(actual->first_causal_input_global_sample,
            expected->first_causal_input_global_sample);
  EXPECT_EQ(actual->samples, expected->samples);
  EXPECT_EQ(shortcut.HistoryStorageSize(), normal.HistoryStorageSize());
  EXPECT_EQ(shortcut.HistoryWriteCount(), normal.HistoryWriteCount());
  EXPECT_EQ(shortcut.HistoryOverwriteCount(), normal.HistoryOverwriteCount());
  EXPECT_EQ(shortcut.HistoryNonzeroCount(), 0u);
  EXPECT_EQ(normal.HistoryNonzeroCount(), 0u);

  const auto continuation =
      Tone(800u, 3'750'000.0, config.frequency.sample_rate_hz, 0.25, 0.17);
  const auto expected_continuation =
      normal.Process(continuation, kGlobalStart + zeros.size());
  const auto actual_continuation =
      shortcut.Process(continuation, kGlobalStart + zeros.size());
  ASSERT_TRUE(expected_continuation.has_value());
  ASSERT_TRUE(actual_continuation.has_value());
  ASSERT_EQ(actual_continuation->samples.size(),
            expected_continuation->samples.size());
  for (std::size_t index = 0; index < expected_continuation->samples.size();
       ++index) {
    EXPECT_NEAR(actual_continuation->samples[index].real(),
                expected_continuation->samples[index].real(), 1.0e-12);
    EXPECT_NEAR(actual_continuation->samples[index].imag(),
                expected_continuation->samples[index].imag(), 1.0e-12);
  }
}

TEST(FHSSPhase2ChannelizerTest,
     SparseZeroRunsMatchIndependentFirAcrossPacketsAndHistoryFlushes) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  std::vector<std::complex<double>> input(901u, {0.0, 0.0});
  for (std::size_t index = 32u; index < 64u; ++index) {
    input[index] = {0.2 + 0.001 * static_cast<double>(index),
                    -0.1 + 0.0005 * static_cast<double>(index)};
  }
  for (std::size_t index = 400u; index < 450u; ++index) {
    input[index] = {-0.3 + 0.0007 * static_cast<double>(index),
                    0.15 - 0.0002 * static_cast<double>(index)};
  }
  for (std::size_t index = 801u; index < input.size(); ++index) {
    input[index] = {0.4 - 0.0003 * static_cast<double>(index),
                    0.05 + 0.0001 * static_cast<double>(index)};
  }
  const auto expected =
      IndependentFirDecimate(input, coefficients, config.decimation_factor);

  FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor, 0.0,
                                  config.frequency.sample_rate_hz);
  std::vector<std::complex<double>> actual;
  const std::array<std::size_t, 5> packet_ends{300u, 400u, 520u, 801u,
                                               input.size()};
  const std::array<std::size_t, 5> expected_nonzero_counts{5u, 0u, 50u, 0u,
                                                           100u};
  std::size_t packet_start = 0u;
  for (std::size_t packet = 0; packet < packet_ends.size(); ++packet) {
    const auto packet_end = packet_ends[packet];
    const std::vector<std::complex<double>> samples(
        input.begin() + static_cast<std::ptrdiff_t>(packet_start),
        input.begin() + static_cast<std::ptrdiff_t>(packet_end));
    auto result = kernel.Process(samples, packet_start);
    ASSERT_TRUE(result.has_value()) << packet;
    actual.insert(actual.end(), result->samples.begin(), result->samples.end());
    EXPECT_EQ(kernel.HistoryNonzeroCount(), expected_nonzero_counts[packet])
        << packet;
    packet_start = packet_end;
  }

  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_NEAR(actual[index].real(), expected[index].real(), 1.0e-12)
        << index;
    EXPECT_NEAR(actual[index].imag(), expected[index].imag(), 1.0e-12)
        << index;
  }
  EXPECT_EQ(kernel.HistoryWriteCount(), input.size());
  EXPECT_EQ(kernel.HistoryOverwriteCount(),
            input.size() - coefficients.size());
  kernel.Reset();
  EXPECT_EQ(kernel.HistoryNonzeroCount(), 0u);
}

TEST(FHSSPhase2ChannelizerTest,
     CancellableKernelReportsTeardownWithoutValidationFailureOrOutput) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor,
                                  3'750'000.0,
                                  config.frequency.sample_rate_hz);
  const std::atomic<bool> cancellation_requested{true};

  const auto result = kernel.ProcessCancellable(
      Tone(16'384u, 3'750'000.0, config.frequency.sample_rate_hz), 0u,
      cancellation_requested);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->has_value());
  EXPECT_EQ(kernel.HistoryStorageSize(), 0u);
  EXPECT_EQ(kernel.HistoryWriteCount(), 0u);
  EXPECT_EQ(kernel.HistoryNonzeroCount(), 0u);
}

TEST(FHSSPhase2ChannelizerTest,
     ProductionNodeStopInterruptsInFlightFirWithoutPartialOutput) {
  constexpr std::size_t kLongCaptureSamples = 1'048'576u;
  const auto samples =
      std::make_shared<const std::vector<std::complex<double>>>(Tone(
          kLongCaptureSamples,
          ProductionConfig().frequency.iq_offset_frequency_hz[24],
          FHSSProtocolConstants::kSampleRateHz, 0.25, 0.37));
  FHSSProductionCandidateChannelizerNode channelizer(ProductionConfig());
  ASSERT_TRUE(channelizer.Init());
  ASSERT_TRUE(channelizer.Start());
  std::atomic<bool> consume_finished{false};
  std::atomic<bool> consume_result{true};
  std::jthread worker([&] {
    consume_result.store(
        channelizer.ConsumeInput<0>(DownconvertedToken(samples, 0u, true)),
        std::memory_order_release);
    consume_finished.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ASSERT_FALSE(consume_finished.load(std::memory_order_acquire));
  const auto stop_started = std::chrono::steady_clock::now();
  channelizer.Stop();
  worker.join();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
  channelizer.Join();

  EXPECT_FALSE(consume_result.load(std::memory_order_acquire));
  EXPECT_LT(stop_elapsed, std::chrono::seconds(1));
  for (std::size_t port = 0;
       port < FHSSProtocolConstants::kFrequencyCount; ++port) {
    const auto *metrics = channelizer.GetOutputQueueMetrics(port);
    ASSERT_NE(metrics, nullptr);
    EXPECT_EQ(metrics->current_size.load(), 0u) << port;
  }
}

TEST(FHSSPhase2ChannelizerTest,
     IndependentImpulseMultitoneAndBroadbandReferenceMatchesKernel) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  std::mt19937_64 rng(91'003);
  std::normal_distribution<double> noise(0.0, 0.01);
  std::vector<std::complex<double>> input(4'096);
  for (std::size_t sample = 0; sample < input.size(); ++sample) {
    const double t =
        static_cast<double>(sample) / config.frequency.sample_rate_hz;
    input[sample] =
        0.4 * std::polar(1.0, 2.0 * std::numbers::pi * 2'500'000.0 * t) +
        0.2 * std::polar(1.0, -2.0 * std::numbers::pi * 2'500'000.0 * t) +
        0.7 * std::polar(1.0, 2.0 * std::numbers::pi * 12'000'000.0 * t) +
        std::complex<double>{noise(rng), noise(rng)};
  }
  input.front() += std::complex<double>{1.0, 0.0};
  const auto expected =
      IndependentFirDecimate(input, coefficients, config.decimation_factor);
  FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor, 0.0,
                                  config.frequency.sample_rate_hz);
  const auto actual = kernel.Process(input, 0);
  ASSERT_TRUE(actual.has_value());
  ASSERT_EQ(actual->samples.size(), expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    EXPECT_NEAR(actual->samples[index].real(), expected[index].real(), 1.0e-12);
    EXPECT_NEAR(actual->samples[index].imag(), expected[index].imag(), 1.0e-12);
  }
}

TEST(FHSSPhase2ChannelizerTest,
     EveryConfiguredCenterMixesToDcWithExpectedGainIncludingReservedPorts) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  for (std::uint32_t index = 0; index < FHSSProtocolConstants::kFrequencyCount;
       ++index) {
    const double center = config.frequency.iq_offset_frequency_hz[index];
    auto input = Tone(600, center, config.frequency.sample_rate_hz, 0.75,
                      static_cast<double>(index) * 0.01);
    FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor,
                                    center, config.frequency.sample_rate_hz);
    auto output = kernel.Process(input, 0);
    ASSERT_TRUE(output.has_value()) << index;
    ASSERT_FALSE(output->samples.empty()) << index;
    const auto mean =
        std::accumulate(output->samples.begin(), output->samples.end(),
                        std::complex<double>{0.0, 0.0}) /
        static_cast<double>(output->samples.size());
    EXPECT_NEAR(std::abs(mean), 0.75, 1.0e-10) << index;
    EXPECT_EQ(IsReservedFrequencyIndex(index), index == 0u || index == 63u);
  }
}

TEST(FHSSPhase2ChannelizerTest,
     MultitoneNearFarVectorShowsIndependentAdjacentAndAlternateLeakage) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  const double adjacent = IndependentFirMagnitude(
      coefficients, 7'500'000.0, config.frequency.sample_rate_hz);
  const double alternate = IndependentFirMagnitude(
      coefficients, 15'000'000.0, config.frequency.sample_rate_hz);
  const double near_far_amplitude = std::pow(10.0, 30.0 / 20.0);
  EXPECT_LT(near_far_amplitude * adjacent, 0.5);
  EXPECT_LT(near_far_amplitude * alternate, 0.01);
  const double enbw =
      config.frequency.sample_rate_hz *
      std::inner_product(coefficients.begin(), coefficients.end(),
                         coefficients.begin(), 0.0);
  EXPECT_GT(enbw, 5'000'000.0);
  EXPECT_LT(enbw, 10'000'000.0);
}

TEST(FHSSPhase2ChannelizerTest,
     EveryDecimationFoldingBandIsSuppressedAndMatchesIndependentOracle) {
  const auto config = ProductionConfig();
  const auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  for (const double frequency_hz :
       {12'000'000.0, 50'000'000.0, 100'000'000.0, 150'000'000.0, 200'000'000.0,
        237'500'000.0}) {
    const auto input =
        Tone(4'096, frequency_hz, config.frequency.sample_rate_hz);
    const auto expected =
        IndependentFirDecimate(input, coefficients, config.decimation_factor);
    FHSSFirChannelizerKernel kernel(coefficients, config.decimation_factor, 0.0,
                                    config.frequency.sample_rate_hz);
    const auto actual = kernel.Process(input, 0u);
    ASSERT_TRUE(actual.has_value());
    EXPECT_GT(kernel.AllocationHighWaterBytes(), 0u);
    ASSERT_EQ(actual->samples.size(), expected.size());
    EXPECT_NEAR(Rms(input), 1.0, 1.0e-12);
    EXPECT_LT(20.0 * std::log10(Rms(actual->samples) / Rms(input)), -50.0)
        << frequency_hz;
    for (std::size_t index = 0; index < expected.size(); ++index) {
      EXPECT_NEAR(actual->samples[index].real(), expected[index].real(),
                  1.0e-12);
      EXPECT_NEAR(actual->samples[index].imag(), expected[index].imag(),
                  1.0e-12);
    }
  }
}

TEST(FHSSPhase2ChannelizerTest,
     KernelRejectsDiscontinuousPacketsAndConfigurationRejectsAliases) {
  auto config = ProductionConfig();
  auto coefficients =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  FHSSFirChannelizerKernel kernel(coefficients, 10, 0.0,
                                  config.frequency.sample_rate_hz);
  ASSERT_TRUE(
      kernel.Process(Tone(32, 0.0, config.frequency.sample_rate_hz), 0));
  EXPECT_FALSE(
      kernel.Process(Tone(8, 0.0, config.frequency.sample_rate_hz), 40));

  config.frequency.iq_offset_frequency_hz[1] =
      config.frequency.iq_offset_frequency_hz[0];
  EXPECT_FALSE(ValidateFHSSProductionChannelizerConfig(config));
  config = ProductionConfig();
  config.frequency.iq_offset_frequency_hz[63] = 249'000'000.0;
  EXPECT_FALSE(ValidateFHSSProductionChannelizerConfig(config));
  config = ProductionConfig();
  config.cutoff_frequency_hz = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(ValidateFHSSProductionChannelizerConfig(config));
}

TEST(FHSSPhase2ChannelizerTest,
     EveryTerminalControlPropagatesAndResetsFirPacketContinuity) {
  const auto samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          Tone(600, ProductionConfig().frequency.iq_offset_frequency_hz[24],
               FHSSProtocolConstants::kSampleRateHz));
  for (const graph::EdgeControl &terminal :
       {graph::EdgeControl{graph::EdgeEndOfStream{}},
        graph::EdgeControl{graph::EdgeCancellation{"test cancellation"}},
        graph::EdgeControl{graph::EdgeFailure{"test failure"}}}) {
    FHSSProductionCandidateChannelizerNode channelizer(ProductionConfig());
    auto first = DownconvertedToken(samples, 0u, false);
    first.edge_control = terminal;
    ASSERT_TRUE(channelizer.ConsumeInput<0>(first));
    EXPECT_GT(channelizer.AllocationHighWaterBytes(), 0u);
    const auto terminal_output = DrainChannelizer<24>(channelizer);
    ASSERT_TRUE(terminal_output.has_value());
    EXPECT_EQ(terminal_output->edge_control, terminal);
    EXPECT_TRUE(
        FHSSAcquisitionDetectorMetadataIsValid(terminal_output->sidecar));

    ASSERT_TRUE(
        channelizer.ConsumeInput<0>(DownconvertedToken(samples, 0u, true)));
    const auto restarted = DrainChannelizer<24>(channelizer);
    ASSERT_TRUE(restarted.has_value());
    EXPECT_TRUE(std::holds_alternative<graph::EdgeEndOfStream>(
        restarted->edge_control));
    EXPECT_TRUE(FHSSAcquisitionDetectorMetadataIsValid(restarted->sidecar));
  }
}

TEST(FHSSPhase2ChannelizerTest,
     ValidationSizedProductionNodeCompletesAll64LanesWithFixedHistory) {
  constexpr std::size_t kValidationCaptureSamples = 188'876u;
  const auto samples =
      std::make_shared<const std::vector<std::complex<double>>>(Tone(
          kValidationCaptureSamples,
          ProductionConfig().frequency.iq_offset_frequency_hz[24],
          FHSSProtocolConstants::kSampleRateHz, 0.25, 0.37));
  FHSSProductionCandidateChannelizerNode channelizer(ProductionConfig());
  const auto input = DownconvertedToken(samples, 0u, true);

  const auto started = std::chrono::steady_clock::now();
  ASSERT_TRUE(channelizer.ConsumeInput<0>(input));
  const auto channel24 = DrainChannelizer<24>(channelizer);
  const auto elapsed = std::chrono::steady_clock::now() - started;

  ASSERT_TRUE(channel24.has_value());
  EXPECT_TRUE(std::holds_alternative<graph::EdgeEndOfStream>(
      channel24->edge_control));
  EXPECT_EQ(channel24->sidecar.iq.sample_count, 18'864u);
  EXPECT_GT(channelizer.AllocationHighWaterBytes(), 0u);
  RecordProperty(
      "elapsed_microseconds",
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
  RecordProperty("input_samples", kValidationCaptureSamples);
  RecordProperty("lane_count", FHSSProtocolConstants::kFrequencyCount);
}

TEST(FHSSPhase2DetectorTest,
     AcquiresUnknownEpochMultipleBurstsAndMeasuresNoiseFromEvidence) {
  FHSSAcquisitionPulseDetectorKernel kernel;
  const auto samples = CaptureWithPulses({137, 1'037}, 1'700, 10, 0.8, 0.23,
                                         20'000.0, 0.01, 91001);
  auto detections = kernel.Detect(samples, 10);
  ASSERT_TRUE(detections.has_value());
  ASSERT_EQ(detections->size(), 2u);
  EXPECT_NEAR(detections->at(0).begin, 137u, 2u);
  EXPECT_NEAR(detections->at(1).begin, 1'037u, 2u);
  EXPECT_GT(detections->at(0).noise_power_linear, 0.0);
  EXPECT_GT(detections->at(0).mean_power_linear,
            detections->at(0).noise_power_linear * 8.0);
}

TEST(FHSSPhase2DetectorTest,
     WholeHopTwoTransmitterCaptureHasBoundedCandidateAndDecodeWork) {
  constexpr std::uint64_t kEpoch = 100u;
  constexpr std::uint64_t kPulsePeriod = 650u;
  constexpr std::uint64_t kRelativeStart = 650u;
  constexpr std::uint64_t kCaptureSamples = 12'000u;
  constexpr double kWeakAmplitude = 0.251188643150958;
  std::size_t total_candidates = 0u;
  std::size_t maximum_channel_candidates = 0u;

  for (std::uint32_t frequency_index = 0;
       frequency_index < FHSSProtocolConstants::kFrequencyCount;
       ++frequency_index) {
    std::vector<std::uint64_t> wanted_starts;
    std::vector<std::uint64_t> weak_starts;
    for (std::uint32_t ordinal = 0; ordinal < 17u; ++ordinal) {
      const auto pulse_frequency = ordinal < 16u
                                       ? 24u + 4u * (ordinal % 4u)
                                       : 32u;
      if (pulse_frequency == frequency_index) {
        wanted_starts.push_back(kEpoch + ordinal * kPulsePeriod);
        weak_starts.push_back(kEpoch + kRelativeStart +
                              ordinal * kPulsePeriod);
      }
    }

    auto capture = CaptureWithPulses(wanted_starts, kCaptureSamples, 10u,
                                     1.0, 0.41, 0.0, 0.01, 38'011u);
    const auto weak =
        CaptureWithPulses(weak_starts, kCaptureSamples, 10u, kWeakAmplitude,
                          -0.27, 0.0, 0.0, 38'012u);
    std::ranges::transform(capture, weak, capture.begin(), std::plus{});

    FHSSAcquisitionPulseDetectorKernel kernel;
    const auto detections = kernel.Detect(capture, 10u);
    ASSERT_TRUE(detections.has_value()) << frequency_index;
    total_candidates += detections->size();
    maximum_channel_candidates =
        std::max(maximum_channel_candidates, detections->size());
  }

  const auto estimated_decoded_samples =
      total_candidates * FHSSProtocolConstants::kPulseWidthSamples;
  RecordProperty("total_candidates", total_candidates);
  RecordProperty("maximum_channel_candidates", maximum_channel_candidates);
  RecordProperty("estimated_decoded_samples", estimated_decoded_samples);
  EXPECT_EQ(total_candidates, 34u);
  EXPECT_EQ(maximum_channel_candidates, 10u);
  EXPECT_EQ(estimated_decoded_samples, 108'800u);
}

TEST(FHSSPhase2DetectorTest,
     CoversEveryDecimatedIntegerEpochAndAmplitudePhaseCfoSweep) {
  FHSSAcquisitionPulseDetectorKernel kernel;
  for (std::uint64_t epoch = 0; epoch < 650; ++epoch) {
    const auto start = 100u + epoch;
    const double amplitude = epoch % 2u == 0u ? 0.1 : 2.0;
    const double phase = static_cast<double>(epoch % 17u) * 0.17;
    const double cfo = epoch % 3u == 0u ? -25'000.0 : 25'000.0;
    const auto samples = CaptureWithPulses(
        {start}, start + 320u + 330u, 10, amplitude, phase, cfo,
        amplitude / std::sqrt(20.0), 91'001u + epoch,
        static_cast<std::uint32_t>(0x9E37'79B9u * (epoch + 1u)));
    auto detections = kernel.Detect(samples, 10);
    ASSERT_TRUE(detections.has_value()) << epoch;
    ASSERT_EQ(detections->size(), 1u) << epoch;
    EXPECT_NEAR(detections->front().begin, start, 16u) << epoch;
  }
}

TEST(FHSSPhase2DetectorTest,
     RejectsNoiseOnlyContinuousWaveAndNonFiniteEvidence) {
  FHSSAcquisitionPulseDetectorKernel kernel;
  std::mt19937_64 rng(91002);
  std::normal_distribution<double> noise(0.0, 0.02);
  std::vector<std::complex<double>> noise_only(2'000);
  for (auto &sample : noise_only) {
    sample = {noise(rng), noise(rng)};
  }
  auto noise_result = kernel.Detect(noise_only, 10);
  ASSERT_TRUE(noise_result.has_value());
  EXPECT_TRUE(noise_result->empty());

  const auto cw = Tone(2'000, 1'000'000.0, 50'000'000.0);
  auto cw_result = kernel.Detect(cw, 10);
  ASSERT_TRUE(cw_result.has_value());
  EXPECT_TRUE(cw_result->empty());

  auto invalid = noise_only;
  invalid[50] = {std::numeric_limits<double>::infinity(), 0.0};
  EXPECT_FALSE(kernel.Detect(invalid, 10));
}

TEST(FHSSPhase2DetectorTest,
     FractionalBoundaryAndNonPulseInterfererVectorsUseIndependentOracles) {
  FHSSAcquisitionPulseDetectorKernel kernel;
  const auto base = CaptureWithPulses({211}, 900, 10, 1.0, 0.3);
  std::vector<std::complex<double>> half_sample_shift(base.size(), {0.0, 0.0});
  for (std::size_t index = 1; index < base.size(); ++index) {
    half_sample_shift[index] = 0.5 * (base[index - 1u] + base[index]);
  }
  auto shifted = kernel.Detect(half_sample_shift, 10);
  ASSERT_TRUE(shifted.has_value());
  ASSERT_EQ(shifted->size(), 1u);
  EXPECT_NEAR(shifted->front().begin, 212u, 4u);

  std::vector<std::complex<double>> impulsive(900, {0.0, 0.0});
  impulsive[300] = {100.0, 0.0};
  auto impulse_result = kernel.Detect(impulsive, 10);
  ASSERT_TRUE(impulse_result.has_value());
  EXPECT_TRUE(impulse_result->empty());

  std::vector<std::complex<double>> chirp(900, {0.0, 0.0});
  for (std::size_t index = 250; index < 570; ++index) {
    const double local = static_cast<double>(index - 250u);
    const double phase = 0.08 * local * local;
    chirp[index] = {std::cos(phase), std::sin(phase)};
  }
  auto chirp_result = kernel.Detect(chirp, 10);
  ASSERT_TRUE(chirp_result.has_value());
  EXPECT_TRUE(chirp_result->empty());

  std::vector<std::complex<double>> unrelated(900, {0.0, 0.0});
  for (std::size_t index = 250; index < 570; ++index) {
    const double phase = static_cast<double>((index * 17u) % 31u);
    unrelated[index] = {std::cos(phase), std::sin(phase)};
  }
  auto unrelated_result = kernel.Detect(unrelated, 10);
  ASSERT_TRUE(unrelated_result.has_value());
  EXPECT_TRUE(unrelated_result->empty());

  std::vector<std::complex<double>> wrong_symbol_rate(900, {0.0, 0.0});
  double wrong_phase = 0.0;
  for (std::size_t index = 250; index < 570; ++index) {
    const double symbol = index % 2u == 0u ? 1.0 : -1.0;
    wrong_phase += symbol * std::numbers::pi / 2.0;
    wrong_symbol_rate[index] = std::polar(1.0, wrong_phase);
  }
  auto wrong_rate_result = kernel.Detect(wrong_symbol_rate, 10);
  ASSERT_TRUE(wrong_rate_result.has_value());
  EXPECT_TRUE(wrong_rate_result->empty());
}

TEST(FHSSPhase2DetectorTest,
     RejectsPartialBoundaryPulsesAndSuppressesOverlappingDuplicates) {
  FHSSAcquisitionPulseDetectorKernel kernel;
  const auto leading_full = CaptureWithPulses({0}, 700);
  const std::vector<std::complex<double>> leading_partial(
      leading_full.begin() + 101, leading_full.end());
  auto leading = kernel.Detect(leading_partial, 10);
  ASSERT_TRUE(leading.has_value());
  EXPECT_TRUE(leading->empty());

  const auto trailing_partial = CaptureWithPulses({750}, 900);
  auto trailing = kernel.Detect(trailing_partial, 10);
  ASSERT_TRUE(trailing.has_value());
  EXPECT_TRUE(trailing->empty());

  const auto overlap = CaptureWithPulses({200, 220}, 900);
  auto duplicate = kernel.Detect(overlap, 10);
  ASSERT_TRUE(duplicate.has_value());
  EXPECT_LE(duplicate->size(), 1u);

  auto floor_config = FHSSAcquisitionPulseDetectorConfig{};
  floor_config.min_absolute_power_linear = 0.05;
  FHSSAcquisitionPulseDetectorKernel floor_kernel(floor_config);
  const auto adjacent_leakage = CaptureWithPulses({200}, 900, 10, 0.1);
  auto leakage = floor_kernel.Detect(adjacent_leakage, 10);
  ASSERT_TRUE(leakage.has_value());
  EXPECT_TRUE(leakage->empty());

  auto threshold_config = FHSSAcquisitionPulseDetectorConfig{};
  threshold_config.min_absolute_power_linear = 1.0;
  FHSSAcquisitionPulseDetectorKernel threshold_kernel(threshold_config);
  auto below = threshold_kernel.Detect(
      CaptureWithPulses({200}, 900, 10, std::sqrt(0.99)), 10);
  auto above = threshold_kernel.Detect(
      CaptureWithPulses({200}, 900, 10, std::sqrt(1.01)), 10);
  ASSERT_TRUE(below.has_value());
  ASSERT_TRUE(above.has_value());
  EXPECT_TRUE(below->empty());
  EXPECT_EQ(above->size(), 1u);
}

TEST(FHSSPhase2DetectorTest,
     NodeRetainsSplitPacketStateEmitsAtEosAndPropagatesTiming) {
  auto capture = CaptureWithPulses({193}, 1'000);
  auto first_samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          capture.begin(), capture.begin() + 500);
  auto second_samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          capture.begin() + 500, capture.end());
  FHSSAcquisitionPulseDetectorNode detector;
  auto first = ChannelToken(first_samples, 10'000, false);
  auto first_output =
      detector.Transfer(first, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(first_output.has_value());
  EXPECT_TRUE(first_output->sidecar.detected_pulses.empty());

  auto second = ChannelToken(second_samples, 15'000, true);
  auto output =
      detector.Transfer(second, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(output.has_value());
  ASSERT_EQ(output->sidecar.detected_pulses.size(), 1u);
  EXPECT_EQ(output->sidecar.detected_pulses.front().timing.global_start_sample,
            10'000u + 193u * 10u);
  EXPECT_TRUE(
      std::holds_alternative<graph::EdgeEndOfStream>(output->edge_control));
  EXPECT_GT(output->sidecar.detected_pulses.front().snr_db, 0.0);
  EXPECT_LT(output->sidecar.detected_pulses.front().noise_floor_db, -100.0);
}

TEST(FHSSPhase2IntegrationContractTest,
     NonAlignedChannelizerPacketsRetainDetectorTimingContinuity) {
  constexpr std::uint64_t kPulseStart = 1'500;
  constexpr std::size_t kSplit = 1'003;
  auto capture = CaptureWithPulses({kPulseStart}, 7'000, 1);
  const auto center = ProductionConfig().frequency.iq_offset_frequency_hz[24];
  for (std::size_t sample = 0; sample < capture.size(); ++sample) {
    const double phase = 2.0 * std::numbers::pi * center *
                         static_cast<double>(sample) /
                         FHSSProtocolConstants::kSampleRateHz;
    capture[sample] *= std::polar(1.0, phase);
  }
  auto first_samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          capture.begin(), capture.begin() + kSplit);
  auto second_samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          capture.begin() + kSplit, capture.end());

  FHSSProductionCandidateChannelizerNode channelizer(ProductionConfig());
  FHSSAcquisitionPulseDetectorNode detector;
  ASSERT_TRUE(
      channelizer.ConsumeInput<0>(DownconvertedToken(first_samples, 0, false)));
  auto first = DrainChannelizer<24>(channelizer);
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(FHSSAcquisitionDetectorMetadataIsValid(first->sidecar));
  EXPECT_EQ(
      first->sidecar.channel.sample_time_map.input_packet_global_start_sample,
      120u);
  EXPECT_EQ(first->sidecar.channel.sample_time_map.output_start_sample, 12u);
  EXPECT_EQ(first->sidecar.channel.sample_time_map.decimation_factor, 10u);
  EXPECT_EQ(first->sidecar.channel.sample_time_map.group_delay_input_samples,
            120);
  EXPECT_EQ(first->sidecar.channel.input_global_start_sample, 120u);
  EXPECT_EQ(first->sidecar.channel.channel_global_start_sample, 240u);
  EXPECT_EQ(first->sidecar.channel.channel_global_start_sample,
            first->sidecar.channel.input_global_start_sample +
                first->sidecar.channel.filter_group_delay_input_samples);
  const auto first_center = NormalizeToGlobalStartSample(
      FHSSSampleTimeMapFromGraphX(first->sidecar.channel.sample_time_map), 0u);
  ASSERT_TRUE(first_center.has_value());
  EXPECT_EQ(*first_center, first->sidecar.channel.input_global_start_sample);
  auto first_detection =
      detector.Transfer(*first, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(first_detection.has_value());
  EXPECT_TRUE(first_detection->sidecar.detected_pulses.empty());

  ASSERT_TRUE(channelizer.ConsumeInput<0>(
      DownconvertedToken(second_samples, kSplit, true)));
  auto second = DrainChannelizer<24>(channelizer);
  ASSERT_TRUE(second.has_value());
  auto detection =
      detector.Transfer(*second, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(detection.has_value());
  ASSERT_EQ(detection->sidecar.detected_pulses.size(), 1u);
  EXPECT_NEAR(
      detection->sidecar.detected_pulses.front().timing.global_start_sample,
      kPulseStart, 160u);
  EXPECT_TRUE(
      std::holds_alternative<graph::EdgeEndOfStream>(detection->edge_control));
}

TEST(FHSSPhase2IntegrationContractTest,
     NonDivisibleFirDelayUsesSignedAnchorAcrossReceiverAndMergeContracts) {
  constexpr std::size_t kSplit = 1'003;
  const auto all_samples =
      std::make_shared<const std::vector<std::complex<double>>>(
          Tone(1'800, 0.0, FHSSProtocolConstants::kSampleRateHz));
  for (const std::uint32_t taps : {7u, 11u, 15u, 23u}) {
    auto config = ProductionConfig();
    config.decimation_factor = 4u;
    config.fir_tap_count = taps;
    const auto first_samples =
        std::make_shared<const std::vector<std::complex<double>>>(
            all_samples->begin(), all_samples->begin() + kSplit);
    const auto second_samples =
        std::make_shared<const std::vector<std::complex<double>>>(
            all_samples->begin() + kSplit, all_samples->end());
    FHSSProductionCandidateChannelizerNode channelizer(config);
    FHSSAcquisitionPulseDetectorNode detector;
    ASSERT_TRUE(channelizer.ConsumeInput<0>(
        DownconvertedToken(first_samples, 0u, false)));
    const auto first = DrainChannelizer<24>(channelizer);
    ASSERT_TRUE(first.has_value()) << taps;
    ASSERT_TRUE(FHSSAcquisitionDetectorMetadataIsValid(first->sidecar)) << taps;
    const auto first_center = NormalizeToGlobalStartSample(
        FHSSSampleTimeMapFromGraphX(first->sidecar.channel.sample_time_map),
        0u);
    ASSERT_TRUE(first_center.has_value()) << taps;
    EXPECT_EQ(*first_center, first->sidecar.channel.input_global_start_sample)
        << taps;
    ASSERT_TRUE(detector.Transfer(*first,
                                  std::integral_constant<std::size_t, 0>{},
                                  std::integral_constant<std::size_t, 0>{}))
        << taps;

    ASSERT_TRUE(channelizer.ConsumeInput<0>(
        DownconvertedToken(second_samples, kSplit, true)));
    const auto second = DrainChannelizer<24>(channelizer);
    ASSERT_TRUE(second.has_value()) << taps;
    ASSERT_TRUE(FHSSAcquisitionDetectorMetadataIsValid(second->sidecar))
        << taps;
    EXPECT_EQ(second->sidecar.channel.input_global_start_sample,
              first->sidecar.channel.input_global_start_sample +
                  first->sidecar.iq.sample_count * config.decimation_factor)
        << taps;
    const auto detected =
        detector.Transfer(*second, std::integral_constant<std::size_t, 0>{},
                          std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(detected.has_value()) << taps;

    FHSSLocalPulseDetection local{};
    local.sample_time_map =
        FHSSSampleTimeMapFromGraphX(first->sidecar.channel.sample_time_map);
    local.local_start_offset = 5u;
    local.duration_samples = 100u;
    local.channel_id = 24u;
    local.frequency_index = 24u;
    local.complex_evidence.samples = first->sidecar.iq.host_complex64_samples;
    local.complex_evidence.sample_count = 1u;
    const auto merged = NormalizeLocalDetection(local);
    ASSERT_TRUE(merged.has_value()) << taps;
    EXPECT_EQ(merged->candidate.detected_pulse.global_start_sample,
              *first_center + 5u * config.decimation_factor)
        << taps;
    const auto candidate = FHSSGraphXPulseCandidateFromMergeCandidate(*merged);
    EXPECT_EQ(candidate.pulse.timing.global_start_sample,
              merged->candidate.detected_pulse.global_start_sample)
        << taps;
    EXPECT_EQ(candidate.pulse.timing.sample_time_map.group_delay_input_samples,
              static_cast<std::int64_t>((taps - 1u) / 2u));
  }
}

TEST(FHSSPhase2DetectorTest, RejectsMalformedAndNonFiniteConfiguration) {
  auto config = FHSSAcquisitionPulseDetectorConfig{};
  config.noise_power_quantile = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(ValidateFHSSAcquisitionPulseDetectorConfig(config));
  config = {};
  config.threshold_above_noise_linear = 1.0;
  EXPECT_FALSE(ValidateFHSSAcquisitionPulseDetectorConfig(config));
  config = {};
  config.max_pulse_input_samples = config.min_pulse_input_samples - 1u;
  EXPECT_FALSE(ValidateFHSSAcquisitionPulseDetectorConfig(config));
}

TEST(FHSSPhase2DetectorTest,
     RejectsInconsistentMetadataAndOverflowThenResetsForNextCapture) {
  const auto capture = CaptureWithPulses({200}, 900);
  const auto samples =
      std::make_shared<const std::vector<std::complex<double>>>(capture);
  FHSSAcquisitionPulseDetectorNode detector;

  auto malformed = ChannelToken(samples, 1'000, true);
  malformed.sidecar.channel.rf_frequency_hz += 1.0;
  EXPECT_FALSE(detector.Transfer(malformed,
                                 std::integral_constant<std::size_t, 0>{},
                                 std::integral_constant<std::size_t, 0>{}));

  auto non_finite = ChannelToken(samples, 1'000, true);
  non_finite.sidecar.channel.iq_offset_frequency_hz =
      std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(detector.Transfer(non_finite,
                                 std::integral_constant<std::size_t, 0>{},
                                 std::integral_constant<std::size_t, 0>{}));

  auto overflow = ChannelToken(
      samples, std::numeric_limits<std::uint64_t>::max() - 5u, true);
  EXPECT_FALSE(detector.Transfer(overflow,
                                 std::integral_constant<std::size_t, 0>{},
                                 std::integral_constant<std::size_t, 0>{}));

  auto valid = ChannelToken(samples, 1'000, true);
  const auto output =
      detector.Transfer(valid, std::integral_constant<std::size_t, 0>{},
                        std::integral_constant<std::size_t, 0>{});
  ASSERT_TRUE(output.has_value());
  ASSERT_EQ(output->sidecar.detected_pulses.size(), 1u);
  EXPECT_EQ(detector.LastDetectedPulseCount(), 1u);
  EXPECT_GT(detector.AllocationHighWaterBytes(), 0u);
}

TEST(FHSSPhase2DetectorTest,
     CancellationAndFailureDiscardBufferedStateBeforeRestart) {
  const auto capture = CaptureWithPulses({200}, 900);
  const auto midpoint = capture.size() / 2u;
  const auto first = std::make_shared<const std::vector<std::complex<double>>>(
      capture.begin(), capture.begin() + midpoint);
  const auto second = std::make_shared<const std::vector<std::complex<double>>>(
      capture.begin() + midpoint, capture.end());
  const auto full =
      std::make_shared<const std::vector<std::complex<double>>>(capture);

  for (const graph::EdgeControl &terminal :
       {graph::EdgeControl{graph::EdgeCancellation{"test cancellation"}},
        graph::EdgeControl{graph::EdgeFailure{"test failure"}}}) {
    FHSSAcquisitionPulseDetectorNode detector;
    auto first_token = ChannelToken(first, 0u, false);
    ASSERT_TRUE(detector.Transfer(first_token,
                                  std::integral_constant<std::size_t, 0>{},
                                  std::integral_constant<std::size_t, 0>{}));
    auto terminal_token = ChannelToken(second, midpoint * 10u, false);
    terminal_token.edge_control = terminal;
    const auto terminal_output = detector.Transfer(
        terminal_token, std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(terminal_output.has_value());
    EXPECT_TRUE(terminal_output->sidecar.detected_pulses.empty());
    EXPECT_EQ(detector.LastDetectedPulseCount(), 0u);

    const auto restarted = detector.Transfer(
        ChannelToken(full, 0u, true), std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(restarted.has_value());
    EXPECT_EQ(restarted->sidecar.detected_pulses.size(), 1u);
  }
}

TEST(FHSSPhase2ContractTest,
     ParameterNamesExactlyMatchValuesAndDescriptionsAreUseful) {
  const auto check = [](const graph::IParameterized &node) {
    const auto parameters = node.GetParameters().Raw();
    const auto names = node.GetParameterNames();
    std::set<std::string> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size());
    EXPECT_EQ(unique.size(), parameters.size());
    for (const auto &name : unique) {
      EXPECT_TRUE(parameters.contains(name)) << name;
      const auto description = node.GetParameterDescription(name).Raw();
      ASSERT_TRUE(description.contains("description")) << name;
      EXPECT_FALSE(description.at("description").get<std::string>().empty())
          << name;
    }
  };
  check(FHSSProductionCandidateChannelizerNode(ProductionConfig()));
  check(FHSSAcquisitionPulseDetectorNode{});
}

TEST(FHSSPhase2ContractTest,
     CheckedTimeMappingAndDecimationAdaptersRejectOverflowAndZero) {
  FHSSSampleTimeMap map{};
  map.input_packet_global_start_sample = 1u;
  map.output_start_sample = std::numeric_limits<std::uint64_t>::max();
  EXPECT_FALSE(NormalizeToGlobalStartSample(map, 1u));
  map.output_start_sample = 0u;
  map.decimation_factor = 2u;
  EXPECT_FALSE(NormalizeToGlobalStartSample(
      map, std::numeric_limits<std::uint64_t>::max()));
  map.decimation_factor = 1u;
  map.input_packet_global_start_sample =
      std::numeric_limits<std::uint64_t>::max();
  EXPECT_FALSE(NormalizeToGlobalStartSample(map, 1u));

  const std::vector<std::complex<double>> evidence(320, {1.0, 0.0});
  EXPECT_TRUE(FHSSExpandDecimatedCpsmEvidence(evidence, 0u).empty());
  EXPECT_EQ(FHSSExpandDecimatedCpsmEvidence(evidence, 1u), evidence);
}

TEST(FHSSPhase2ContractTest, BranchAndViterbiRejectZeroDecimationEvidence) {
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(320, {1.0, 0.0}));
  FHSSGraphXPulseCandidate candidate{};
  candidate.complex_evidence =
      FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), {});
  candidate.complex_evidence.sample_time_map.decimation_factor = 0u;
  candidate.pulse.timing.sample_time_map.decimation_factor = 0u;

  FHSSPulseCandidateToken candidates{};
  candidates.sidecar.ordered_candidates.push_back(candidate);
  CPSMBranchMetricNode branch;
  EXPECT_FALSE(branch.Transfer(candidates,
                               std::integral_constant<std::size_t, 0>{},
                               std::integral_constant<std::size_t, 0>{}));
  EXPECT_EQ(branch.LastRejectionReason(),
            "candidate decimation cannot be expanded to canonical evidence");

  FHSSCpsmBranchMetricToken metrics{};
  metrics.sidecar.candidate = candidate;
  CPSMViterbiDecoderNode viterbi;
  EXPECT_FALSE(viterbi.Transfer(metrics,
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{}));
}

TEST(FHSSPhase2IntegrationContractTest,
     MergePreservesDecimationMapAndCpsmAdapterHasCheckedShape) {
  FHSSPulseCandidateWithEvidence merged{};
  merged.candidate.detected_pulse.global_start_sample = 1'000;
  merged.candidate.detected_pulse.global_end_sample = 4'200;
  merged.candidate.detected_pulse.duration_samples = 3'200;
  merged.sample_time_map.input_packet_global_start_sample = 0;
  merged.sample_time_map.output_start_sample = 12;
  merged.sample_time_map.decimation_factor = 10;
  merged.sample_time_map.group_delay_input_samples = 120;
  merged.sample_time_map.sample_rate_hz = 500'000'000.0;
  auto samples = std::make_shared<const std::vector<std::complex<double>>>(
      std::vector<std::complex<double>>(320, {1.0, 0.0}));
  merged.complex_evidence = {
      .samples = samples, .sample_offset = 0, .sample_count = 320};

  const auto packet = FHSSGraphXPulseCandidateFromMergeCandidate(merged);
  EXPECT_EQ(packet.pulse.timing.sample_time_map.decimation_factor, 10u);
  EXPECT_EQ(packet.complex_evidence.sample_time_map.decimation_factor, 10u);
  EXPECT_DOUBLE_EQ(
      packet.complex_evidence.sample_time_map.output_sample_rate_hz,
      50'000'000.0);
  const auto expanded = FHSSExpandDecimatedCpsmEvidence(
      FHSSGraphXComplexEvidenceSamples(packet.complex_evidence), 10);
  EXPECT_EQ(expanded.size(), 3'200u);
  EXPECT_TRUE(FHSSExpandDecimatedCpsmEvidence(*samples, 3).empty());
}

TEST(FHSSPhase2IntegrationContractTest,
     CausalFilteredEvidencePreservesFirstBitPhaseSlope) {
  constexpr std::uint32_t kWord = 0x85B1'3A59u;
  constexpr std::uint64_t kPulseStart = 1'000u;
  const auto config = ProductionConfig();
  std::vector<std::complex<double>> input(5'000, {0.0, 0.0});
  double completed = 0.0;
  for (std::uint32_t local = 0;
       local < FHSSProtocolConstants::kPulseWidthSamples; ++local) {
    const auto symbol_index = local / FHSSProtocolConstants::kSamplesPerSymbol;
    const auto in_symbol = local % FHSSProtocolConstants::kSamplesPerSymbol;
    if (in_symbol == 0u && symbol_index != 0u) {
      const auto prior = (kWord >> (32u - symbol_index)) & 1u;
      completed += prior == 0u ? 0.5 : -0.5;
    }
    const auto bit = (kWord >> (31u - symbol_index)) & 1u;
    const double symbol = bit == 0u ? 1.0 : -1.0;
    const double phase =
        std::numbers::pi *
        (completed + symbol * 0.5 * static_cast<double>(in_symbol) /
                         FHSSProtocolConstants::kSamplesPerSymbol);
    input[kPulseStart + local] = std::polar(1.0, phase);
  }
  const auto taps =
      DesignFHSSHammingLowpass(config.fir_tap_count, config.cutoff_frequency_hz,
                               config.frequency.sample_rate_hz);
  FHSSFirChannelizerKernel kernel(taps, config.decimation_factor, 0.0,
                                  config.frequency.sample_rate_hz);
  const auto filtered = kernel.Process(input, 0u);
  ASSERT_TRUE(filtered.has_value());
  ASSERT_TRUE(filtered->has_output);
  const auto first_center = filtered->first_causal_input_global_sample;
  ASSERT_LE(first_center, kPulseStart + 120u);
  const auto evidence_begin =
      (kPulseStart + 120u - first_center) / config.decimation_factor;
  ASSERT_LE(evidence_begin + 320u, filtered->samples.size());
  const std::vector<std::complex<double>> decimated(
      filtered->samples.begin() + static_cast<std::ptrdiff_t>(evidence_begin),
      filtered->samples.begin() +
          static_cast<std::ptrdiff_t>(evidence_begin + 320u));
  const auto expanded =
      FHSSExpandDecimatedCpsmEvidence(decimated, config.decimation_factor);
  const auto decoded = CPSMViterbiDecoderKernel::Decode(expanded);
  ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
  std::uint32_t actual = 0u;
  for (const double symbol : decoded->symbols) {
    actual = (actual << 1u) | (symbol < 0.0 ? 1u : 0u);
  }
  EXPECT_EQ(actual, kWord);
}

} // namespace
} // namespace dsp::fhss
