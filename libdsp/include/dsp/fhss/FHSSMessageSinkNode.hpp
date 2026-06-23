#pragma once

#include "dsp/fhss/FHSSGraphXNodeUtils.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <optional>
#include <type_traits>

namespace dsp::fhss {

class FHSSMessageSinkNode
    : public graph::NamedSinkNode<FHSSMessageSinkNode,
                                  FHSSAssembledMessageToken>,
      public graph::IDiagnosable,
      public graph::CompletionCallbackProvider {
public:
  using InputTokenType = FHSSAssembledMessageToken;

  bool Consume(const InputTokenType &input,
               std::integral_constant<std::size_t, 0>) override {
    last_diagnostics_ = input.sidecar.diagnostics;
    last_message_ = input.sidecar;
    OnProcessingComplete();
    return true;
  }

  [[nodiscard]] const FHSSDiagnosticsPacket &last_diagnostics() const {
    return last_diagnostics_;
  }

  [[nodiscard]] graph::JsonView GetDiagnostics() const override {
    diagnostics_cache_ = {
        {"schema", "graphx.fhss.message_sink.diagnostics.v1"},
        {"pulse_count", last_diagnostics_.pulse_count},
        {"rejected_count", last_diagnostics_.rejected_count},
        {"preamble_lock", last_diagnostics_.preamble_lock},
        {"truth_mismatch_count", last_diagnostics_.truth_mismatch_count},
        {"truth_is_validation_only", last_diagnostics_.truth_is_validation_only},
        {"unsupported_overlap_rejected",
         last_diagnostics_.unsupported_overlap_rejected},
        {"unsupported_impairments_rejected",
         last_diagnostics_.unsupported_impairments_rejected},
        {"synchronization_assumption",
         last_diagnostics_.synchronization_assumption},
    };
    if (last_diagnostics_.global_start_sample) {
      diagnostics_cache_["global_start_sample"] =
          *last_diagnostics_.global_start_sample;
    }
    if (last_diagnostics_.frequency_index) {
      diagnostics_cache_["frequency_index"] = *last_diagnostics_.frequency_index;
    }
    if (last_diagnostics_.confidence) {
      diagnostics_cache_["confidence"] = *last_diagnostics_.confidence;
    }
    if (last_diagnostics_.viterbi_path_metric) {
      diagnostics_cache_["viterbi_path_metric"] =
          *last_diagnostics_.viterbi_path_metric;
    }
    if (last_diagnostics_.decoded_value) {
      diagnostics_cache_["decoded_value"] = *last_diagnostics_.decoded_value;
    }

    diagnostics_cache_["decoded_pulses"] = nlohmann::json::array();
    if (last_message_) {
      diagnostics_cache_["active_frequency_indices"] =
          last_message_->active_frequency_indices;
      diagnostics_cache_["message_status"] = last_message_->status_message;
      for (const auto &pulse : last_message_->ordered_pulses) {
        diagnostics_cache_["decoded_pulses"].push_back({
            {"global_start_sample", pulse.pulse.timing.global_start_sample},
            {"duration_samples", pulse.pulse.timing.duration_samples},
            {"frequency_index", pulse.pulse.frequency.frequency_index},
            {"rf_frequency_hz", pulse.pulse.frequency.rf_frequency_hz},
            {"iq_offset_frequency_hz",
             pulse.pulse.frequency.iq_offset_frequency_hz},
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
    return graph::JsonView(diagnostics_cache_);
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
  mutable nlohmann::json diagnostics_cache_{};
};

} // namespace dsp::fhss
