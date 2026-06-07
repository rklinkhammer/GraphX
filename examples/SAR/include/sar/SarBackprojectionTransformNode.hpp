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

struct SarBackprojectionTransformConfig {
    std::uint32_t image_width{16};
    std::uint32_t backend_id{0};
    std::uint32_t queue_id{0};
    std::uint32_t kernel_id{3301};
    SarBackendKind backend{SarBackendKind::SimulatedDevice};
};

class SarBackprojectionTransformNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarRangeTileMessage>,
          graph::TypeList<SarImageTileMessage>,
          SarBackprojectionTransformNode>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    SarBackprojectionTransformNode() = default;
    explicit SarBackprojectionTransformNode(SarBackprojectionTransformConfig config);

    std::optional<SarImageTileMessage> Transfer(
        const SarRangeTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{
                .name = "image_width",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "16",
                .enum_values = std::nullopt,
                .description = "Image tile width in pixels"
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
                .name = "queue_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "0",
                .enum_values = std::nullopt,
                .description = "Dispatch queue identifier"
            },
            graph::JsonField{
                .name = "kernel_id",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = std::nullopt,
                .default_value = "3301",
                .enum_values = std::nullopt,
                .description = "Kernel identifier"
            },
            graph::JsonField{
                .name = "backend",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 0.0,
                .max = 2.0,
                .default_value = "1",
                .enum_values = std::nullopt,
                .description = "Backend kind enum: 0=Host, 1=SimulatedDevice, 2=NativeDevice"
            }
        }};
    }

    void SetConfig(const SarBackprojectionTransformConfig& config);
    const SarBackprojectionTransformConfig& GetConfig() const noexcept;

private:
    SarImageTileMessage BuildDataTile(const SarRangeTileMessage& input) const;
    SarImageTileMessage BuildEndOfStreamTile(const SarRangeTileMessage& input) const;

    std::uint32_t ResolveImageWidth() const noexcept;

    SarBackprojectionTransformConfig config_{};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
