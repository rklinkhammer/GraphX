// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "accelgraph/Accelerator.hpp"
#include "accelgraph/fhss/FHSSAccelConfig.hpp"
#include "accelgraph/fhss/FHSSAccelTypes.hpp"
#include "config/Config.hpp"
#include "dsp/fhss/FHSSFixtureFrequencyChannelizerNode.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/TypedFixedFanNode.hpp"

namespace accelgraph::fhss {

inline constexpr const char* kFhssChannelizerMetalNativeNotImplementedDiagnostic =
    "AccelFhssChannelizerNode metal backend requested, but native metal kernel path is not implemented yet.";
inline constexpr const char* kFhssChannelizerCudaNativeNotImplementedDiagnostic =
    "AccelFhssChannelizerNode cuda backend requested, but native cuda kernel path is not implemented yet.";

using FHSSChannelizerOutputList =
    graph::RepeatType_t<FHSSChannelizedIqToken, dsp::fhss::FHSSProtocolConstants::kFrequencyCount>;

class AccelFhssChannelizerNode
    : public graph::TypedFixedFanNode<AccelFhssChannelizerNode,
                                      graph::TypeList<FHSSDownconvertedIqToken>,
                                      FHSSChannelizerOutputList>,
      public graph::IConfigurable {
public:
    using Base = graph::TypedFixedFanNode<AccelFhssChannelizerNode,
                                          graph::TypeList<FHSSDownconvertedIqToken>,
                                          FHSSChannelizerOutputList>;
    using InputTokenType = FHSSDownconvertedIqToken;
    using OutputTokenType = FHSSChannelizedIqToken;

    static constexpr std::size_t kOutputPortCount = dsp::fhss::FHSSProtocolConstants::kFrequencyCount;

    static constexpr std::array<graph::JsonField, 16> Fields() {
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
            graph::JsonField{.name = "iq_center_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt,
                             .description = "Receiver IQ center frequency used to derive per-channel offsets"},
            graph::JsonField{.name = "receiver_frequency_indices", .type = graph::JsonType::Array, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = std::nullopt,
                             .enum_values = std::nullopt,
                             .description = "Receiver frequency indices; defaults to all 64 FHSS channels"},
            graph::JsonField{.name = "channel_ids", .type = graph::JsonType::Array, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = std::nullopt,
                             .enum_values = std::nullopt,
                             .description = "Channel IDs corresponding one-to-one with receiver frequencies"},
            graph::JsonField{.name = "transmitted_active_frequency_indices", .type = graph::JsonType::Array, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = std::nullopt,
                             .enum_values = std::nullopt,
                             .description = "Active transmitter frequency indices"},
            graph::JsonField{.name = "transmitted_pulse_frequency_indices", .type = graph::JsonType::Array, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = std::nullopt,
                             .enum_values = std::nullopt,
                             .description = "Pulse-frequency indices used in fixture generation"},
            graph::JsonField{.name = "sample_rate_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "500000000.0",
                             .enum_values = std::nullopt,
                             .description = "Input sample rate in Hz"},
            graph::JsonField{.name = "channel_sample_rate_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "250000000.0",
                             .enum_values = std::nullopt,
                             .description = "Output per-channel sample rate in Hz"},
            graph::JsonField{.name = "decimation_factor", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "2",
                             .enum_values = std::nullopt,
                             .description = "Fixture channelizer decimation factor"},
            graph::JsonField{.name = "filter_group_delay_input_samples", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt,
                             .description = "Fixture filter group delay in input samples"},
            graph::JsonField{.name = "iq_capture", .type = graph::JsonType::Object, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = std::nullopt,
                             .enum_values = std::nullopt,
                             .description = "Optional fixture IQ capture settings"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    template <std::size_t Port>
    bool ConsumeInput(const typename Base::template InputType<Port>& input) {
        static_assert(Port == 0);
        if (!cpu_reference_.template ConsumeInput<0>(input)) {
            return false;
        }
        return ForwardAllOutputs<0>();
    }

    template <std::size_t OutputPort>
    std::optional<OutputTokenType> ProduceOutput() {
        static_assert(OutputPort < kOutputPortCount);
        return Base::template DequeueOutput<OutputPort>();
    }

    [[nodiscard]] AcceleratorBackend RequestedBackend() const noexcept;
    [[nodiscard]] AcceleratorBackend SelectedBackend() const noexcept;
    [[nodiscard]] bool UsedFallback() const noexcept;
    [[nodiscard]] const std::string& FallbackDiagnostic() const noexcept;

private:
    template <std::size_t Port>
    bool ForwardAllOutputs() {
        if constexpr (Port < kOutputPortCount) {
            auto output = cpu_reference_.template ProduceOutput<Port>();
            if (!output.has_value()) {
                return false;
            }
            if (!Base::template EnqueueOutput<Port>(*output)) {
                return false;
            }
            return ForwardAllOutputs<Port + 1>();
        }
        return true;
    }

    FHSSAccelConfig accel_config_{};
    dsp::fhss::FHSSChannelizerConfig channelizer_config_{};
    AcceleratorBackend requested_backend_{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend_{AcceleratorBackend::Cpu};
    bool used_fallback_{false};
    std::string fallback_diagnostic_;
    std::shared_ptr<IAcceleratorSession> metal_session_;
    std::shared_ptr<IAcceleratorSession> cuda_session_;
    dsp::fhss::FHSSFixtureFrequencyChannelizerNode cpu_reference_{};
};

class AccelFhssChannelizerSinkNode
    : public graph::NamedSinkNode<AccelFhssChannelizerSinkNode, FHSSChannelizedIqToken>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const FHSSChannelizedIqToken& packet,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::optional<FHSSChannelizedIqToken> LastPacket() const;

private:
    mutable std::mutex mutex_;
    std::optional<FHSSChannelizedIqToken> last_packet_;
};

}  // namespace accelgraph::fhss
