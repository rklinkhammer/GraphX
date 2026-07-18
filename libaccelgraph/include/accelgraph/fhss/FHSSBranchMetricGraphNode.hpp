// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "accelgraph/Accelerator.hpp"
#include "accelgraph/fhss/FHSSAccelConfig.hpp"
#include "accelgraph/fhss/FHSSAccelTypes.hpp"
#include "config/Config.hpp"
#include "dsp/fhss/FHSSCpsmDecoder.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph::fhss {

inline constexpr const char* kFhssBranchMetricMetalNativeNotImplementedDiagnostic =
    "AccelFhssBranchMetricNode metal backend requested, but native metal kernel path is not implemented yet.";
inline constexpr const char* kFhssBranchMetricCudaNativeNotImplementedDiagnostic =
    "AccelFhssBranchMetricNode cuda backend requested, but native cuda kernel path is not implemented yet.";

class AccelFhssBranchMetricNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSPerChannelPulseEvidenceToken>,
          graph::TypeList<FHSSCpsmBranchMetricToken>,
          AccelFhssBranchMetricNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 11> Fields() {
        return {
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt,
                             .description = "Requested accelerator backend (cpu, metal, cuda)"},
            graph::JsonField{.name = "fallback_policy", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "strict",
                             .enum_values = std::nullopt,
                             .description = "Fallback policy (strict or allow)"},
            graph::JsonField{.name = "strict_fallback", .type = graph::JsonType::Boolean, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "true",
                             .enum_values = std::nullopt,
                             .description = "When true, disallow fallback to cpu"},
            graph::JsonField{.name = "provider_id", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu.default",
                             .enum_values = std::nullopt,
                             .description = "Accelerator provider id; defaults from backend"},
            graph::JsonField{.name = "session_key", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "graph.default",
                             .enum_values = std::nullopt,
                             .description = "Accelerator session key"},
            graph::JsonField{.name = "cuda_device_ordinal", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "CUDA device ordinal for cuda backend"},
            graph::JsonField{.name = "symbol_count", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = 32.0, .default_value = "32",
                             .enum_values = std::nullopt,
                             .description = "CPSM symbol count per pulse"},
            graph::JsonField{.name = "modulation_index", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.5",
                             .enum_values = std::nullopt,
                             .description = "CPSM modulation index"},
            graph::JsonField{.name = "initial_phase_state", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = 3.0, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "Initial CPSM phase state"},
            graph::JsonField{.name = "check_terminal_phase", .type = graph::JsonType::Boolean, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "false",
                             .enum_values = std::nullopt,
                             .description = "When true, enforce expected terminal phase"},
            graph::JsonField{.name = "expected_terminal_phase_state", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = 3.0, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "Expected terminal phase state when check_terminal_phase=true"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<FHSSCpsmBranchMetricToken> Transfer(
        const FHSSPerChannelPulseEvidenceToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] AcceleratorBackend RequestedBackend() const noexcept;
    [[nodiscard]] AcceleratorBackend SelectedBackend() const noexcept;
    [[nodiscard]] bool UsedFallback() const noexcept;
    [[nodiscard]] const std::string& FallbackDiagnostic() const noexcept;

private:
    bool StageInputThroughCuda(const FHSSPerChannelPulseEvidenceToken& input);

    FHSSAccelConfig accel_config_{};
    dsp::fhss::CPSMDecoderConfig cpsm_config_{};
    AcceleratorBackend requested_backend_{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend_{AcceleratorBackend::Cpu};
    bool used_fallback_{false};
    std::string fallback_diagnostic_;
    std::shared_ptr<IAcceleratorSession> metal_session_;
    std::shared_ptr<IAcceleratorSession> cuda_session_;
};

class AccelFhssBranchMetricSinkNode
    : public graph::NamedSinkNode<AccelFhssBranchMetricSinkNode, FHSSCpsmBranchMetricToken>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const FHSSCpsmBranchMetricToken& packet,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::optional<FHSSCpsmBranchMetricToken> LastPacket() const;

private:
    mutable std::mutex mutex_;
    std::optional<FHSSCpsmBranchMetricToken> last_packet_;
};

}  // namespace accelgraph::fhss