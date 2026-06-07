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

struct H2DAsyncConfig {
    bool override_backend{false};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class H2DAsyncNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarRangeTileMessage>,
          graph::TypeList<SarRangeTileMessage>,
          H2DAsyncNode>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    H2DAsyncNode() = default;

    std::optional<SarRangeTileMessage> Transfer(
        const SarRangeTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 3> Fields() {
        return {{
            graph::JsonField{
                .name = "override_backend",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Override backend metadata on outgoing messages"
            },
            graph::JsonField{
                .name = "backend_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend device index for override mode"
            },
            graph::JsonField{
                .name = "backend",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = 2.0,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Backend kind enum for override mode"
            }
        }};
    }

    void SetConfig(const H2DAsyncConfig& config);
    const H2DAsyncConfig& GetConfig() const noexcept;

private:
    H2DAsyncConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
