/**
 * @file FHSSCpsmDecoder.hpp
 * @brief PR5 binary CPSM branch metric and Viterbi/MLSE fixture decoder.
 *
 * @details CPU-only one-pulse decoder for the deterministic FHSS CPSM
 * fixture. This file does not map symbols to uint32_t words, detect
 * preambles, assemble messages, add graph runtime lanes, channelize, use GPU
 * execution, or model Doppler/noise behavior.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <expected>
#include <limits>
#include <numbers>
#include <vector>

namespace dsp::fhss {

struct CPSMDecoderConfig {
  FHSSTimingConfig timing{};
  double modulation_index = 0.5;
  std::uint32_t symbol_count = FHSSProtocolConstants::kBitsPerPulse;
  std::uint32_t initial_phase_state = 0;
  bool check_terminal_phase = false;
  std::uint32_t expected_terminal_phase_state = 0;
};

struct CPSMBranchMetric {
  std::uint32_t symbol_index = 0;
  std::uint32_t from_state = 0;
  std::uint32_t to_state = 0;
  double symbol = 1.0;
  double correlation = 0.0;
  double cost = 0.0;
};

struct CPSMTrellisTransition {
  std::uint32_t from_state = 0;
  std::uint32_t to_state = 0;
  double symbol = 1.0;
};

struct CPSMViterbiResult {
  std::vector<double> symbols;
  std::vector<std::uint32_t> phase_states;
  double best_path_metric = 0.0;
  double second_best_path_metric = 0.0;
  double confidence = 0.0;
  bool terminal_phase_checked = false;
  std::uint32_t terminal_phase_state = 0;
};

[[nodiscard]] inline std::uint32_t CPSMPhaseStateCount(double h = 0.5) {
  if (!NearlyEqual(h, 0.5)) {
    return 0;
  }
  return 4;
}

[[nodiscard]] inline double CPSMStatePhaseRad(std::uint32_t state) {
  return 0.5 * std::numbers::pi * static_cast<double>(state % 4u);
}

[[nodiscard]] inline std::uint32_t CPSMSymbolToPhaseStep(double symbol) {
  return symbol >= 0.0 ? 1u : 3u;
}

[[nodiscard]] inline std::uint32_t CPSMTransitionState(std::uint32_t from_state,
                                                       double symbol) {
  return (from_state + CPSMSymbolToPhaseStep(symbol)) % 4u;
}

[[nodiscard]] inline std::array<CPSMTrellisTransition, 8>
BuildCPSMTrellisTransitions() {
  std::array<CPSMTrellisTransition, 8> transitions{};
  std::size_t out = 0;
  for (std::uint32_t state = 0; state < 4u; ++state) {
    transitions[out++] = CPSMTrellisTransition{
        .from_state = state,
        .to_state = CPSMTransitionState(state, 1.0),
        .symbol = 1.0};
    transitions[out++] = CPSMTrellisTransition{
        .from_state = state,
        .to_state = CPSMTransitionState(state, -1.0),
        .symbol = -1.0};
  }
  return transitions;
}

[[nodiscard]] inline double CPSMThetaRad(std::uint32_t phase_state,
                                         double symbol,
                                         std::uint32_t sample_in_symbol,
                                         std::uint32_t samples_per_symbol,
                                         double h = 0.5) {
  const double q = RectangularFullResponsePhasePulse(sample_in_symbol,
                                                    samples_per_symbol);
  return CPSMStatePhaseRad(phase_state) +
         2.0 * std::numbers::pi * h * symbol * q;
}

[[nodiscard]] inline std::complex<double>
CPSMPredictedSample(std::uint32_t phase_state, double symbol,
                    std::uint32_t sample_in_symbol,
                    std::uint32_t samples_per_symbol, double h = 0.5) {
  return std::exp(std::complex<double>(
      0.0, CPSMThetaRad(phase_state, symbol, sample_in_symbol,
                        samples_per_symbol, h)));
}

[[nodiscard]] inline FHSSVoidResult
ValidateCPSMDecoderConfig(const CPSMDecoderConfig &config) {
  if (!NearlyEqual(config.modulation_index, 0.5)) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR5 CPSM fixture requires modulation index h = 1/2"));
  }
  if (config.symbol_count == 0 ||
      config.symbol_count > FHSSProtocolConstants::kBitsPerPulse) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR5 one-pulse decoder supports 1..32 CPSM symbols"));
  }
  if (config.initial_phase_state >= CPSMPhaseStateCount() ||
      config.expected_terminal_phase_state >= CPSMPhaseStateCount()) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR5 CPSM phase states are accumulated phase modulo 2*pi"));
  }
  if (auto timing = DeriveTimingModel(config.timing); !timing) {
    return std::unexpected(timing.error());
  }
  return {};
}

[[nodiscard]] inline FHSSVoidResult
ValidateCPSMEvidence(const std::vector<std::complex<double>> &samples,
                     const CPSMDecoderConfig &config) {
  if (auto validation = ValidateCPSMDecoderConfig(config); !validation) {
    return validation;
  }
  const auto timing = DeriveTimingModel(config.timing).value();
  const auto expected_samples =
      static_cast<std::size_t>(config.symbol_count) * timing.samples_per_symbol;
  if (samples.size() != expected_samples) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidGlobalTiming,
        "FHSS PR5 CPSM evidence must contain exactly symbol_count * 100 "
        "complex samples"));
  }
  return {};
}

class CPSMBranchMetricNode {
public:
  [[nodiscard]] static CPSMBranchMetric ScoreBranch(
      const std::vector<std::complex<double>> &samples,
      std::uint32_t symbol_index, std::uint32_t from_state, double symbol,
      const CPSMDecoderConfig &config = {}) {
    const auto timing = DeriveTimingModel(config.timing).value();
    const std::size_t start =
        static_cast<std::size_t>(symbol_index) * timing.samples_per_symbol;

    double correlation_sum = 0.0;
    double usable_samples = 0.0;
    for (std::uint32_t i = 0; i < timing.samples_per_symbol; ++i) {
      const auto &sample = samples[start + i];
      const double magnitude = std::abs(sample);
      if (magnitude <= std::numeric_limits<double>::epsilon()) {
        continue;
      }
      const auto observed_unit = sample / magnitude;
      const auto predicted = CPSMPredictedSample(
          from_state, symbol, i, timing.samples_per_symbol,
          config.modulation_index);
      correlation_sum += (observed_unit * std::conj(predicted)).real();
      usable_samples += 1.0;
    }

    const double correlation =
        usable_samples == 0.0 ? -1.0 : correlation_sum / usable_samples;
    const double cost = 1.0 - std::clamp(correlation, -1.0, 1.0);
    return CPSMBranchMetric{
        .symbol_index = symbol_index,
        .from_state = from_state,
        .to_state = CPSMTransitionState(from_state, symbol),
        .symbol = symbol,
        .correlation = correlation,
        .cost = cost};
  }

  [[nodiscard]] static FHSSResult<std::vector<CPSMBranchMetric>>
  Compute(const std::vector<std::complex<double>> &samples,
          const CPSMDecoderConfig &config = {}) {
    if (auto validation = ValidateCPSMEvidence(samples, config); !validation) {
      return std::unexpected(validation.error());
    }

    std::vector<CPSMBranchMetric> metrics;
    metrics.reserve(static_cast<std::size_t>(config.symbol_count) * 8u);
    for (std::uint32_t symbol_index = 0; symbol_index < config.symbol_count;
         ++symbol_index) {
      for (std::uint32_t state = 0; state < CPSMPhaseStateCount(); ++state) {
        metrics.push_back(ScoreBranch(samples, symbol_index, state, 1.0,
                                      config));
        metrics.push_back(ScoreBranch(samples, symbol_index, state, -1.0,
                                      config));
      }
    }
    return metrics;
  }
};

class CPSMViterbiDecoderNode {
public:
  [[nodiscard]] static FHSSResult<CPSMViterbiResult>
  Decode(const std::vector<std::complex<double>> &samples,
         const CPSMDecoderConfig &config = {}) {
    if (auto validation = ValidateCPSMEvidence(samples, config); !validation) {
      return std::unexpected(validation.error());
    }

    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    constexpr std::uint32_t kStateCount = 4;

    std::array<double, kStateCount> previous{};
    previous.fill(kInfinity);
    previous[config.initial_phase_state] = 0.0;

    std::vector<std::array<double, kStateCount>> path_metrics(
        config.symbol_count);
    std::vector<std::array<std::uint32_t, kStateCount>> predecessors(
        config.symbol_count);
    std::vector<std::array<double, kStateCount>> predecessor_symbols(
        config.symbol_count);

    for (std::uint32_t symbol_index = 0; symbol_index < config.symbol_count;
         ++symbol_index) {
      std::array<double, kStateCount> current{};
      current.fill(kInfinity);
      predecessors[symbol_index].fill(0);
      predecessor_symbols[symbol_index].fill(1.0);

      for (std::uint32_t from_state = 0; from_state < kStateCount;
           ++from_state) {
        if (!std::isfinite(previous[from_state])) {
          continue;
        }
        for (const double symbol : {1.0, -1.0}) {
          const auto metric = CPSMBranchMetricNode::ScoreBranch(
              samples, symbol_index, from_state, symbol, config);
          const double candidate_metric =
              previous[from_state] + metric.cost;
          if (candidate_metric < current[metric.to_state]) {
            current[metric.to_state] = candidate_metric;
            predecessors[symbol_index][metric.to_state] = from_state;
            predecessor_symbols[symbol_index][metric.to_state] = symbol;
          }
        }
      }

      path_metrics[symbol_index] = current;
      previous = current;
    }

    std::uint32_t best_state = 0;
    double best_metric = kInfinity;
    double second_best_metric = kInfinity;
    for (std::uint32_t state = 0; state < kStateCount; ++state) {
      const double metric = previous[state];
      if (config.check_terminal_phase &&
          state != config.expected_terminal_phase_state) {
        continue;
      }
      if (metric < best_metric) {
        second_best_metric = best_metric;
        best_metric = metric;
        best_state = state;
      } else if (metric < second_best_metric) {
        second_best_metric = metric;
      }
    }

    if (!std::isfinite(best_metric)) {
      return std::unexpected(MakeError(
          FHSSValidationCode::InvalidTiming,
          "FHSS PR5 terminal phase policy rejected every Viterbi path"));
    }

    std::vector<double> symbols(config.symbol_count, 1.0);
    std::vector<std::uint32_t> states(config.symbol_count + 1u, 0u);
    states[config.symbol_count] = best_state;
    auto state = best_state;
    for (std::uint32_t i = config.symbol_count; i > 0; --i) {
      const auto symbol_index = i - 1u;
      symbols[symbol_index] = predecessor_symbols[symbol_index][state];
      const auto previous_state = predecessors[symbol_index][state];
      states[symbol_index] = previous_state;
      state = previous_state;
    }

    const double separation = std::isfinite(second_best_metric)
                                  ? second_best_metric - best_metric
                                  : best_metric == 0.0 ? 1.0 : best_metric;
    const double confidence = std::clamp(
        separation / static_cast<double>(config.symbol_count), 0.0, 1.0);

    return CPSMViterbiResult{.symbols = std::move(symbols),
                             .phase_states = std::move(states),
                             .best_path_metric = best_metric,
                             .second_best_path_metric = second_best_metric,
                             .confidence = confidence,
                             .terminal_phase_checked =
                                 config.check_terminal_phase,
                             .terminal_phase_state = best_state};
  }
};

} // namespace dsp::fhss
