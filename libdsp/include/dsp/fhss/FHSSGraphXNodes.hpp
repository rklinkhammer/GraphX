/**
 * @file FHSSGraphXNodes.hpp
 * @brief Real GraphX FHSS nodes with accel-token-ready edge contracts.
 *
 * @details PR7B CPU-only GraphX node wrappers for the deterministic FHSS lane.
 * Every public node edge is a graph::gpu::accel::ControlToken carrying a PR7A
 * FHSS packet sidecar. This file does not add graph JSON executor wiring,
 * plugin registration, channelization, GPU execution, Doppler/noise behavior,
 * overlap-aware separation, or production RF behavior.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSGraphXPackets.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "dsp/fhss/FHSSPulseWordDecoder.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "graph/NamedNodes.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

template <typename PacketT>
using FHSSGraphXToken = graph::gpu::accel::ControlToken<PacketT>;

using FHSSSyntheticIqToken = FHSSGraphXToken<FHSSSyntheticIqOutputPacket>;
using FHSSDetectedPulseToken =
    FHSSGraphXToken<FHSSDetectedPulseEvidencePacket>;
using FHSSPulseCandidateToken =
    FHSSGraphXToken<FHSSPulseCandidateEvidencePacket>;
using FHSSCpsmBranchMetricToken =
    FHSSGraphXToken<FHSSCpsmBranchMetricPacket>;
using FHSSCpsmSymbolDecisionToken =
    FHSSGraphXToken<FHSSCpsmSymbolDecisionPacket>;
using FHSSDecodedPulseWordToken =
    FHSSGraphXToken<FHSSDecodedPulseWordPacket>;
using FHSSDecodedPulseWordsToken =
    FHSSGraphXToken<FHSSDecodedPulseWordsPacket>;
using FHSSAssembledMessageToken =
    FHSSGraphXToken<FHSSAssembledMessagePacket>;
using FHSSDiagnosticsToken = FHSSGraphXToken<FHSSDiagnosticsPacket>;

static_assert(std::is_same_v<FHSSSyntheticIqToken,
                             graph::gpu::accel::ControlToken<
                                 FHSSSyntheticIqOutputPacket>>);
static_assert(std::is_same_v<FHSSDecodedPulseWordsToken,
                             graph::gpu::accel::ControlToken<
                                 FHSSDecodedPulseWordsPacket>>);

[[nodiscard]] inline FHSSGraphXComplexEvidence
FHSSGraphXComplexEvidenceFromHostSamples(
    std::shared_ptr<const std::vector<std::complex<double>>> samples,
    std::uint64_t sample_count,
    const FHSSGraphXSampleTimeMap &sample_time_map = {}) {
  return FHSSGraphXComplexEvidence{
      .host_complex64_samples = std::move(samples),
      .sample_offset = 0,
      .sample_count = sample_count,
      .sample_format = FHSSGraphXSampleFormat::ComplexFloat64,
      .residency = FHSSGraphXPayloadResidency::HostSharedImmutable,
      .sample_time_map = sample_time_map,
      .decoder_usable_complex_iq = true,
      .truth_metadata_required_for_decision = false};
}

[[nodiscard]] inline FHSSSampleTimeMap
FHSSSampleTimeMapFromGraphX(const FHSSGraphXSampleTimeMap &map) {
  return FHSSSampleTimeMap{
      .has_input_global_start_sample = map.has_input_global_start_sample,
      .input_packet_global_start_sample =
          map.input_packet_global_start_sample,
      .output_start_sample = map.output_start_sample,
      .decimation_factor = map.decimation_factor,
      .group_delay_input_samples = map.group_delay_input_samples,
      .sample_rate_hz = map.input_sample_rate_hz};
}

[[nodiscard]] inline FHSSGraphXSampleTimeMap
FHSSGraphXSampleTimeMapFromMergeMap(const FHSSSampleTimeMap &map) {
  return FHSSGraphXSampleTimeMap{
      .has_input_global_start_sample = map.has_input_global_start_sample,
      .input_packet_global_start_sample =
          map.input_packet_global_start_sample,
      .output_start_sample = map.output_start_sample,
      .decimation_factor = map.decimation_factor,
      .group_delay_input_samples = map.group_delay_input_samples,
      .input_sample_rate_hz = map.sample_rate_hz,
      .output_sample_rate_hz = map.sample_rate_hz};
}

[[nodiscard]] inline FHSSComplexEvidence
FHSSComplexEvidenceFromGraphX(const FHSSGraphXComplexEvidence &evidence) {
  return FHSSComplexEvidence{.samples = evidence.host_complex64_samples,
                             .sample_offset = evidence.sample_offset,
                             .sample_count = evidence.sample_count};
}

[[nodiscard]] inline FHSSGraphXComplexEvidence
FHSSGraphXComplexEvidenceFromMergeEvidence(
    const FHSSComplexEvidence &evidence,
    const FHSSGraphXSampleTimeMap &sample_time_map = {}) {
  return FHSSGraphXComplexEvidence{
      .host_complex64_samples = evidence.samples,
      .sample_offset = evidence.sample_offset,
      .sample_count = evidence.sample_count,
      .sample_format = FHSSGraphXSampleFormat::ComplexFloat64,
      .residency = evidence.samples ? FHSSGraphXPayloadResidency::HostSharedImmutable
                                    : FHSSGraphXPayloadResidency::Empty,
      .sample_time_map = sample_time_map,
      .decoder_usable_complex_iq = evidence.samples != nullptr,
      .truth_metadata_required_for_decision = false};
}

[[nodiscard]] inline FHSSDetectedPulse
FHSSDetectedPulseFromGraphX(const FHSSGraphXPulseMetadata &metadata) {
  FHSSDetectedPulse pulse{};
  pulse.global_start_sample = metadata.timing.global_start_sample;
  pulse.global_end_sample = metadata.timing.global_end_sample;
  pulse.duration_samples = metadata.timing.duration_samples;
  pulse.channel_start_sample = metadata.timing.channel_start_sample;
  pulse.channel_id = metadata.timing.channel_id;
  pulse.frequency_index = metadata.frequency.frequency_index;
  pulse.rf_frequency_hz = metadata.frequency.rf_frequency_hz;
  pulse.iq_offset_frequency_hz = metadata.frequency.iq_offset_frequency_hz;
  pulse.estimated_center_frequency_hz =
      metadata.frequency.estimated_center_frequency_hz;
  pulse.frequency_error_hz = metadata.frequency.frequency_error_hz;
  pulse.amplitude = metadata.amplitude;
  pulse.power_db = metadata.power_db;
  pulse.snr_db = metadata.snr_db;
  pulse.noise_floor_db = metadata.noise_floor_db;
  pulse.phase_at_start_rad = metadata.phase_at_start_rad;
  pulse.phase_slope_rad_per_sample = metadata.phase_slope_rad_per_sample;
  pulse.cfo_hz = metadata.cfo_hz;
  pulse.bandwidth_hz = metadata.bandwidth_hz;
  pulse.confidence = metadata.confidence;
  pulse.detector_id = metadata.detector_id;
  pulse.packet_sequence = metadata.packet_sequence;
  return pulse;
}

[[nodiscard]] inline FHSSLocalPulseDetection
FHSSLocalPulseDetectionFromGraphX(const FHSSGraphXPulseMetadata &metadata,
                                  const FHSSGraphXComplexEvidence &evidence) {
  FHSSLocalPulseDetection local{};
  local.sample_time_map =
      FHSSSampleTimeMapFromGraphX(metadata.timing.sample_time_map);
  local.local_start_offset = metadata.timing.channel_start_sample;
  local.duration_samples = metadata.timing.duration_samples;
  local.channel_id = metadata.timing.channel_id;
  local.frequency_index = metadata.frequency.frequency_index;
  local.rf_frequency_hz = metadata.frequency.rf_frequency_hz;
  local.iq_offset_frequency_hz = metadata.frequency.iq_offset_frequency_hz;
  local.estimated_center_frequency_hz =
      metadata.frequency.estimated_center_frequency_hz;
  local.frequency_error_hz = metadata.frequency.frequency_error_hz;
  local.amplitude = metadata.amplitude;
  local.power_db = metadata.power_db;
  local.snr_db = metadata.snr_db;
  local.noise_floor_db = metadata.noise_floor_db;
  local.phase_at_start_rad = metadata.phase_at_start_rad;
  local.phase_slope_rad_per_sample = metadata.phase_slope_rad_per_sample;
  local.cfo_hz = metadata.cfo_hz;
  local.bandwidth_hz = metadata.bandwidth_hz;
  local.confidence = metadata.confidence;
  local.detector_id = metadata.detector_id;
  local.packet_sequence = metadata.packet_sequence;
  local.complex_evidence = FHSSComplexEvidenceFromGraphX(evidence);
  return local;
}

[[nodiscard]] inline FHSSGraphXPulseCandidate
FHSSGraphXPulseCandidateFromMergeCandidate(
    const FHSSPulseCandidateWithEvidence &candidate) {
  const auto sample_time_map =
      FHSSGraphXSampleTimeMapFromMergeMap(FHSSSampleTimeMap{});
  FHSSGraphXPulseCandidate out{};
  out.pulse = FHSSGraphXPulseMetadataFromDetectedPulse(
      candidate.candidate.detected_pulse, sample_time_map);
  out.provisional_slot_index = candidate.candidate.provisional_slot_index;
  out.final_slot_index = candidate.candidate.final_slot_index;
  out.complex_evidence =
      FHSSGraphXComplexEvidenceFromMergeEvidence(candidate.complex_evidence,
                                                sample_time_map);
  return out;
}

[[nodiscard]] inline FHSSPulseCandidate
FHSSPulseCandidateFromGraphX(const FHSSGraphXPulseCandidate &candidate) {
  FHSSPulseCandidate out{};
  out.detected_pulse = FHSSDetectedPulseFromGraphX(candidate.pulse);
  out.provisional_slot_index = candidate.provisional_slot_index;
  out.final_slot_index = candidate.final_slot_index;
  return out;
}

[[nodiscard]] inline FHSSDecodedPulseWord
FHSSDecodedPulseWordFromGraphX(const FHSSDecodedPulseWordPacket &packet) {
  FHSSDecodedPulseWord decoded{};
  decoded.candidate.detected_pulse = FHSSDetectedPulseFromGraphX(packet.pulse);
  decoded.decoded_value = packet.decoded_value;
  decoded.confidence = packet.confidence;
  decoded.viterbi_path_metric = packet.viterbi_path_metric;
  decoded.status = packet.status == FHSSGraphXDecodeStatus::Ok
                       ? FHSSPulseWordDecodeStatus::Ok
                       : FHSSPulseWordDecodeStatus::InvalidPathMetric;
  decoded.status_message = packet.status_message;
  return decoded;
}

[[nodiscard]] inline FHSSDecodedPulseWordPacket
FHSSGraphXDecodedPulseWordFromKernel(const FHSSDecodedPulseWord &decoded) {
  return FHSSDecodedPulseWordPacket{
      .pulse = FHSSGraphXPulseMetadataFromDetectedPulse(
          decoded.candidate.detected_pulse),
      .decoded_value = decoded.decoded_value,
      .confidence = decoded.confidence,
      .viterbi_path_metric = decoded.viterbi_path_metric,
      .status = decoded.status == FHSSPulseWordDecodeStatus::Ok
                    ? FHSSGraphXDecodeStatus::Ok
                    : FHSSGraphXDecodeStatus::InvalidEvidence,
      .status_message = decoded.status_message,
      .truth_metadata_required_for_decision = false};
}

[[nodiscard]] inline FHSSGraphXTruthMismatch
FHSSGraphXTruthMismatchFromKernel(const FHSSTruthMismatch &mismatch) {
  FHSSGraphXTruthMismatchKind kind = FHSSGraphXTruthMismatchKind::StartSample;
  switch (mismatch.kind) {
  case FHSSTruthMismatchKind::StartSample:
    kind = FHSSGraphXTruthMismatchKind::StartSample;
    break;
  case FHSSTruthMismatchKind::Duration:
    kind = FHSSGraphXTruthMismatchKind::Duration;
    break;
  case FHSSTruthMismatchKind::Frequency:
    kind = FHSSGraphXTruthMismatchKind::Frequency;
    break;
  case FHSSTruthMismatchKind::Value:
    kind = FHSSGraphXTruthMismatchKind::Value;
    break;
  }
  return FHSSGraphXTruthMismatch{.pulse_index = mismatch.pulse_index,
                                 .kind = kind,
                                 .message = mismatch.message};
}

[[nodiscard]] inline FHSSDiagnosticsPacket
FHSSGraphXDiagnosticsFromKernel(const FHSSMessageDiagnostics &diagnostics) {
  return FHSSDiagnosticsPacket{.pulse_count = diagnostics.pulse_count,
                               .rejected_count = diagnostics.rejected_count,
                               .preamble_lock = diagnostics.preamble_lock,
                               .truth_mismatch_count =
                                   diagnostics.truth_mismatch_count};
}

[[nodiscard]] inline FHSSAssembledMessagePacket
FHSSGraphXAssembledMessageFromKernel(const FHSSAssembledMessage &message) {
  FHSSAssembledMessagePacket packet{};
  packet.active_frequency_indices = message.active_frequency_indices;
  packet.preamble_lock = message.diagnostics.preamble_lock;
  packet.diagnostics = FHSSGraphXDiagnosticsFromKernel(message.diagnostics);
  packet.status = message.status == FHSSMessageAssemblyStatus::Ok
                      ? FHSSGraphXDecodeStatus::Ok
                      : FHSSGraphXDecodeStatus::InvalidEvidence;
  packet.status_message = message.status_message;
  packet.truth_is_validation_only = true;
  packet.ordered_pulses.reserve(message.ordered_pulses.size());
  for (const auto &decoded : message.ordered_pulses) {
    packet.ordered_pulses.push_back(FHSSGraphXDecodedPulseWordFromKernel(decoded));
  }
  packet.truth_mismatches.reserve(message.truth_mismatches.size());
  for (const auto &mismatch : message.truth_mismatches) {
    packet.truth_mismatches.push_back(FHSSGraphXTruthMismatchFromKernel(mismatch));
  }
  return packet;
}

class FHSSSyntheticIqSourceNode
    : public graph::NamedSourceNode<FHSSSyntheticIqSourceNode,
                                    FHSSSyntheticIqToken> {
public:
  using OutputTokenType = FHSSSyntheticIqToken;

  FHSSSyntheticIqSourceNode() = default;
  explicit FHSSSyntheticIqSourceNode(FHSSSyntheticIqGeneratorConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSSyntheticIqGeneratorConfig config) {
    config_ = std::move(config);
    emitted_ = false;
  }

  std::optional<OutputTokenType>
  Produce(std::integral_constant<std::size_t, 0>) override {
    if (emitted_) {
      return std::nullopt;
    }
    emitted_ = true;

    auto fixture = GenerateSyntheticIqFixture(config_);
    if (!fixture) {
      return std::nullopt;
    }

    auto samples =
        std::make_shared<const std::vector<std::complex<double>>>(
            std::move(fixture->samples));
    FHSSGraphXSampleTimeMap sample_time_map{};
    sample_time_map.input_packet_global_start_sample = 0;
    OutputTokenType token{};
    token.token_id = next_token_id_++;
    token.sidecar.iq = FHSSGraphXComplexEvidenceFromHostSamples(
        samples, samples->size(), sample_time_map);
    token.sidecar.truth_pulses = std::move(fixture->truth_pulses);
    token.sidecar.timing = fixture->timing;
    token.sidecar.truth_is_validation_only = true;
    return token;
  }

private:
  FHSSSyntheticIqGeneratorConfig config_{};
  bool emitted_{false};
  std::uint64_t next_token_id_{1};
};

class FHSSCorrelatorBankDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSSyntheticIqToken>,
          graph::TypeList<FHSSDetectedPulseToken>,
          FHSSCorrelatorBankDetectorNode> {
public:
  using InputTokenType = FHSSSyntheticIqToken;
  using OutputTokenType = FHSSDetectedPulseToken;

  FHSSCorrelatorBankDetectorNode() = default;
  explicit FHSSCorrelatorBankDetectorNode(
      FHSSCorrelatorBankDetectorConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSCorrelatorBankDetectorConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (!FHSSGraphXEvidenceHasHostComplexIq(input.sidecar.iq)) {
      return std::nullopt;
    }

    auto result = FHSSCorrelatorBankDetectorKernel::Detect(
        *input.sidecar.iq.host_complex64_samples, config_);
    if (!result) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.source_iq = input.sidecar.iq;
    output.sidecar.detected_pulses.reserve(result->local_detections.size());
    output.sidecar.pulse_evidence.reserve(result->local_detections.size());
    for (const auto &local : result->local_detections) {
      auto normalized = NormalizeLocalDetection(local);
      if (!normalized) {
        return std::nullopt;
      }
      const auto map =
          FHSSGraphXSampleTimeMapFromMergeMap(local.sample_time_map);
      output.sidecar.detected_pulses.push_back(
          FHSSGraphXPulseMetadataFromDetectedPulse(
              normalized->candidate.detected_pulse, map));
      output.sidecar.pulse_evidence.push_back(
          FHSSGraphXComplexEvidenceFromMergeEvidence(local.complex_evidence,
                                                     map));
    }
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  FHSSCorrelatorBankDetectorConfig config_{};
};

class FHSSPulseMergeNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDetectedPulseToken>,
          graph::TypeList<FHSSPulseCandidateToken>, FHSSPulseMergeNode> {
public:
  using InputTokenType = FHSSDetectedPulseToken;
  using OutputTokenType = FHSSPulseCandidateToken;

  FHSSPulseMergeNode() = default;
  explicit FHSSPulseMergeNode(FHSSPulseMergeConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseMergeConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (input.sidecar.detected_pulses.size() !=
        input.sidecar.pulse_evidence.size()) {
      return std::nullopt;
    }

    std::vector<FHSSLocalPulseDetection> local_detections;
    local_detections.reserve(input.sidecar.detected_pulses.size());
    for (std::size_t i = 0; i < input.sidecar.detected_pulses.size(); ++i) {
      local_detections.push_back(FHSSLocalPulseDetectionFromGraphX(
          input.sidecar.detected_pulses[i], input.sidecar.pulse_evidence[i]));
    }

    auto merged = FHSSPulseMergeKernel::Merge(local_detections, config_);
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.ordered_candidates.reserve(merged.ordered_candidates.size());
    for (const auto &candidate : merged.ordered_candidates) {
      output.sidecar.ordered_candidates.push_back(
          FHSSGraphXPulseCandidateFromMergeCandidate(candidate));
    }
    output.sidecar.globally_ordered = true;
    output.sidecar.unsupported_overlap_rejected = true;
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  FHSSPulseMergeConfig config_{};
};

class FHSSPulseCandidateNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSPulseCandidateToken>,
          graph::TypeList<FHSSPulseCandidateToken>, FHSSPulseCandidateNode> {
public:
  using TokenType = FHSSPulseCandidateToken;

  std::optional<TokenType>
  Transfer(const TokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    return input;
  }
};

class CPSMBranchMetricNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSPulseCandidateToken>,
          graph::TypeList<FHSSCpsmBranchMetricToken>, CPSMBranchMetricNode> {
public:
  using InputTokenType = FHSSPulseCandidateToken;
  using OutputTokenType = FHSSCpsmBranchMetricToken;

  CPSMBranchMetricNode() = default;
  explicit CPSMBranchMetricNode(CPSMDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(CPSMDecoderConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    if (input.sidecar.ordered_candidates.empty()) {
      return std::nullopt;
    }
    const auto &candidate = input.sidecar.ordered_candidates.front();
    if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
      return std::nullopt;
    }
    auto metrics = CPSMBranchMetricKernel::Compute(
        *candidate.complex_evidence.host_complex64_samples, config_);
    if (!metrics) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.candidate = candidate;
    output.sidecar.trellis_state_count = CPSMPhaseStateCount(config_.modulation_index);
    output.sidecar.branch_costs.reserve(metrics->size());
    for (const auto &metric : *metrics) {
      output.sidecar.branch_costs.push_back(metric.cost);
    }
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  CPSMDecoderConfig config_{};
};

class CPSMViterbiDecoderNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSCpsmBranchMetricToken>,
          graph::TypeList<FHSSCpsmSymbolDecisionToken>,
          CPSMViterbiDecoderNode> {
public:
  using InputTokenType = FHSSCpsmBranchMetricToken;
  using OutputTokenType = FHSSCpsmSymbolDecisionToken;

  CPSMViterbiDecoderNode() = default;
  explicit CPSMViterbiDecoderNode(CPSMDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(CPSMDecoderConfig config) { config_ = std::move(config); }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    const auto &candidate = input.sidecar.candidate;
    if (!FHSSGraphXEvidenceHasHostComplexIq(candidate.complex_evidence)) {
      return std::nullopt;
    }
    auto decoded = CPSMViterbiDecoderKernel::Decode(
        *candidate.complex_evidence.host_complex64_samples, config_);
    if (!decoded) {
      return std::nullopt;
    }

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.pulse = candidate.pulse;
    output.sidecar.symbols = std::move(decoded->symbols);
    output.sidecar.phase_states = std::move(decoded->phase_states);
    output.sidecar.best_path_metric = decoded->best_path_metric;
    output.sidecar.confidence = decoded->confidence;
    output.sidecar.status = FHSSGraphXDecodeStatus::Ok;
    output.sidecar.status_message = "Ok";
    output.sidecar.truth_metadata_required_for_decision = false;
    return output;
  }

private:
  CPSMDecoderConfig config_{};
};

class FHSSPulseWordDecoderNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSCpsmSymbolDecisionToken>,
          graph::TypeList<FHSSDecodedPulseWordToken>,
          FHSSPulseWordDecoderNode> {
public:
  using InputTokenType = FHSSCpsmSymbolDecisionToken;
  using OutputTokenType = FHSSDecodedPulseWordToken;

  FHSSPulseWordDecoderNode() = default;
  explicit FHSSPulseWordDecoderNode(FHSSPulseWordDecoderConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSPulseWordDecoderConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    CPSMViterbiResult viterbi{};
    viterbi.symbols = input.sidecar.symbols;
    viterbi.phase_states = input.sidecar.phase_states;
    viterbi.best_path_metric = input.sidecar.best_path_metric;
    viterbi.confidence = input.sidecar.confidence;

    FHSSPulseCandidate candidate{};
    candidate.detected_pulse = FHSSDetectedPulseFromGraphX(input.sidecar.pulse);
    auto decoded =
        FHSSPulseWordDecoderKernel::Decode(candidate, viterbi, config_);

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = FHSSGraphXDecodedPulseWordFromKernel(decoded);
    return output;
  }

private:
  FHSSPulseWordDecoderConfig config_{};
};

class FHSSPreambleDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDecodedPulseWordsToken>,
          graph::TypeList<FHSSAssembledMessageToken>,
          FHSSPreambleDetectorNode> {
public:
  using InputTokenType = FHSSDecodedPulseWordsToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSPreambleDetectorNode() = default;
  explicit FHSSPreambleDetectorNode(std::vector<FHSSPreamblePulseSpec> preamble)
      : preamble_(std::move(preamble)) {}

  void SetPreamble(std::vector<FHSSPreamblePulseSpec> preamble) {
    preamble_ = std::move(preamble);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    std::vector<FHSSDecodedPulseWord> decoded;
    decoded.reserve(input.sidecar.decoded_pulses.size());
    for (const auto &packet : input.sidecar.decoded_pulses) {
      decoded.push_back(FHSSDecodedPulseWordFromGraphX(packet));
    }
    decoded = GloballyOrderDecodedPulses(std::move(decoded));

    auto lock = FHSSPreambleDetectorKernel::Detect(decoded, preamble_);
    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar.ordered_pulses = input.sidecar.decoded_pulses;
    output.sidecar.active_frequency_indices = lock.active_frequency_indices;
    output.sidecar.preamble_lock = lock.preamble_lock;
    output.sidecar.diagnostics.pulse_count = input.sidecar.decoded_pulses.size();
    output.sidecar.diagnostics.preamble_lock = lock.preamble_lock;
    output.sidecar.status = lock.status == FHSSMessageAssemblyStatus::Ok
                                ? FHSSGraphXDecodeStatus::Ok
                                : FHSSGraphXDecodeStatus::InvalidEvidence;
    output.sidecar.status_message = lock.status_message;
    return output;
  }

private:
  std::vector<FHSSPreamblePulseSpec> preamble_{};
};

class FHSSMessageAssemblerNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSDecodedPulseWordsToken>,
          graph::TypeList<FHSSAssembledMessageToken>,
          FHSSMessageAssemblerNode> {
public:
  using InputTokenType = FHSSDecodedPulseWordsToken;
  using OutputTokenType = FHSSAssembledMessageToken;

  FHSSMessageAssemblerNode() = default;
  explicit FHSSMessageAssemblerNode(FHSSMessageAssemblerConfig config)
      : config_(std::move(config)) {}

  void SetConfig(FHSSMessageAssemblerConfig config) {
    config_ = std::move(config);
  }

  std::optional<OutputTokenType>
  Transfer(const InputTokenType &input, std::integral_constant<std::size_t, 0>,
           std::integral_constant<std::size_t, 0>) override {
    std::vector<FHSSDecodedPulseWord> decoded;
    decoded.reserve(input.sidecar.decoded_pulses.size());
    for (const auto &packet : input.sidecar.decoded_pulses) {
      decoded.push_back(FHSSDecodedPulseWordFromGraphX(packet));
    }
    auto assembled =
        FHSSMessageAssemblerKernel::Assemble(std::move(decoded), config_);

    OutputTokenType output{};
    output.token_id = input.token_id;
    output.sidecar = FHSSGraphXAssembledMessageFromKernel(assembled);
    return output;
  }

private:
  FHSSMessageAssemblerConfig config_{};
};

class FHSSMessageSinkNode
    : public graph::NamedSinkNode<FHSSMessageSinkNode,
                                  FHSSAssembledMessageToken> {
public:
  using InputTokenType = FHSSAssembledMessageToken;

  bool Consume(const InputTokenType &input,
               std::integral_constant<std::size_t, 0>) override {
    last_diagnostics_ = input.sidecar.diagnostics;
    return true;
  }

  [[nodiscard]] const FHSSDiagnosticsPacket &last_diagnostics() const {
    return last_diagnostics_;
  }

private:
  FHSSDiagnosticsPacket last_diagnostics_{};
};

} // namespace dsp::fhss
