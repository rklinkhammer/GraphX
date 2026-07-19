/**
 * @file FHSSPulseWordDecoder.hpp
 * @brief PR6 FHSS pulse word decoder.
 *
 * @details CPU-only conversion from PR5 CPSM symbol decisions into one
 * uint32_t per pulse. This file does not decode from truth metadata, detect
 * preambles, assemble messages, add graph runtime lanes, channelize, use GPU
 * execution, or model Doppler/noise behavior.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"

#include <cmath>
#include <cstdint>
#include <string>

namespace dsp::fhss {

enum class FHSSPulseWordDecodeStatus {
  Ok,
  InvalidSymbolCount,
  InvalidSymbolDecision,
  LowConfidence,
  InvalidPathMetric
};

struct FHSSPulseWordDecoderConfig {
  double minimum_confidence = 0.0;
};

struct FHSSDecodedPulseWord {
  FHSSPulseCandidate candidate{};
  std::uint32_t decoded_value = 0;
  double confidence = 0.0;
  double viterbi_path_metric = 0.0;
  double viterbi_second_best_path_metric = 0.0;
  FHSSPulseWordDecodeStatus status = FHSSPulseWordDecodeStatus::Ok;
  std::string status_message;
};

[[nodiscard]] inline const char *
FHSSPulseWordDecodeStatusName(FHSSPulseWordDecodeStatus status) {
  switch (status) {
  case FHSSPulseWordDecodeStatus::Ok:
    return "Ok";
  case FHSSPulseWordDecodeStatus::InvalidSymbolCount:
    return "InvalidSymbolCount";
  case FHSSPulseWordDecodeStatus::InvalidSymbolDecision:
    return "InvalidSymbolDecision";
  case FHSSPulseWordDecodeStatus::LowConfidence:
    return "LowConfidence";
  case FHSSPulseWordDecodeStatus::InvalidPathMetric:
    return "InvalidPathMetric";
  }
  return "Unknown";
}

[[nodiscard]] inline FHSSResult<std::uint32_t>
FHSSCpsmSymbolToBit(double symbol) {
  if (NearlyEqual(symbol, 1.0)) {
    return 0u;
  }
  if (NearlyEqual(symbol, -1.0)) {
    return 1u;
  }
  return std::unexpected(MakeError(
      FHSSValidationCode::InvalidTiming,
      "FHSS PR6 pulse-word decode requires CPSM symbols exactly +1 or -1"));
}

[[nodiscard]] inline FHSSResult<std::uint32_t>
AssembleFHSSPulseWordMsbFirst(const std::vector<double> &symbols) {
  if (symbols.size() != FHSSProtocolConstants::kBitsPerPulse) {
    return std::unexpected(MakeError(
        FHSSValidationCode::InvalidTiming,
        "FHSS PR6 pulse-word decode requires exactly 32 CPSM symbols"));
  }

  std::uint32_t value = 0;
  for (const auto symbol : symbols) {
    auto bit = FHSSCpsmSymbolToBit(symbol);
    if (!bit) {
      return std::unexpected(bit.error());
    }
    value = (value << 1u) | *bit;
  }
  return value;
}

class FHSSPulseWordDecoderKernel {
public:
  [[nodiscard]] static FHSSDecodedPulseWord
  Decode(const FHSSPulseCandidate &candidate,
         const CPSMViterbiResult &viterbi_result,
         const FHSSPulseWordDecoderConfig &config = {}) {
    FHSSDecodedPulseWord decoded{};
    decoded.candidate = candidate;
    decoded.confidence = viterbi_result.confidence;
    decoded.viterbi_path_metric = viterbi_result.best_path_metric;
    decoded.viterbi_second_best_path_metric =
        viterbi_result.second_best_path_metric;

    if (viterbi_result.symbols.size() !=
        FHSSProtocolConstants::kBitsPerPulse) {
      decoded.status = FHSSPulseWordDecodeStatus::InvalidSymbolCount;
      decoded.status_message =
          "FHSS pulse word decode requires exactly 32 CPSM symbol decisions";
      return decoded;
    }

    if (!std::isfinite(viterbi_result.best_path_metric) ||
        !std::isfinite(viterbi_result.second_best_path_metric) ||
        !std::isfinite(viterbi_result.confidence)) {
      decoded.status = FHSSPulseWordDecodeStatus::InvalidPathMetric;
      decoded.status_message =
          "FHSS pulse word decode received non-finite Viterbi best-path, "
          "second-best-path, or confidence metric";
      return decoded;
    }

    auto value = AssembleFHSSPulseWordMsbFirst(viterbi_result.symbols);
    if (!value) {
      decoded.status = FHSSPulseWordDecodeStatus::InvalidSymbolDecision;
      decoded.status_message = value.error().message;
      return decoded;
    }

    decoded.decoded_value = *value;
    if (viterbi_result.confidence < config.minimum_confidence) {
      decoded.status = FHSSPulseWordDecodeStatus::LowConfidence;
      decoded.status_message =
          "FHSS pulse word decode confidence is below configured threshold";
      return decoded;
    }

    decoded.status = FHSSPulseWordDecodeStatus::Ok;
    decoded.status_message = FHSSPulseWordDecodeStatusName(decoded.status);
    return decoded;
  }
};

} // namespace dsp::fhss
