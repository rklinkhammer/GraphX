#pragma once

#include "sar/SarMessages.hpp"

#include "config/Config.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
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

struct AzimuthTileSplitConfig {
    std::uint32_t tile_count{4};
    std::uint32_t tile_id_offset{0};
    int fixed_tile_id{-1};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class AzimuthTileSplitNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarAccelControlToken>,
          graph::TypeList<SarAccelControlToken>,
          AzimuthTileSplitNode>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    AzimuthTileSplitNode() = default;
    explicit AzimuthTileSplitNode(AzimuthTileSplitConfig config);

    std::optional<SarAccelControlToken> Transfer(
        const SarAccelControlToken& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{
                .name = "tile_count",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "4",
                .enum_values = std::nullopt,
                .description = "Number of azimuth tiles"
            },
            graph::JsonField{
                .name = "tile_id_offset",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Modulo offset applied to deterministic tile id selection"
            },
            graph::JsonField{
                .name = "fixed_tile_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = -1.0,
                .max = std::nullopt,
                .default_value = "-1",
                .enum_values = std::nullopt,
                .description = "Fixed tile id for graph-visible branch fan-out; -1 preserves sequence modulo behavior"
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

    void SetConfig(const AzimuthTileSplitConfig& config);
    const AzimuthTileSplitConfig& GetConfig() const noexcept;

private:
    std::uint32_t ResolveTileId(const SarAccelControlToken& input) const;
    SarAccelControlToken BuildDataTile(const SarAccelControlToken& input) const;
    SarAccelControlToken BuildEndOfStreamTile(const SarAccelControlToken& input) const;

    AzimuthTileSplitConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
