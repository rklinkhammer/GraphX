#include "sar/SarDiagnosticsSinkNode.hpp"

namespace sar {

bool SarDiagnosticsSinkNode::Consume(const SarMergeStatusMessage& value,
                                     std::integral_constant<std::size_t, 0>) {
    last_status_ = value;
    ++consume_count_;
    UpdateDiagnostics(value);

    if (value.envelope.marker == SarFrameMarker::EndOfStream && value.complete) {
        SignalCompletion();
    }

    return true;
}

void SarDiagnosticsSinkNode::UpdateDiagnostics(const SarMergeStatusMessage& value) {
    diagnostics_.envelope = value.envelope;

    if (value.envelope.marker == SarFrameMarker::Data) {
        const auto pulse_count = static_cast<std::uint64_t>(value.envelope.sequence_id + 1u);
        if (pulse_count > diagnostics_.pulses_processed) {
            diagnostics_.pulses_processed = pulse_count;
        }
    } else if (value.envelope.marker == SarFrameMarker::EndOfStream) {
        const auto pulse_count = static_cast<std::uint64_t>(value.envelope.sequence_id);
        if (pulse_count > diagnostics_.pulses_processed) {
            diagnostics_.pulses_processed = pulse_count;
        }
    }

    diagnostics_.tiles_processed = static_cast<std::uint64_t>(value.received_tiles);
    diagnostics_.bytes_h2d = value.bytes_h2d;
    diagnostics_.bytes_d2h = value.bytes_d2h;
    diagnostics_.kernel_dispatches = value.kernel_dispatches;
    diagnostics_.fanin_wait_ms = value.fanin_wait_ms;
    diagnostics_.e2e_latency_ms = value.fanin_wait_ms;
    diagnostics_.duplicate_tile_count = static_cast<std::uint64_t>(value.duplicate_tiles);
    diagnostics_.missing_tile_count = static_cast<std::uint64_t>(value.missing_tiles);
}

void SarDiagnosticsSinkNode::UpdateFromGraphMetrics(const graph::GraphMetrics& metrics) {
    diagnostics_.queue_backpressure_events =
        metrics.backpressure_events.load(std::memory_order_relaxed);
    diagnostics_.peak_queue_depth =
        metrics.peak_queue_depth.load(std::memory_order_relaxed);
}

void SarDiagnosticsSinkNode::SignalCompletion() {
    if (!HasCallbackProvider()) {
        return;
    }

    auto* provider = dynamic_cast<CompletionNodeCallback*>(GetCallbackProvider());
    if (provider != nullptr) {
        provider->OnComplete();
    }
}

} // namespace sar
