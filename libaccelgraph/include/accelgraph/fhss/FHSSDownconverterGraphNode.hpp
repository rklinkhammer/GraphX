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
#include "dsp/fhss/FHSSDownconverterNode.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph::fhss {

inline constexpr const char* kFhssDownconverterMetalNativeNotImplementedDiagnostic =
    "AccelFhssDownconverterNode metal backend requested, but native metal kernel path is not implemented yet.";
inline constexpr const char* kFhssDownconverterCudaNativeNotImplementedDiagnostic =
    "AccelFhssDownconverterNode cuda backend requested, but native cuda kernel path is not implemented yet.";

class AccelFhssDownconverterNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSSyntheticIqToken>,
          graph::TypeList<FHSSDownconvertedIqToken>,
          AccelFhssDownconverterNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 14> Fields() {
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
            graph::JsonField{.name = "input_iq_center_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Input IQ center frequency in Hz"},
            graph::JsonField{.name = "input_reference_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Input reference frequency in Hz"},
            graph::JsonField{.name = "output_iq_center_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Output IQ center frequency in Hz"},
            graph::JsonField{.name = "output_reference_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Output reference frequency in Hz"},
            graph::JsonField{.name = "translation_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Frequency translation in Hz"},
            graph::JsonField{.name = "passthrough", .type = graph::JsonType::Boolean, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "true",
                             .enum_values = std::nullopt,
                             .description = "When true, pass input IQ without translation"},
            graph::JsonField{.name = "phase_convention", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt,
                             .default_value = "passthrough_no_phase_rotation",
                             .enum_values = std::nullopt,
                             .description = "Phase convention for translated output"},
            graph::JsonField{.name = "sample_rate_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 1.0, .max = std::nullopt,
                             .default_value = "500000000.0",
                             .enum_values = std::nullopt,
                             .description = "Downconverter sample rate in Hz"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<FHSSDownconvertedIqToken> Transfer(
        const FHSSSyntheticIqToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] AcceleratorBackend RequestedBackend() const noexcept;
    [[nodiscard]] AcceleratorBackend SelectedBackend() const noexcept;
    [[nodiscard]] bool UsedFallback() const noexcept;
    [[nodiscard]] const std::string& FallbackDiagnostic() const noexcept;
    [[nodiscard]] std::optional<FHSSDownconvertedIqToken> LastOutput() const;

private:
    FHSSAccelConfig accel_config_{};
    dsp::fhss::FHSSDownconverterConfig downconverter_config_{};
    AcceleratorBackend requested_backend_{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend_{AcceleratorBackend::Cpu};
    bool used_fallback_{false};
    std::string fallback_diagnostic_;
    std::shared_ptr<IAcceleratorSession> metal_session_;
    std::shared_ptr<IAcceleratorSession> cuda_session_;
    dsp::fhss::FHSSDownconverterNode cpu_reference_{};
    std::optional<FHSSDownconvertedIqToken> last_output_;
};

class AccelFhssDownconverterSinkNode
    : public graph::NamedSinkNode<AccelFhssDownconverterSinkNode, FHSSDownconvertedIqToken>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const FHSSDownconvertedIqToken& packet,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::optional<FHSSDownconvertedIqToken> LastPacket() const;

private:
    mutable std::mutex mutex_;
    std::optional<FHSSDownconvertedIqToken> last_packet_;
};

}  // namespace accelgraph::fhss
