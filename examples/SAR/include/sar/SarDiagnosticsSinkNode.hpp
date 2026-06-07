#pragma once

#include "sar/SarMessages.hpp"

#include "graph/ICompletionCallback.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>

namespace sar {

class SarDiagnosticsSinkNode
    : public graph::NamedSinkNode<SarDiagnosticsSinkNode, SarMergeStatusMessage>,
      public graph::CompletionCallbackProvider {
public:
    SarDiagnosticsSinkNode() = default;

    bool Consume(const SarMergeStatusMessage& value,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::size_t consume_count() const noexcept {
        return consume_count_;
    }

    [[nodiscard]] const SarMergeStatusMessage& last_status() const noexcept {
        return last_status_;
    }

    [[nodiscard]] const SarDiagnosticsMessage& last_diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    void UpdateDiagnostics(const SarMergeStatusMessage& value);
    void SignalCompletion();

    std::size_t consume_count_{0};
    SarMergeStatusMessage last_status_{};
    SarDiagnosticsMessage diagnostics_{};
};

} // namespace sar
