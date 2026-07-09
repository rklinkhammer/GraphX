// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "accelgraph/SpectrumAnalysis.hpp"
#include "accelgraph/SpectrumGraphTypes.hpp"
#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

namespace accelgraph {

class SineWaveSourceNode : public graph::NamedSourceNode<SineWaveSourceNode, IqFrameToken>,
                           public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 6> Fields() {
        return {
            graph::JsonField{.name = "sample_rate_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "48000.0",
                             .enum_values = std::nullopt, .description = "Sample rate in Hz"},
            graph::JsonField{.name = "tone_frequency_hz", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "1000.0",
                             .enum_values = std::nullopt, .description = "Deterministic tone frequency in Hz"},
            graph::JsonField{.name = "amplitude", .type = graph::JsonType::Number, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "1.0",
                             .enum_values = std::nullopt, .description = "Tone amplitude"},
            graph::JsonField{.name = "phase_radians", .type = graph::JsonType::Number, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "0.0",
                             .enum_values = std::nullopt, .description = "Initial phase in radians"},
            graph::JsonField{.name = "complex_sample_count", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "256",
                             .enum_values = std::nullopt, .description = "Complex IQ samples per emitted frame"},
            graph::JsonField{.name = "frame_count", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "1",
                             .enum_values = std::nullopt, .description = "Number of frames to emit"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<IqFrameToken> Produce(std::integral_constant<std::size_t, 0>) override;

private:
    double sample_rate_hz_{48000.0};
    double tone_frequency_hz_{1000.0};
    double amplitude_{1.0};
    double phase_radians_{0.0};
    std::size_t complex_sample_count_{256};
    std::size_t frame_count_{1};
    std::size_t produced_frames_{0};
};

class SpectrumAnalysisNode
    : public graph::NamedInteriorNode<
          graph::TypeList<IqFrameToken>,
          graph::TypeList<SpectrumFrameToken>,
          SpectrumAnalysisNode>,
      public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 4> Fields() {
        return {
            graph::JsonField{.name = "backend", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "auto",
                             .enum_values = std::nullopt, .description = "Requested backend: cpu, metal, cuda, auto"},
            graph::JsonField{.name = "fallback_policy", .type = graph::JsonType::String, .required = false,
                             .min = std::nullopt, .max = std::nullopt, .default_value = "strict",
                             .enum_values = std::nullopt, .description = "Fallback policy: strict or allow"},
            graph::JsonField{.name = "output_bins", .type = graph::JsonType::Integer, .required = false,
                             .min = 1.0, .max = std::nullopt, .default_value = "128",
                             .enum_values = std::nullopt, .description = "Number of output magnitude bins"},
            graph::JsonField{.name = "cuda_device_ordinal", .type = graph::JsonType::Integer, .required = false,
                             .min = 0.0, .max = std::nullopt, .default_value = "0",
                             .enum_values = std::nullopt, .description = "Requested CUDA device ordinal when backend=cuda"},
        };
    }

    void Configure(const graph::JsonView& cfg) override;

    std::optional<SpectrumFrameToken> Transfer(const IqFrameToken& iq,
                                               std::integral_constant<std::size_t, 0>,
                                               std::integral_constant<std::size_t, 0>) override;

private:
    std::unique_ptr<ISpectrumAnalysisOperation> operation_;
    SpectrumRequestedBackend requested_backend_{SpectrumRequestedBackend::Auto};
    SpectrumFallbackPolicy fallback_policy_{SpectrumFallbackPolicy::Strict};
    std::size_t output_bins_{128};
    int cuda_device_ordinal_{0};
    AcceleratorBackend execution_backend_{AcceleratorBackend::Cpu};
    bool fallback_used_{false};
    std::string fallback_reason_{"none"};
};

class SpectrumSinkNode : public graph::NamedSinkNode<SpectrumSinkNode, SpectrumFrameToken>,
                         public graph::IConfigurable {
public:
    static constexpr std::array<graph::JsonField, 0> Fields() {
        return {};
    }

    void Configure(const graph::JsonView& cfg) override;

    bool Consume(const SpectrumFrameToken& spectrum,
                 std::integral_constant<std::size_t, 0>) override;

    [[nodiscard]] const std::optional<SpectrumFrameToken>& LastSpectrum() const noexcept {
        return last_spectrum_;
    }

    [[nodiscard]] std::size_t CapturedCount() const noexcept {
        return captured_count_;
    }

private:
    std::optional<SpectrumFrameToken> last_spectrum_;
    std::size_t captured_count_{0};
};

}  // namespace accelgraph
