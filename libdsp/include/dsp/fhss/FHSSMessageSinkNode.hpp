#pragma once

#include "dsp/fhss/FHSSFixtureUtils.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"
#include "dsp/fhss/FHSSReceiverObservationSource.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <mutex>
#include <type_traits>

namespace dsp::fhss {

class FHSSMessageSinkNode
    : public graph::NamedSinkNode<FHSSMessageSinkNode,
                                  FHSSAssembledMessageToken>,
      public IFHSSReceiverObservationSource,
      public graph::CompletionCallbackProvider {
public:
  using InputTokenType = FHSSAssembledMessageToken;

  bool Consume(const InputTokenType &input,
               std::integral_constant<std::size_t, 0>) override {
    {
      const std::lock_guard lock(diagnostics_mutex_);
      last_diagnostics_ = input.sidecar.diagnostics;
      last_message_ = input.sidecar;
    }
    OnProcessingComplete();
    return true;
  }

  [[nodiscard]] FHSSDiagnosticsPacket last_diagnostics() const {
    const std::lock_guard lock(diagnostics_mutex_);
    return last_diagnostics_;
  }

  [[nodiscard]] std::shared_ptr<const FHSSReceiverNodeObservationSnapshot>
  SnapshotReceiverObservation() const override {
    const std::lock_guard lock(diagnostics_mutex_);
    auto snapshot = std::make_shared<FHSSReceiverNodeObservationSnapshot>();
    snapshot->source_schema = "graphx.fhss.message_sink.observation.v1";
    snapshot->source_kind = "message_sink";
    if (last_message_) {
      snapshot->detected_pulse_count = last_diagnostics_.pulse_count;
      snapshot->rejected_count = last_diagnostics_.rejected_count;
      snapshot->preamble_lock = last_diagnostics_.preamble_lock;
      if (last_diagnostics_.unsupported_overlap_rejected)
        snapshot->rejection_reason_codes.push_back("unsupported_overlap");
      if (last_diagnostics_.unsupported_impairments_rejected)
        snapshot->rejection_reason_codes.push_back("unsupported_impairment");
      snapshot->receiver_derived_active_frequencies =
          last_message_->active_frequency_indices;
      snapshot->assembler_status = last_message_->status_message;
      snapshot->receiver_message_status = last_message_->status_message;
      snapshot->receiver_message_accepted =
          last_message_->status == FHSSGraphXDecodeStatus::Ok &&
          last_message_->preamble_lock && !last_message_->ordered_pulses.empty();
      snapshot->decoded_pulses.reserve(last_message_->ordered_pulses.size());
      for (const auto &pulse : last_message_->ordered_pulses) {
        snapshot->decoded_pulses.push_back(
            {.global_start_sample =
                 pulse.pulse.timing.global_start_sample,
             .duration_samples = pulse.pulse.timing.duration_samples,
             .logical_frequency_index =
                 pulse.pulse.frequency.frequency_index,
             .physical_channel_index = pulse.pulse.timing.channel_id,
             .rf_frequency_hz = pulse.pulse.frequency.rf_frequency_hz,
             .iq_offset_frequency_hz =
                 pulse.pulse.frequency.iq_offset_frequency_hz,
             .estimated_center_frequency_hz =
                 pulse.pulse.frequency.estimated_center_frequency_hz,
             .detector_frequency_error_hz_unqualified =
                 pulse.pulse.frequency.frequency_error_hz,
             .confidence_score_uncalibrated = pulse.confidence,
             .viterbi_path_metric = pulse.viterbi_path_metric,
             .viterbi_second_best_path_metric =
                 pulse.viterbi_second_best_path_metric,
             .decoded_value = pulse.decoded_value});
      }
    }
    return snapshot;
  }

  [[nodiscard]] graph::JsonView GetDiagnostics() const override {
    thread_local nlohmann::json diagnostics_snapshot;
    const std::lock_guard lock(diagnostics_mutex_);
    diagnostics_snapshot = {
        {"schema", "graphx.fhss.message_sink.diagnostics.v1"},
        {"pulse_count", last_diagnostics_.pulse_count},
        {"rejected_count", last_diagnostics_.rejected_count},
        {"preamble_lock", last_diagnostics_.preamble_lock},
        {"unsupported_overlap_rejected",
         last_diagnostics_.unsupported_overlap_rejected},
        {"unsupported_impairments_rejected",
         last_diagnostics_.unsupported_impairments_rejected},
        {"synchronization_assumption",
         last_diagnostics_.synchronization_assumption},
    };
    if (last_diagnostics_.global_start_sample) {
      diagnostics_snapshot["global_start_sample"] =
          *last_diagnostics_.global_start_sample;
    }
    if (last_diagnostics_.frequency_index) {
      diagnostics_snapshot["frequency_index"] =
          *last_diagnostics_.frequency_index;
    }
    if (last_diagnostics_.confidence) {
      diagnostics_snapshot["confidence"] = *last_diagnostics_.confidence;
    }
    if (last_diagnostics_.viterbi_path_metric) {
      diagnostics_snapshot["viterbi_path_metric"] =
          *last_diagnostics_.viterbi_path_metric;
    }
    if (last_diagnostics_.decoded_value) {
      diagnostics_snapshot["decoded_value"] = *last_diagnostics_.decoded_value;
    }

    diagnostics_snapshot["decoded_pulses"] = nlohmann::json::array();
    if (last_message_) {
      diagnostics_snapshot["active_frequency_indices"] =
          last_message_->active_frequency_indices;
      diagnostics_snapshot["message_status"] = last_message_->status_message;
      for (const auto &pulse : last_message_->ordered_pulses) {
        diagnostics_snapshot["decoded_pulses"].push_back({
            {"global_start_sample", pulse.pulse.timing.global_start_sample},
            {"duration_samples", pulse.pulse.timing.duration_samples},
            {"frequency_index", pulse.pulse.frequency.frequency_index},
            {"rf_frequency_hz", pulse.pulse.frequency.rf_frequency_hz},
            {"iq_offset_frequency_hz",
             pulse.pulse.frequency.iq_offset_frequency_hz},
            {"estimated_center_frequency_hz",
             pulse.pulse.frequency.estimated_center_frequency_hz},
            {"frequency_error_hz", pulse.pulse.frequency.frequency_error_hz},
            {"cfo_hz", pulse.pulse.cfo_hz},
            {"snr_db", pulse.pulse.snr_db},
            {"channel_id", pulse.pulse.timing.channel_id},
            {"downconverter_passthrough",
             pulse.pulse.downconverter_passthrough},
            {"downconverter_translation_frequency_hz",
             pulse.pulse.downconverter_translation_frequency_hz},
            {"confidence", pulse.confidence},
            {"viterbi_path_metric", pulse.viterbi_path_metric},
            {"decoded_value", pulse.decoded_value},
            {"sample_time_mapping",
             {{"input_packet_global_start_sample",
               pulse.pulse.timing.sample_time_map
                   .input_packet_global_start_sample},
              {"output_start_sample",
               pulse.pulse.timing.sample_time_map.output_start_sample},
              {"decimation_factor",
               pulse.pulse.timing.sample_time_map.decimation_factor},
              {"group_delay_input_samples",
               pulse.pulse.timing.sample_time_map.group_delay_input_samples}}},
        });
      }
    }
    return graph::JsonView(diagnostics_snapshot);
  }

  void OnProcessingComplete() noexcept override {
    if (auto *provider =
            dynamic_cast<CompletionNodeCallback *>(this->GetCallbackProvider());
        provider != nullptr) {
      provider->OnComplete();
    }
  }

private:
  FHSSDiagnosticsPacket last_diagnostics_{};
  std::optional<FHSSAssembledMessagePacket> last_message_{};
  mutable std::mutex diagnostics_mutex_;
};

} // namespace dsp::fhss
