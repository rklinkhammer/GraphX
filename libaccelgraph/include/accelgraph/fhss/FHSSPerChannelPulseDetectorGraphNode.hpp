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
#include "dsp/fhss/PerChannelPulseDetectorNode.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph::fhss {

inline constexpr const char* kFhssPerChannelPulseDetectorMetalNativeNotImplementedDiagnostic =
    "AccelFhssPerChannelPulseDetectorNode metal backend requested, but native metal kernel path is not implemented yet.";
inline constexpr const char* kFhssPerChannelPulseDetectorCudaNativeNotImplementedDiagnostic =
    "AccelFhssPerChannelPulseDetectorNode cuda backend requested, but native cuda kernel path is not implemented yet.";

class AccelFhssPerChannelPulseDetectorNode
    : public graph::NamedInteriorNode<
          graph::TypeList<FHSSChannelizedIqToken>,
          graph::TypeList<FHSSPerChannelPulseEvidenceToken>,
          AccelFhssPerChannelPulseDetectorNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 13> Fields() {
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
            graph::JsonField{.name = "detector_id", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "Per-channel detector id for diagnostics"},
            graph::JsonField{.name = "packet_sequence", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "Packet sequence id propagated to detected pulses"},
            graph::JsonField{.name = "min_power_linear", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "1.0e-12",
                             .enum_values = std::nullopt,
                             .description = "Minimum power threshold in linear units"},
            graph::JsonField{.name = "min_symbol_coherence", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = 1.0, .default_value = "0.5",
                             .enum_values = std::nullopt,
                             .description = "Minimum symbol coherence threshold"},
            graph::JsonField{.name = "noise_floor_db", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "-120.0",
                             .enum_values = std::nullopt,
                             .description = "Noise floor estimate in dB"},
            graph::JsonField{.name = "nominal_bandwidth_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "5000000.0",
                             .enum_values = std::nullopt,
                             .description = "Nominal pulse bandwidth in Hz"},
            graph::JsonField{.name = "max_pulse_input_samples", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = std::nullopt,
                             .default_value = "2048",
                             .enum_values = std::nullopt,
                             .description = "Max input samples used per pulse detection window"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<FHSSPerChannelPulseEvidenceToken> Transfer(
        const FHSSChannelizedIqToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] AcceleratorBackend RequestedBackend() const noexcept;
    [[nodiscard]] AcceleratorBackend SelectedBackend() const noexcept;
    [[nodiscard]] bool UsedFallback() const noexcept;
    [[nodiscard]] const std::string& FallbackDiagnostic() const noexcept;

private:
    FHSSAccelConfig accel_config_{};
    dsp::fhss::PerChannelPulseDetectorConfig detector_config_{};
    AcceleratorBackend requested_backend_{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend_{AcceleratorBackend::Cpu};
    bool used_fallback_{false};
    std::string fallback_diagnostic_;
    std::shared_ptr<IAcceleratorSession> metal_session_;
    std::shared_ptr<IAcceleratorSession> cuda_session_;
    dsp::fhss::PerChannelPulseDetectorNode cpu_reference_{};
};

class AccelFhssPerChannelPulseDetectorSinkNode
    : public graph::NamedSinkNode<AccelFhssPerChannelPulseDetectorSinkNode, FHSSPerChannelPulseEvidenceToken>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const FHSSPerChannelPulseEvidenceToken& packet,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::optional<FHSSPerChannelPulseEvidenceToken> LastPacket() const;

private:
    mutable std::mutex mutex_;
    std::optional<FHSSPerChannelPulseEvidenceToken> last_packet_;
};

}  // namespace accelgraph::fhss
