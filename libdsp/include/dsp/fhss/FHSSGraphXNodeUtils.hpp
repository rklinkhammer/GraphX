/**
 * @file FHSSGraphXNodeUtils.hpp
 * @brief Shared FHSS GraphX token and metadata conversion helpers.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "dsp/fhss/FHSSGraphXPackets.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "dsp/fhss/FHSSPulseMerge.hpp"
#include "dsp/fhss/FHSSPulseWordDecoder.hpp"
#include "gpu/accel/types/AccelTypes.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace dsp::fhss {

template <typename PacketT>
using FHSSGraphXToken = graph::gpu::accel::ControlToken<PacketT>;

using FHSSSyntheticIqToken = FHSSGraphXToken<FHSSSyntheticIqOutputPacket>;
using FHSSDownconvertedIqToken =
    FHSSGraphXToken<FHSSDownconvertedIqPacket>;
using FHSSChannelizedIqToken = FHSSGraphXToken<FHSSChannelizedIqPacket>;
using FHSSPerChannelPulseEvidenceToken =
    FHSSGraphXToken<FHSSPerChannelPulseEvidencePacket>;
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
static_assert(std::is_same_v<FHSSDownconvertedIqToken,
                             graph::gpu::accel::ControlToken<
                                 FHSSDownconvertedIqPacket>>);
static_assert(std::is_same_v<FHSSChannelizedIqToken,
                             graph::gpu::accel::ControlToken<
                                 FHSSChannelizedIqPacket>>);
static_assert(std::is_same_v<FHSSPerChannelPulseEvidenceToken,
                             graph::gpu::accel::ControlToken<
                                 FHSSPerChannelPulseEvidencePacket>>);
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

[[nodiscard]] inline FHSSGraphXSampleTimeMap
FHSSGraphXSampleTimeMapFromPulse(const FHSSDetectedPulse &pulse) {
  FHSSGraphXSampleTimeMap map{};
  map.input_packet_global_start_sample =
      pulse.global_start_sample - pulse.channel_start_sample;
  map.output_start_sample = pulse.global_start_sample;
  map.input_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  map.output_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  return map;
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
      candidate.candidate.detected_pulse,
      FHSSGraphXSampleTimeMapFromPulse(candidate.candidate.detected_pulse));
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
          decoded.candidate.detected_pulse,
          FHSSGraphXSampleTimeMapFromPulse(decoded.candidate.detected_pulse)),
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
  if (!packet.ordered_pulses.empty()) {
    const auto &first = packet.ordered_pulses.front();
    packet.diagnostics.global_start_sample =
        first.pulse.timing.global_start_sample;
    packet.diagnostics.frequency_index =
        first.pulse.frequency.frequency_index;
    packet.diagnostics.confidence = first.confidence;
    packet.diagnostics.viterbi_path_metric = first.viterbi_path_metric;
    packet.diagnostics.decoded_value = first.decoded_value;
  }
  packet.diagnostics.truth_is_validation_only = true;
  packet.diagnostics.unsupported_overlap_rejected = true;
  packet.diagnostics.unsupported_impairments_rejected = true;
  packet.diagnostics.synchronization_assumption =
      "known message_start_sample = 0";
  packet.truth_mismatches.reserve(message.truth_mismatches.size());
  for (const auto &mismatch : message.truth_mismatches) {
    packet.truth_mismatches.push_back(FHSSGraphXTruthMismatchFromKernel(mismatch));
  }
  packet.diagnostics.truth_mismatches = packet.truth_mismatches;
  return packet;
}

} // namespace dsp::fhss
