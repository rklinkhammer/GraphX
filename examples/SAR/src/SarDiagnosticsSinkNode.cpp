#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

namespace sar {

namespace {

} // namespace

void SarDiagnosticsSinkNode::Configure(const graph::JsonView& cfg) {
    if (cfg.Contains("completion_signal_enabled")) {
        auto value = cfg.TryGetBool("completion_signal_enabled");
        if (!value) {
            throw value.error();
        }
        completion_signal_enabled_ = value.value();
    }
}

graph::JsonView SarDiagnosticsSinkNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["completion_signal_enabled"] = completion_signal_enabled_;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SarDiagnosticsSinkNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> SarDiagnosticsSinkNode::GetParameterNames() const {
    return {"completion_signal_enabled"};
}

bool SarDiagnosticsSinkNode::Consume(const SarAccelControlToken& value,
                                     std::integral_constant<std::size_t, 0>) {
    const auto stage_start = runtime::SteadyClock::now();
    last_token_ = value;
    ++consume_count_;
    UpdateDiagnostics(value);

    if (value.sidecar.marker == SarFrameMarker::EndOfStream && value.sidecar.merge_complete) {
        SignalCompletion();
    }

    diagnostics_.stage_timings.diagnostics_sink_time_us += runtime::ElapsedUs(stage_start);

    return true;
}

void SarDiagnosticsSinkNode::UpdateDiagnostics(const SarAccelControlToken& value) {
    diagnostics_.sidecar = value.sidecar;
    diagnostics_.stage_timings = value.sidecar.stage_timings;

    if (value.sidecar.marker == SarFrameMarker::Data) {
        const auto pulse_count = static_cast<std::uint64_t>(value.sidecar.sequence_id + 1u);
        if (pulse_count > diagnostics_.pulses_processed) {
            diagnostics_.pulses_processed = pulse_count;
        }
    } else if (value.sidecar.marker == SarFrameMarker::EndOfStream) {
        const auto pulse_count = static_cast<std::uint64_t>(value.sidecar.sequence_id);
        if (pulse_count > diagnostics_.pulses_processed) {
            diagnostics_.pulses_processed = pulse_count;
        }
    }

    diagnostics_.tiles_processed = static_cast<std::uint64_t>(value.sidecar.received_tiles);
    diagnostics_.bytes_h2d = value.sidecar.bytes_h2d;
    diagnostics_.bytes_d2h = value.sidecar.bytes_d2h;
    diagnostics_.kernel_dispatches = value.sidecar.kernel_dispatches;
    diagnostics_.transfer_h2d_time_us = value.sidecar.transfer_h2d_time_us;
    diagnostics_.kernel_exec_time_us = value.sidecar.kernel_exec_time_us;
    diagnostics_.transfer_d2h_time_us = value.sidecar.transfer_d2h_time_us;
    diagnostics_.fanin_wait_ms = value.sidecar.fanin_wait_ms;
    diagnostics_.e2e_latency_ms = value.sidecar.fanin_wait_ms;
    diagnostics_.duplicate_tile_count = static_cast<std::uint64_t>(value.sidecar.duplicate_tiles);
    diagnostics_.missing_tile_count = static_cast<std::uint64_t>(value.sidecar.missing_tiles);
    diagnostics_.out_of_order_completion_count =
        static_cast<std::uint64_t>(value.sidecar.out_of_order_tiles);
}

void SarDiagnosticsSinkNode::UpdateFromGraphMetrics(const graph::GraphMetrics& metrics) {
    diagnostics_.queue_backpressure_events =
        metrics.backpressure_events.load(std::memory_order_relaxed);
    diagnostics_.peak_queue_depth =
        metrics.peak_queue_depth.load(std::memory_order_relaxed);
}

void SarDiagnosticsSinkNode::SignalCompletion() {
    if (!completion_signal_enabled_) {
        return;
    }

    if (!HasCallbackProvider()) {
        return;
    }

    auto* provider = dynamic_cast<CompletionNodeCallback*>(GetCallbackProvider());
    if (provider != nullptr) {
        provider->OnComplete();
    }
}

} // namespace sar
