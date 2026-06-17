// SPDX-License-Identifier: MIT

/**
 * @file SyntheticApertureIqSourceNode.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct SyntheticApertureIqSourceConfig {
    std::uint32_t stream_id{0};
    std::uint32_t total_pulses{32};
    std::uint32_t samples_per_pulse{256};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
    bool moving_target_enabled{false};
    float target_initial_range_m{2000.0f};
    float target_closing_velocity_mps{250.0f};
    float pulse_interval_s{0.001f};
    float target_reflectivity{1.0f};
};

/**
 * @class SyntheticApertureIqSourceNode
 * @brief SyntheticApertureIqSourceNode class.
 */
class SyntheticApertureIqSourceNode
    : public graph::NamedSourceNode<SyntheticApertureIqSourceNode, SarAccelControlToken>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    SyntheticApertureIqSourceNode() = default;
    explicit SyntheticApertureIqSourceNode(SyntheticApertureIqSourceConfig config);

    std::optional<SarAccelControlToken> Produce(
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 10> Fields() {
        return {{
            graph::JsonField{
                .name = "stream_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "SAR stream identifier"
            },
            graph::JsonField{
                .name = "total_pulses",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "32",
                .enum_values = std::nullopt,
                .description = "Number of pulses emitted before EOS"
            },
            graph::JsonField{
                .name = "samples_per_pulse",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "256",
                .enum_values = std::nullopt,
                .description = "Number of IQ samples per pulse"
            },
            graph::JsonField{
                .name = "backend_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend device index"
            },
            graph::JsonField{
                .name = "backend",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = 2.0,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend kind enum: 0=Host, 1=SimulatedDevice, 2=NativeDevice"
            },
            graph::JsonField{
                .name = "moving_target_enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Enable deterministic moving-target synthesis profile"
            },
            graph::JsonField{
                .name = "target_initial_range_m",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "2000.0",
                .enum_values = std::nullopt,
                .description = "Initial target range in meters"
            },
            graph::JsonField{
                .name = "target_closing_velocity_mps",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "250.0",
                .enum_values = std::nullopt,
                .description = "Target closing velocity in meters per second"
            },
            graph::JsonField{
                .name = "pulse_interval_s",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.000001,
                .max = std::nullopt,
                .default_value = "0.001",
                .enum_values = std::nullopt,
                .description = "Pulse interval in seconds"
            },
            graph::JsonField{
                .name = "target_reflectivity",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "1.0",
                .enum_values = std::nullopt,
                .description = "Deterministic target reflectivity scale"
            }
        }};
    }

    void Reset();
    void SetConfig(const SyntheticApertureIqSourceConfig& config);
    const SyntheticApertureIqSourceConfig& GetConfig() const noexcept;

private:
    SarAccelControlToken MakeDataToken() const;
    SarAccelControlToken MakeEndOfStreamToken() const;

    SyntheticApertureIqSourceConfig config_{};
    std::uint64_t next_sequence_id_{0};
    bool eos_emitted_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
