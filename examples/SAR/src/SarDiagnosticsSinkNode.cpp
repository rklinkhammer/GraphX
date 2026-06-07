#include "sar/SarDiagnosticsSinkNode.hpp"

namespace sar {

bool SarDiagnosticsSinkNode::Consume(const SarMergeStatusMessage& value,
                                     std::integral_constant<std::size_t, 0>) {
    last_status_ = value;
    ++consume_count_;

    if (value.envelope.marker == SarFrameMarker::EndOfStream && value.complete) {
        SignalCompletion();
    }

    return true;
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
