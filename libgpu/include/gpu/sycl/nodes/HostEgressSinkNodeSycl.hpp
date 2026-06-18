/**
 * @file HostEgressSinkNodeSycl.hpp
 * @brief Host Egress Sink Node SYCL GPU acceleration support.
 *
 * @details Provides SYCL acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>

namespace graph::gpu::sycl::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class HostEgressSinkNodeSycl
 * @brief Host Egress Sink Node SYCL graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class HostEgressSinkNodeSycl
    : public graph::NamedSinkNode<HostEgressSinkNodeSycl, accel::HostPinnedBufferView>,
      public graph::CompletionCallbackProvider {
public:
    /**
     * @brief Executes the Host Egress Sink Node Sycl operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    HostEgressSinkNodeSycl() = default;

    bool Consume(const accel::HostPinnedBufferView& value,
                 std::integral_constant<std::size_t, 0>) override {
        if (!accel::IsValidView(value)) {
            return false;
        }

        last_view_ = value;
        ++consume_count_;

        if (expected_message_count_ > 0 && consume_count_ >= expected_message_count_) {
            /**
             * @brief Executes the Signal Completion operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            SignalCompletion();
        }

        return true;
    }

    /**
     * @brief Processes data through the Consume For Test operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param value Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool ConsumeForTest(const accel::HostPinnedBufferView& value) {
        return Consume(value, std::integral_constant<std::size_t, 0>{});
    }

    /**
     * @brief Executes the Last View operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] const accel::HostPinnedBufferView& LastView() const noexcept {
        return last_view_;
    }

    /**
     * @brief Processes data through the Consume Count operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::size_t ConsumeCount() const noexcept {
        return consume_count_;
    }

    /**
     * @brief Updates the Expected Message Count.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param count Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void SetExpectedMessageCount(std::size_t count) noexcept {
        expected_message_count_ = count;
    }

private:
    /**
     * @brief Executes the Signal Completion operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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

} // namespace graph::gpu::sycl::nodes
