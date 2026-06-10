#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>

namespace sar {

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t ElapsedUs(const Clock::time_point start) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - start);
    const auto count = static_cast<std::uint64_t>(elapsed.count());
    return (count == 0u) ? 1u : count;
}

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
    const auto stage_start = Clock::now();
    last_status_ = ProjectStatus(value);
    ++consume_count_;
    UpdateDiagnostics(value);

    if (value.sidecar.marker == SarFrameMarker::EndOfStream && value.sidecar.merge_complete) {
        SignalCompletion();
    }

    diagnostics_.stage_timings.diagnostics_sink_time_us += ElapsedUs(stage_start);

    return true;
}

void SarDiagnosticsSinkNode::UpdateDiagnostics(const SarAccelControlToken& value) {
    diagnostics_.envelope.sequence_id = value.sidecar.sequence_id;
    diagnostics_.envelope.batch_id = value.sidecar.batch_id;
    diagnostics_.envelope.aperture_id = value.sidecar.aperture_id;
    diagnostics_.envelope.pulse_range_start = value.sidecar.pulse_range_start;
    diagnostics_.envelope.pulse_range_count = value.sidecar.pulse_range_count;
    diagnostics_.envelope.stream_id = value.sidecar.stream_id;
    diagnostics_.envelope.tile_id = value.sidecar.tile_id;
    diagnostics_.envelope.tile_count = value.sidecar.tile_count;
    diagnostics_.envelope.backend_id = value.sidecar.backend_id;
    diagnostics_.envelope.backend = value.sidecar.backend;
    diagnostics_.envelope.marker = value.sidecar.marker;
    diagnostics_.envelope.synthetic = value.sidecar.synthetic;
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

SarMergeStatusMessage SarDiagnosticsSinkNode::ProjectStatus(const SarAccelControlToken& value) {
    SarMergeStatusMessage status{};
    status.envelope.sequence_id = value.sidecar.sequence_id;
    status.envelope.batch_id = value.sidecar.batch_id;
    status.envelope.aperture_id = value.sidecar.aperture_id;
    status.envelope.pulse_range_start = value.sidecar.pulse_range_start;
    status.envelope.pulse_range_count = value.sidecar.pulse_range_count;
    status.envelope.stream_id = value.sidecar.stream_id;
    status.envelope.tile_id = value.sidecar.tile_id;
    status.envelope.tile_count = value.sidecar.tile_count;
    status.envelope.backend_id = value.sidecar.backend_id;
    status.envelope.backend = value.sidecar.backend;
    status.envelope.marker = value.sidecar.marker;
    status.envelope.synthetic = value.sidecar.synthetic;

    status.gpu.host_view = value.host_view;
    status.gpu.device_view = value.device_view;
    status.gpu.lease = value.lease;
    status.gpu.transfer_ticket = value.transfer_ticket;
    status.gpu.kernel_ticket = value.kernel_ticket;
    status.gpu.has_host_view = value.has_host_view;
    status.gpu.has_device_view = value.has_device_view;
    status.gpu.has_lease = value.has_lease;
    status.gpu.has_transfer_ticket = value.has_transfer_ticket;
    status.gpu.has_kernel_ticket = value.has_kernel_ticket;

    status.stage_timings = value.sidecar.stage_timings;
    status.expected_tiles = value.sidecar.expected_tiles;
    status.received_tiles = value.sidecar.received_tiles;
    status.duplicate_tiles = value.sidecar.duplicate_tiles;
    status.missing_tiles = value.sidecar.missing_tiles;
    status.out_of_order_tiles = value.sidecar.out_of_order_tiles;
    status.bytes_h2d = value.sidecar.bytes_h2d;
    status.bytes_d2h = value.sidecar.bytes_d2h;
    status.kernel_dispatches = value.sidecar.kernel_dispatches;
    status.transfer_h2d_time_us = value.sidecar.transfer_h2d_time_us;
    status.kernel_exec_time_us = value.sidecar.kernel_exec_time_us;
    status.transfer_d2h_time_us = value.sidecar.transfer_d2h_time_us;
    status.watermark_seen = value.sidecar.watermark_seen;
    status.fanin_wait_ms = value.sidecar.fanin_wait_ms;
    status.complete = value.sidecar.merge_complete;
    return status;
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
