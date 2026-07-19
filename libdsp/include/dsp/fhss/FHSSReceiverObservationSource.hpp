// SPDX-License-Identifier: MIT
#pragma once

#include "graph/IConfigurable.hpp"

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dsp::fhss {

struct FHSSObservedPulseProduct {
  std::uint64_t global_start_sample = 0;
  std::uint64_t duration_samples = 0;
  std::uint32_t logical_frequency_index = 0;
  std::uint32_t physical_channel_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  double estimated_center_frequency_hz = 0.0;
  double detector_frequency_error_hz_unqualified = 0.0;
  double confidence_score_uncalibrated = 0.0;
  double viterbi_path_metric = 0.0;
  double viterbi_second_best_path_metric = 0.0;
  std::uint32_t decoded_value = 0;
};

struct FHSSReceiverSampleCapture {
  std::uint32_t logical_frequency_index = 0;
  std::uint32_t physical_channel_index = 0;
  double rf_frequency_hz = 0.0;
  double iq_offset_frequency_hz = 0.0;
  double sample_rate_hz = 0.0;
  std::uint64_t global_start_sample = 0;
  std::uint32_t input_sample_interval = 1;
  std::vector<std::complex<double>> samples;
  std::size_t original_sample_count = 0;
  bool truncated = false;
};

struct FHSSReceiverNodeObservationSnapshot {
  std::string source_schema;
  std::string source_kind;
  std::size_t allocation_high_water_bytes = 0;
  std::optional<std::size_t> detected_pulse_count;
  std::optional<std::size_t> rejected_count;
  std::vector<std::string> rejection_reason_codes;
  std::optional<bool> preamble_lock;
  std::vector<std::uint32_t> receiver_derived_active_frequencies;
  std::optional<std::string> assembler_status;
  std::optional<std::string> receiver_message_status;
  std::optional<bool> receiver_message_accepted;
  std::vector<FHSSObservedPulseProduct> decoded_pulses;
  std::vector<FHSSReceiverSampleCapture> sample_captures;
};

class IFHSSReceiverObservationSource : public virtual graph::IDiagnosable {
public:
  ~IFHSSReceiverObservationSource() override = default;
  [[nodiscard]] virtual std::shared_ptr<
      const FHSSReceiverNodeObservationSnapshot>
  SnapshotReceiverObservation() const = 0;
};

} // namespace dsp::fhss
