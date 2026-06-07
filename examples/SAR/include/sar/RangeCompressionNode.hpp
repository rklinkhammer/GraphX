#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NamedNodes.hpp"

#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace sar {

struct RangeCompressionConfig {
    bool enabled{true};
    float gain{1.0f};
    double sample_rate_hz{48000.0};
};

class RangeCompressionNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarPulseBlockMessage>,
          graph::TypeList<SarPulseBlockMessage>,
          RangeCompressionNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    RangeCompressionNode() = default;
    explicit RangeCompressionNode(RangeCompressionConfig config);

    std::optional<SarPulseBlockMessage> Transfer(
        const SarPulseBlockMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 3> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "true",
                .enum_values = std::nullopt,
                .description = "Apply FFT-backed range compression to pulse IQ data"
            },
            graph::JsonField{
                .name = "gain",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "1.0",
                .enum_values = std::nullopt,
                .description = "Linear gain applied to compressed magnitude bins"
            },
            graph::JsonField{
                .name = "sample_rate_hz",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "48000.0",
                .enum_values = std::nullopt,
                .description = "Sample rate used for FFT frequency axis metadata"
            }
        }};
    }

    void SetConfig(const RangeCompressionConfig& config);
    const RangeCompressionConfig& GetConfig() const noexcept;

private:
    SarPulseBlockMessage CompressWithFft(const SarPulseBlockMessage& input) const;

    RangeCompressionConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
