/**
 * @file FHSSGraphXPackets.hpp
 * @brief Canonical GraphX FHSS edge packet contracts.
 *
 * @details Canonical data contracts for the current GraphX FHSS decoder lane.
 * These packet types define public GraphX edge payloads while keeping future
 * accelerator storage separate from FHSS semantic metadata. Token readiness
 * does not claim GPU execution, production channelization, impairment support,
 * or overlap-aware separation.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "dsp/fhss/FHSSProtocol.hpp"
#include "graph/dashboard/FHSSStepping.hpp"

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
  DownconvertedIq,
  ChannelizedIq,
  PerChannelPulseEvidence,
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

inline constexpr std::array<FHSSGraphXEdgeContractDescriptor, 11>
    kFHSSGraphXEdgeContracts{{
        {FHSSGraphXEdgeContract::SyntheticIqOutput, "SyntheticIqOutput",
         "FHSSSyntheticIqOutputPacket", true},
        {FHSSGraphXEdgeContract::DownconvertedIq, "DownconvertedIq",
         "FHSSDownconvertedIqPacket", true},
        {FHSSGraphXEdgeContract::ChannelizedIq, "ChannelizedIq",
         "FHSSChannelizedIqPacket", true},
        {FHSSGraphXEdgeContract::PerChannelPulseEvidence,
         "PerChannelPulseEvidence", "FHSSPerChannelPulseEvidencePacket", true},
        {FHSSGraphXEdgeContract::DetectedPulseEvidence,
         "DetectedPulseEvidence", "FHSSDetectedPulseEvidencePacket", true},
        {FHSSGraphXEdgeContract::PulseCandidateEvidence,
         "PulseCandidateEvidence", "FHSSPulseCandidateEvidencePacket", true},
        {FHSSGraphXEdgeContract::CpsmBranchMetrics, "CpsmBranchMetrics",
         "FHSSCpsmBranchMetricPacket", true},
        {FHSSGraphXEdgeContract::CpsmSymbolDecisions, "CpsmSymbolDecisions",
         "FHSSCpsmSymbolDecisionPacket", true},
        {FHSSGraphXEdgeContract::DecodedPulseWords, "DecodedPulseWords",
         "FHSSDecodedPulseWordsPacket", true},
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
  bool downconverter_passthrough = true;
  double downconverter_translation_frequency_hz = 0.0;
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

enum class FHSSGraphXDownconverterPhaseConvention {
  OutputTimesExpNegativeJTwoPiTranslationT,
  PassthroughNoPhaseRotation
};

struct FHSSGraphXDownconverterMetadata {
  double input_iq_center_frequency_hz = 0.0;
  double input_reference_frequency_hz = 0.0;
  double output_iq_center_frequency_hz = 0.0;
  double output_reference_frequency_hz = 0.0;
  double translation_frequency_hz = 0.0;
  bool passthrough = true;
  FHSSGraphXDownconverterPhaseConvention phase_convention =
      FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation;
  double sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  std::uint64_t input_global_start_sample = 0;
  std::uint64_t output_global_start_sample = 0;
  FHSSGraphXSampleTimeMap sample_time_map{};
};

struct FHSSGraphXChannelMetadata {
  std::uint32_t channel_id = 0;
  std::uint32_t frequency_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  bool downconverter_passthrough = true;
  double downconverter_translation_frequency_hz = 0.0;
  double channel_sample_rate_hz = FHSSProtocolConstants::kSampleRateHz;
  std::uint32_t decimation_factor = 1;
  std::int64_t filter_group_delay_input_samples = 0;
  std::uint64_t input_global_start_sample = 0;
  std::uint64_t channel_global_start_sample = 0;
  FHSSGraphXSampleTimeMap sample_time_map{};
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

struct FHSSCpsmPulseBranchMetric {
  FHSSGraphXPulseCandidate candidate{};
  std::vector<double> branch_costs;
  std::uint32_t trellis_state_count = 0;
  double best_path_metric = 0.0;
  double second_best_path_metric = 0.0;
};

struct FHSSCpsmPulseSymbolDecision {
  FHSSGraphXPulseMetadata pulse{};
  std::vector<double> symbols;
  std::vector<std::uint32_t> phase_states;
  double best_path_metric = 0.0;
  double confidence = 0.0;
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
};

struct FHSSSyntheticIqOutputPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXComplexEvidence iq{};
  std::vector<FHSSTruthPulse> truth_pulses;
  FHSSTimingModel timing{};
  bool truth_is_validation_only = true;
};

struct FHSSDownconvertedIqPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXComplexEvidence iq{};
  FHSSGraphXDownconverterMetadata downconverter{};
  bool truth_metadata_required_for_decision = false;
};

struct FHSSChannelizedIqPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXChannelMetadata channel{};
  FHSSGraphXComplexEvidence iq{};
  bool receiver_guard_or_metadata_channel = false;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSPerChannelPulseEvidencePacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXChannelMetadata channel{};
  std::vector<FHSSGraphXPulseMetadata> detected_pulses;
  std::vector<FHSSGraphXComplexEvidence> pulse_evidence;
  FHSSGraphXComplexEvidence channel_iq{};
  bool truth_metadata_required_for_decision = false;
};

struct FHSSDetectedPulseEvidencePacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  std::vector<FHSSGraphXPulseMetadata> detected_pulses;
  std::vector<FHSSGraphXComplexEvidence> pulse_evidence;
  FHSSGraphXComplexEvidence source_iq{};
  bool truth_metadata_required_for_decision = false;
};

struct FHSSPulseCandidateEvidencePacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  std::vector<FHSSGraphXPulseCandidate> ordered_candidates;
  bool globally_ordered = false;
  bool unsupported_overlap_rejected = true;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSCpsmBranchMetricPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXPulseCandidate candidate{};
  std::vector<double> branch_costs;
  std::vector<FHSSCpsmPulseBranchMetric> pulse_metrics;
  std::uint32_t trellis_state_count = 0;
  double best_path_metric = 0.0;
  double second_best_path_metric = 0.0;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSCpsmSymbolDecisionPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXPulseMetadata pulse{};
  std::vector<double> symbols;
  std::vector<std::uint32_t> phase_states;
  std::vector<FHSSCpsmPulseSymbolDecision> pulse_decisions;
  double best_path_metric = 0.0;
  double confidence = 0.0;
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSDecodedPulseWordPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  FHSSGraphXPulseMetadata pulse{};
  std::uint32_t decoded_value = 0;
  double confidence = 0.0;
  double viterbi_path_metric = 0.0;
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
  bool truth_metadata_required_for_decision = false;
};

struct FHSSDecodedPulseWordsPacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  std::vector<FHSSDecodedPulseWordPacket> decoded_pulses;
  bool globally_ordered = false;
  bool truth_metadata_required_for_decision = false;
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
  bool unsupported_overlap_rejected = true;
  bool unsupported_impairments_rejected = true;
  std::string synchronization_assumption = "known message_start_sample = 0";
};

struct FHSSAssembledMessagePacket {
  graph::dashboard::FHSSMessageCorrelation correlation{};
  std::vector<FHSSDecodedPulseWordPacket> ordered_pulses;
  std::vector<std::uint32_t> active_frequency_indices;
  bool preamble_lock = false;
  FHSSDiagnosticsPacket diagnostics{};
  FHSSGraphXDecodeStatus status = FHSSGraphXDecodeStatus::Ok;
  std::string status_message;
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

[[nodiscard]] inline bool
FHSSGraphXDownconverterMetadataIsValid(
    const FHSSGraphXDownconverterMetadata &metadata) {
  if (metadata.sample_rate_hz <= 0.0) {
    return false;
  }
  if (metadata.passthrough) {
    return NearlyEqual(metadata.translation_frequency_hz, 0.0) &&
           metadata.phase_convention ==
               FHSSGraphXDownconverterPhaseConvention::
                   PassthroughNoPhaseRotation &&
           NearlyEqual(metadata.input_iq_center_frequency_hz,
                       metadata.output_iq_center_frequency_hz);
  }
  return metadata.phase_convention ==
         FHSSGraphXDownconverterPhaseConvention::
             OutputTimesExpNegativeJTwoPiTranslationT;
}

[[nodiscard]] inline bool
FHSSGraphXChannelCountMatchesFrequencyTable(
    std::size_t channel_count, const FHSSFrequencyConfig &config = {}) {
  return ValidateFrequencyConfig(config).has_value() &&
         channel_count == config.frequency_count;
}

[[nodiscard]] inline bool
FHSSGraphXChannelMetadataMatchesFrequencyEntry(
    const FHSSGraphXChannelMetadata &metadata,
    const FHSSFrequencyMapEntry &entry) {
  return ValidateFrequencyIndex(metadata.frequency_index).has_value() &&
         metadata.channel_id == metadata.frequency_index &&
         metadata.frequency_index == entry.index &&
         NearlyEqual(metadata.rf_frequency_hz, entry.rf_frequency_hz) &&
         NearlyEqual(metadata.iq_offset_frequency_hz,
                     entry.iq_offset_frequency_hz) &&
         metadata.decimation_factor > 0 &&
         metadata.channel_sample_rate_hz > 0.0;
}

[[nodiscard]] inline std::int64_t
FHSSGraphXInputGlobalSampleForChannelSample(
    const FHSSGraphXChannelMetadata &metadata,
    std::uint64_t channel_sample) {
  return static_cast<std::int64_t>(metadata.input_global_start_sample) +
         metadata.filter_group_delay_input_samples +
         static_cast<std::int64_t>(channel_sample) *
             static_cast<std::int64_t>(metadata.decimation_factor);
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
