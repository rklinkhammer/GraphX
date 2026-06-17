/**
 * @file HostEgressSinkNodeMetal.hpp
 * @brief GraphX source file.
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
 * @brief HostEgressSinkNodeMetal class.
 */
/**
 * @class HostEgressSinkNodeMetal
 * @brief Host egress sink node metal implementation for GraphX.
 */
class HostEgressSinkNodeMetal
    : public graph::NamedSinkNode<HostEgressSinkNodeMetal, accel::HostPinnedBufferView>,
    public graph::CompletionCallbackProvider,
    public graph::IConfigurable,
    public graph::IParameterized {
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

    [[nodiscard]] graph::JsonView GetParameters() const override {
        static thread_local nlohmann::json params;
        params = {
            {"expected_message_count", expected_message_count_},
        };
        return graph::JsonView(params);
    }

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

    [[nodiscard]] std::vector<std::string> GetParameterNames() const override {
        return {"expected_message_count"};
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
