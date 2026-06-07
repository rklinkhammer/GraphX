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

struct RangeWindowConfig {
    bool enabled{true};
    float gain{1.0f};
};

class RangeWindowNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarPulseBlockMessage>,
          graph::TypeList<SarPulseBlockMessage>,
          RangeWindowNode>,
      public graph::IConfigurable,
      public graph::IParameterized {
public:
    RangeWindowNode() = default;
    explicit RangeWindowNode(RangeWindowConfig config);

    std::optional<SarPulseBlockMessage> Transfer(
        const SarPulseBlockMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 2> Fields() {
        return {{
            graph::JsonField{
                .name = "enabled",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "true",
                .enum_values = std::nullopt,
                .description = "Apply deterministic Hann range window to IQ samples"
            },
            graph::JsonField{
                .name = "gain",
                .type = graph::JsonType::Number,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "1.0",
                .enum_values = std::nullopt,
                .description = "Linear gain applied after windowing"
            }
        }};
    }

    void SetConfig(const RangeWindowConfig& config);
    const RangeWindowConfig& GetConfig() const noexcept;

private:
    static float HannWeight(std::size_t index, std::size_t sample_count);

    RangeWindowConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
