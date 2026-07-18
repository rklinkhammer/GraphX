// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>

#include "accelgraph/Accelerator.hpp"
#include "accelgraph/SpectrumGraphTypes.hpp"
#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph {

class SineWaveSourceNode
    : public graph::NamedSourceNode<SineWaveSourceNode, DeterministicIqPacket>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 6> Fields() {
        return {
            graph::JsonField{.name = "sample_count", .type = graph::JsonType::Integer, .required = false,
                             .min = 2.0, .max = std::nullopt, .default_value = "256",
                             .enum_values = std::nullopt, .description = "Number of deterministic IQ samples"},
            graph::JsonField{.name = "sample_rate_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "48000.0",
                             .enum_values = std::nullopt, .description = "IQ sample rate in Hz"},
            graph::JsonField{.name = "tone_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "1000.0",
                             .enum_values = std::nullopt, .description = "Deterministic sine tone frequency in Hz"},
            graph::JsonField{.name = "amplitude", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "1.0",
                             .enum_values = std::nullopt, .description = "Signal amplitude"},
            graph::JsonField{.name = "phase_radians", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt, .description = "Initial signal phase in radians"},
            graph::JsonField{.name = "packet_number", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "1",
                             .enum_values = std::nullopt, .description = "Packet sequence number"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<DeterministicIqPacket> Produce(std::integral_constant<std::size_t, 0>) override;

private:
    DeterministicIqPacket configured_packet_{};
    bool produced_{false};
};

class SpectrumAnalysisNode
    : public graph::NamedInteriorNode<
          graph::TypeList<DeterministicIqPacket>,
          graph::TypeList<MagnitudeSpectrumPacket>,
          SpectrumAnalysisNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = true,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "cpu",
                             .enum_values = std::nullopt, .description = "Selected spectrum backend (cpu, metal, or cuda)"},
            graph::JsonField{.name = "strict_fallback", .type = graph::JsonType::Boolean, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "true",
                             .enum_values = std::nullopt, .description = "When true, reject fallback from requested backend to cpu"},
            graph::JsonField{.name = "fallback_policy", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "strict",
                             .enum_values = std::nullopt, .description = "Fallback policy (strict or allow)"},
            graph::JsonField{.name = "cuda_device_ordinal", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt, .description = "CUDA device ordinal for cuda backend selection"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<MagnitudeSpectrumPacket> Transfer(
        const DeterministicIqPacket& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::expected<MagnitudeSpectrumPacket, AcceleratorError>
    Execute(const DeterministicIqPacket& input) const;

private:
    AcceleratorBackend requested_backend_{AcceleratorBackend::Cpu};
    AcceleratorBackend selected_backend_{AcceleratorBackend::Cpu};
    bool strict_fallback_{true};
    bool used_fallback_{false};
    std::string fallback_diagnostic_;
    std::shared_ptr<IAcceleratorSession> metal_session_;
    std::shared_ptr<IAcceleratorSession> cuda_session_;
    int cuda_device_ordinal_{0};
};

class SpectrumSinkNode
    : public graph::NamedSinkNode<SpectrumSinkNode, MagnitudeSpectrumPacket>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const MagnitudeSpectrumPacket& spectrum,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] std::optional<MagnitudeSpectrumPacket> LastSpectrum() const;

    [[nodiscard]] std::size_t FrameCount() const;

private:
    mutable std::mutex mutex_;
    std::optional<MagnitudeSpectrumPacket> last_spectrum_;
    std::size_t frame_count_{0};
};

}  // namespace accelgraph
