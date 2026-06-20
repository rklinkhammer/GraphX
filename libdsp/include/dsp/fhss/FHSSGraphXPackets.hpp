/**
 * @file FHSSGraphXPackets.hpp
 * @brief Canonical GraphX FHSS edge packet contracts.
 *
 * @details PR7A data-only contracts for the FHSS decoder lane. These packet
 * types define the public GraphX edge payloads used by future runtime nodes.
 * They deliberately do not add graph JSON, plugin wiring, GPU execution,
 * channelization, Doppler/noise behavior, or overlap-aware separation.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSProtocol.hpp"

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss {

enum class FHSSGraphXEdgeContract {
  SyntheticIqOutput,
  DetectedPulseEvidence,
  PulseCandidateEvidence,
  CpsmBranchMetrics,
  CpsmSymbolDecisions,
  DecodedPulseWords,
  AssembledMessages,
  Diagnostics
};

struct FHSSGraphXEdgeContractDescriptor {
  FHSSGraphXEdgeContract edge = FHSSGraphXEdgeContract::SyntheticIqOutput;
  const char *edge_name = "";
  const char *packet_type_name = "";
  bool future_accel_sidecar_compatible = true;
};

inline constexpr std::array<FHSSGraphXEdgeContractDescriptor, 8>
    kFHSSGraphXEdgeContracts{{
        {FHSSGraphXEdgeContract::SyntheticIqOutput, "SyntheticIqOutput",
         "FHSSSyntheticIqOutputPacket", true},
        {FHSSGraphXEdgeContract::DetectedPulseEvidence,
         "DetectedPulseEvidence", "FHSSDetectedPulseEvidencePacket", true},
        {FHSSGraphXEdgeContract::PulseCandidateEvidence,
         "PulseCandidateEvidence", "FHSSPulseCandidateEvidencePacket", true},
        {FHSSGraphXEdgeContract::CpsmBranchMetrics, "CpsmBranchMetrics",
         "FHSSCpsmBranchMetricPacket", true},
        {FHSSGraphXEdgeContract::CpsmSymbolDecisions, "CpsmSymbolDecisions",
         "FHSSCpsmSymbolDecisionPacket", true},
        {FHSSGraphXEdgeContract::DecodedPulseWords, "DecodedPulseWords",
         "FHSSDecodedPulseWordPacket", true},
        {FHSSGraphXEdgeContract::AssembledMessages, "AssembledMessages",
         "FHSSAssembledMessagePacket", true},
        {FHSSGraphXEdgeContract::Diagnostics, "Diagnostics",
         "FHSSDiagnosticsPacket", true},
    }};

enum class FHSSGraphXSampleFormat {
  ComplexFloat64,
  ComplexFloat32
};

enum class FHSSGraphXPayloadResidency {
  Empty,
  HostSharedImmutable,
  ExternalImmutableReference,
  FutureAccelTokenSidecar
};

enum class FHSSGraphXDecodeStatus {
  Ok,
  LowConfidence,
  InvalidEvidence,
  Unsupported
};

enum class FHSSGraphXTruthMismatchKind {
  StartSample,
  Duration,
  Frequency,
  Value
};

struct FHSSGraphXSampleTimeMap {
  bool has_input_global_start_sample = true;
  std::uint64_t input_packet_global_start_sample = 0;
  std::uint64_t output_start_sample = 0;
  std::uint32_t decimation_factor = 1;
  std::int64_t group_delay_input_samples = 0;
  double input_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  double output_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
};

struct FHSSGraphXComplexEvidence {
  using Complex64Samples = std::vector<std::complex<double>>;

  std::shared_ptr<const Complex64Samples> host_complex64_samples{};
  std::uint64_t sample_offset = 0;
  std::uint64_t sample_count = 0;
  FHSSGraphXSampleFormat sample_format = FHSSGraphXSampleFormat::ComplexFloat64;
  FHSSGraphXPayloadResidency residency =
      FHSSGraphXPayloadResidency::HostSharedImmutable;
  FHSSGraphXSampleTimeMap sample_time_map{};

  // CPU PRs require complex IQ evidence. Future accelerator tokens may carry
  // the samples out of band, but the FHSS semantic metadata remains here.
  bool decoder_usable_complex_iq = true;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSGraphXFrequencyMetadata {
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  double estimated_center_frequency_hz = 0.0;
  double frequency_error_hz = 0.0;
};

struct FHSSGraphXPulseTiming {
  std::uint64_t global_start_sample = 0;
  std::uint64_t global_end_sample = 0;
  std::uint64_t duration_samples = 0;
  std::uint64_t channel_start_sample = 0;
  std::uint32_t channel_id = 0;
  FHSSGraphXSampleTimeMap sample_time_map{};
};

struct FHSSGraphXPulseMetadata {
  FHSSGraphXPulseTiming timing{};
  FHSSGraphXFrequencyMetadata frequency{};
  double amplitude = 0.0;
  double power_db = 0.0;
  double snr_db = 0.0;
  double noise_floor_db = 0.0;
  double phase_at_start_rad = 0.0;
  double phase_slope_rad_per_sample = 0.0;
  double cfo_hz = 0.0;
  double bandwidth_hz = 0.0;
  double confidence = 0.0;
  std::uint64_t detector_id = 0;
  std::uint64_t packet_sequence = 0;
};

struct FHSSGraphXPulseCandidate {
  FHSSGraphXPulseMetadata pulse{};
  std::optional<std::uint64_t> provisional_slot_index{};
  std::optional<std::uint64_t> final_slot_index{};
  FHSSGraphXComplexEvidence complex_evidence{};
};

struct FHSSGraphXTruthMismatch {
  std::size_t pulse_index = 0;
  FHSSGraphXTruthMismatchKind kind = FHSSGraphXTruthMismatchKind::StartSample;
  std::string message;
};

struct FHSSSyntheticIqOutputPacket {
  FHSSGraphXComplexEvidence iq{};
  std::vector<FHSSTruthPulse> truth_pulses;
  FHSSTimingModel timing{};
  bool truth_is_validation_only = true;
};

struct FHSSDetectedPulseEvidencePacket {
  std::vector<FHSSGraphXPulseMetadata> detected_pulses;
  FHSSGraphXComplexEvidence source_iq{};
  bool truth_metadata_required_for_decision = false;
};

struct FHSSPulseCandidateEvidencePacket {
  std::vector<FHSSGraphXPulseCandidate> ordered_candidates;
  bool globally_ordered = false;
  bool unsupported_overlap_rejected = true;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSCpsmBranchMetricPacket {
  FHSSGraphXPulseCandidate candidate{};
  std::vector<double> branch_costs;
  std::uint32_t trellis_state_count = 0;
  double best_path_metric = 0.0;
  double second_best_path_metric = 0.0;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSCpsmSymbolDecisionPacket {
  FHSSGraphXPulseMetadata pulse{};
  std::vector<double> symbols;
  std::vector<std::uint32_t> phase_states;
  double best_path_metric = 0.0;
  double confidence = 0.0;
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSDecodedPulseWordPacket {
  FHSSGraphXPulseMetadata pulse{};
  std::uint32_t decoded_value = 0;
  double confidence = 0.0;
  double viterbi_path_metric = 0.0;
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSAssembledMessagePacket {
  std::vector<FHSSDecodedPulseWordPacket> ordered_pulses;
  std::vector<std::uint32_t> active_frequency_indices;
  bool preamble_lock = false;
  std::vector<FHSSGraphXTruthMismatch> truth_mismatches;
  bool truth_is_validation_only = true;
};

struct FHSSDiagnosticsPacket {
  std::size_t pulse_count = 0;
  std::size_t rejected_count = 0;
  bool preamble_lock = false;
  std::size_t truth_mismatch_count = 0;
  std::optional<std::uint64_t> global_start_sample{};
  std::optional<std::uint32_t> frequency_index{};
  std::optional<double> confidence{};
  std::optional<double> viterbi_path_metric{};
  std::optional<std::uint32_t> decoded_value{};
  std::vector<FHSSGraphXTruthMismatch> truth_mismatches;
  bool truth_is_validation_only = true;
};

struct FHSSFutureAccelSidecarContract {
  FHSSGraphXPayloadResidency residency =
      FHSSGraphXPayloadResidency::FutureAccelTokenSidecar;
  bool carries_same_semantic_metadata = true;
  bool cpu_execution_required_by_contract = false;
  bool gpu_execution_added_by_pr7a = false;
  const char *boundary =
      "Future accelerator tokens may carry sample storage out of band, while "
      "GraphX FHSS packets retain timing, frequency, confidence, and decode "
      "metadata on the edge sidecar.";
};

[[nodiscard]] inline bool
FHSSGraphXEvidenceRangeIsValid(const FHSSGraphXComplexEvidence &evidence) {
  if (evidence.residency == FHSSGraphXPayloadResidency::Empty) {
    return evidence.sample_count == 0 && evidence.sample_offset == 0;
  }
  if (evidence.host_complex64_samples == nullptr) {
    return evidence.residency != FHSSGraphXPayloadResidency::HostSharedImmutable;
  }
  return evidence.sample_offset <= evidence.host_complex64_samples->size() &&
         evidence.sample_count <=
             evidence.host_complex64_samples->size() - evidence.sample_offset;
}

[[nodiscard]] inline bool
FHSSGraphXEvidenceHasHostComplexIq(const FHSSGraphXComplexEvidence &evidence) {
  return evidence.decoder_usable_complex_iq &&
         evidence.residency == FHSSGraphXPayloadResidency::HostSharedImmutable &&
         evidence.host_complex64_samples != nullptr &&
         FHSSGraphXEvidenceRangeIsValid(evidence);
}

[[nodiscard]] inline bool
FHSSGraphXEvidenceIsFutureAccelSidecarCompatible(
    const FHSSGraphXComplexEvidence &evidence) {
  return evidence.residency ==
             FHSSGraphXPayloadResidency::FutureAccelTokenSidecar &&
         !evidence.truth_metadata_required_for_decision &&
         evidence.sample_time_map.has_input_global_start_sample;
}

[[nodiscard]] inline FHSSGraphXPulseMetadata
FHSSGraphXPulseMetadataFromDetectedPulse(
    const FHSSDetectedPulse &pulse,
    const FHSSGraphXSampleTimeMap &sample_time_map = {}) {
  FHSSGraphXPulseMetadata metadata{};
  metadata.timing.global_start_sample = pulse.global_start_sample;
  metadata.timing.global_end_sample = pulse.global_end_sample;
  metadata.timing.duration_samples = pulse.duration_samples;
  metadata.timing.channel_start_sample = pulse.channel_start_sample;
  metadata.timing.channel_id = pulse.channel_id;
  metadata.timing.sample_time_map = sample_time_map;
  metadata.frequency.frequency_index = pulse.frequency_index;
  metadata.frequency.rf_frequency_hz = pulse.rf_frequency_hz;
  metadata.frequency.iq_offset_frequency_hz = pulse.iq_offset_frequency_hz;
  metadata.frequency.estimated_center_frequency_hz =
      pulse.estimated_center_frequency_hz;
  metadata.frequency.frequency_error_hz = pulse.frequency_error_hz;
  metadata.amplitude = pulse.amplitude;
  metadata.power_db = pulse.power_db;
  metadata.snr_db = pulse.snr_db;
  metadata.noise_floor_db = pulse.noise_floor_db;
  metadata.phase_at_start_rad = pulse.phase_at_start_rad;
  metadata.phase_slope_rad_per_sample = pulse.phase_slope_rad_per_sample;
  metadata.cfo_hz = pulse.cfo_hz;
  metadata.bandwidth_hz = pulse.bandwidth_hz;
  metadata.confidence = pulse.confidence;
  metadata.detector_id = pulse.detector_id;
  metadata.packet_sequence = pulse.packet_sequence;
  return metadata;
}

[[nodiscard]] inline bool
FHSSGraphXDecisionContractsRequireTruthMetadata(
    const FHSSCpsmBranchMetricPacket &branch_metrics,
    const FHSSCpsmSymbolDecisionPacket &symbol_decisions,
    const FHSSDecodedPulseWordPacket &decoded_word) {
  return branch_metrics.truth_metadata_required_for_decision ||
         symbol_decisions.truth_metadata_required_for_decision ||
         decoded_word.truth_metadata_required_for_decision;
}

} // namespace dsp::fhss
