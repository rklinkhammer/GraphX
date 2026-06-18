/**
 * @file HostEgressSinkNodeMetal.hpp
 * @brief Host Egress Sink Node Metal GPU acceleration support.
 *
 * @details Provides Metal acceleration boundary and graph-node support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "gpu/accel/types/AccelValidation.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace graph::gpu::metal::nodes {

// Control-plane contract: edges carry readiness/context handles only.
// Backend capabilities perform allocation/copy/synchronization work.
// This node exposes an operation boundary over those backend services.

/**
 * @class HostEgressSinkNodeMetal
 * @brief Host Egress Sink Node Metal graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class HostEgressSinkNodeMetal
    : public graph::NamedSinkNode<HostEgressSinkNodeMetal, accel::HostPinnedBufferView>,
    public graph::CompletionCallbackProvider,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    /**
     * @brief Executes the Host Egress Sink Node Metal operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    HostEgressSinkNodeMetal() = default;

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

    /**
     * @brief Applies configuration to this object.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param cfg Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void Configure(const graph::JsonView& cfg) override {
        if (cfg.Contains("expected_message_count")) {
            auto expected_count = cfg.TryGetInt("expected_message_count");
            if (!expected_count) {
                throw expected_count.error();
            }
            if (expected_count.value() < 0) {
                throw std::invalid_argument("expected_message_count must be >= 0");
            }
            expected_message_count_ = static_cast<std::size_t>(expected_count.value());
        }
    }

    /**
     * @brief Returns the Parameters.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"expected_message_count", expected_message_count_},
        };
        return graph::JsonView(params);
    }

    /**
     * @brief Returns the Parameter Description.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param param_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] graph::JsonView GetParameterDescription(const std::string& param_name) const override {
        static thread_local nlohmann::json desc;
        if (param_name == "expected_message_count") {
            desc = {
                {"type", "integer"},
                {"required", false},
                {"description", "Signals completion after consuming this many messages."},
            };
        } else {
            desc = nlohmann::json::object();
        }
        return graph::JsonView(desc);
    }

    /**
     * @brief Returns the Parameter Names.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"expected_message_count"};
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

} // namespace graph::gpu::metal::nodes
