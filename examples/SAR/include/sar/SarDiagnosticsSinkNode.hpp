// SPDX-License-Identifier: MIT

/**
 * @file SarDiagnosticsSinkNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "graph/GraphMetrics.hpp"
#include "graph/ICompletionCallback.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace sar {

/**
 * @class SarDiagnosticsSinkNode
 * @brief SarDiagnosticsSinkNode class.
 */
class SarDiagnosticsSinkNode
    : public graph::NamedSinkNode<SarDiagnosticsSinkNode, SarControlToken>,
    public graph::CompletionCallbackProvider,
    public graph::IDiagnosable,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    SarDiagnosticsSinkNode() = default;

    bool Consume(const SarControlToken& value,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::size_t consume_count() const noexcept {
        return consume_count_;
    }

    [[nodiscard]] const SarControlToken& last_token() const noexcept {
        return last_token_;
    }

    [[nodiscard]] const SarDiagnosticsSnapshot& last_diagnostics() const noexcept {
        return diagnostics_;
    }

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;
    graph::JsonView GetDiagnostics() const override;

    static constexpr std::array<graph::JsonField, 1> Fields() {
        return {{
            graph::JsonField{
                .name = "completion_signal_enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "true",
                .enum_values = std::nullopt,
                .description = "Enable completion callback signaling on complete EOS"
            }
        }};
    }

    void UpdateFromGraphMetrics(const graph::GraphMetrics& metrics);

private:
    void UpdateDiagnostics(const SarControlToken& value);
    void SignalCompletion();

    std::size_t consume_count_{0};
    SarControlToken last_token_{};
    SarDiagnosticsSnapshot diagnostics_{};
    bool completion_signal_enabled_{true};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
    mutable nlohmann::json diagnostics_cache_{nlohmann::json::object()};
};

} // namespace sar
