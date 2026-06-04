// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>

namespace graph::gpu::metal::nodes {

class HostEgressSinkNodeMetal
    : public graph::NamedSinkNode<HostEgressSinkNodeMetal, accel::HostPinnedBufferView>,
      public graph::CompletionCallbackProvider {
public:
    HostEgressSinkNodeMetal() = default;

    bool Consume(const accel::HostPinnedBufferView& value,
                 std::integral_constant<std::size_t, 0>) override {
        if (!accel::IsValidView(value)) {
            return false;
        }

        last_view_ = value;
        ++consume_count_;

        if (expected_message_count_ > 0 && consume_count_ >= expected_message_count_) {
            SignalCompletion();
        }

        return true;
    }

    bool ConsumeForTest(const accel::HostPinnedBufferView& value) {
        return Consume(value, std::integral_constant<std::size_t, 0>{});
    }

    [[nodiscard]] const accel::HostPinnedBufferView& LastView() const noexcept {
        return last_view_;
    }

    [[nodiscard]] std::size_t ConsumeCount() const noexcept {
        return consume_count_;
    }

    void SetExpectedMessageCount(std::size_t count) noexcept {
        expected_message_count_ = count;
    }

private:
    void SignalCompletion() {
        if (this->HasCallbackProvider()) {
            auto provider = dynamic_cast<CompletionNodeCallback*>(this->GetCallbackProvider());
            if (provider != nullptr) {
                provider->OnComplete();
            }
        }
    }

    accel::HostPinnedBufferView last_view_{};
    std::size_t consume_count_{0};
    std::size_t expected_message_count_{1};
};

} // namespace graph::gpu::metal::nodes