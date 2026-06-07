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
#include <unordered_set>
#include <vector>

namespace sar {

struct ImageTileMergeConfig {
    std::uint32_t expected_tiles{4};
    bool require_watermark_before_complete{false};
    bool require_all_tile_eos_before_complete{false};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class ImageTileMergeNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarImageTileMessage>,
          graph::TypeList<SarMergeStatusMessage>,
          ImageTileMergeNode>,
    public graph::IConfigurable,
    public graph::IParameterized {
public:
    ImageTileMergeNode() = default;
    explicit ImageTileMergeNode(ImageTileMergeConfig config);

    std::optional<SarMergeStatusMessage> Transfer(
        const SarImageTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void Configure(const graph::JsonView& cfg) override;
    graph::JsonView GetParameters() const override;
    graph::JsonView GetParameterDescription(const std::string& param_name) const override;
    std::vector<std::string> GetParameterNames() const override;

    static constexpr std::array<graph::JsonField, 5> Fields() {
        return {{
            graph::JsonField{
                .name = "expected_tiles",
                .type = graph::JsonType::Integer,
                .required = false,
                .min = 1.0,
                .max = std::nullopt,
                .default_value = "4",
                .enum_values = std::nullopt,
                .description = "Expected number of image tiles before completion"
            },
            graph::JsonField{
                .name = "require_watermark_before_complete",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Require watermark marker before end-of-stream completion"
            },
            graph::JsonField{
                .name = "require_all_tile_eos_before_complete",
                .type = graph::JsonType::Boolean,
                .required = false,
                .min = std::nullopt,
                .max = std::nullopt,
                .default_value = "false",
                .enum_values = std::nullopt,
                .description = "Require end-of-stream markers from all tile branches before completion"
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
    void SetConfig(const ImageTileMergeConfig& config);
    const ImageTileMergeConfig& GetConfig() const noexcept;

private:
    SarMergeStatusMessage BuildStatusMessage(
        const SarImageTileMessage& input,
        SarFrameMarker marker,
        bool complete) const;

    std::uint32_t ResolveExpectedTiles() const noexcept;

    ImageTileMergeConfig config_{};

    std::unordered_set<std::uint32_t> seen_tiles_{};
    std::unordered_set<std::uint32_t> eos_tiles_{};
    std::uint32_t received_tiles_{0};
    std::uint32_t duplicate_tiles_{0};
    std::uint32_t out_of_order_tiles_{0};
    std::uint64_t bytes_h2d_{0};
    std::uint64_t bytes_d2h_{0};
    std::uint64_t kernel_dispatches_{0};
    std::uint64_t transfer_h2d_time_us_{0};
    std::uint64_t kernel_exec_time_us_{0};
    std::uint64_t transfer_d2h_time_us_{0};
    std::uint32_t last_tile_id_{0};
    bool has_last_tile_{false};
    bool watermark_seen_{false};
    bool completion_emitted_{false};
    std::uint64_t first_data_sequence_{0};
    bool has_first_data_sequence_{false};
    mutable nlohmann::json parameters_cache_{nlohmann::json::object()};
    mutable nlohmann::json parameter_description_cache_{nlohmann::json::object()};
};

} // namespace sar
