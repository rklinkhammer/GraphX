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
};

class SyntheticApertureIqSourceNode
    : public graph::NamedSourceNode<SyntheticApertureIqSourceNode, SarPulseBlockMessage>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    SyntheticApertureIqSourceNode() = default;
    explicit SyntheticApertureIqSourceNode(SyntheticApertureIqSourceConfig config);

    std::optional<SarPulseBlockMessage> Produce(
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
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
            }
        }};
    }

    void Reset();
    void SetConfig(const SyntheticApertureIqSourceConfig& config);
    const SyntheticApertureIqSourceConfig& GetConfig() const noexcept;

private:
    static SarIqSample MakeSample(std::uint64_t sequence_id, std::uint32_t sample_index);

    SarPulseBlockMessage MakeDataMessage() const;
    SarPulseBlockMessage MakeEndOfStreamMessage() const;

    SyntheticApertureIqSourceConfig config_{};
    std::uint64_t next_sequence_id_{0};
    bool eos_emitted_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
