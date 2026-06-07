#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>

namespace sar {

struct ImageTileMergeConfig {
    std::uint32_t expected_tiles{4};
    bool require_watermark_before_complete{false};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class ImageTileMergeNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarImageTileMessage>,
          graph::TypeList<SarMergeStatusMessage>,
          ImageTileMergeNode> {
public:
    ImageTileMergeNode() = default;
    explicit ImageTileMergeNode(ImageTileMergeConfig config);

    std::optional<SarMergeStatusMessage> Transfer(
        const SarImageTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

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
    std::uint32_t received_tiles_{0};
    std::uint32_t duplicate_tiles_{0};
    std::uint32_t out_of_order_tiles_{0};
    std::uint64_t bytes_d2h_{0};
    std::uint64_t kernel_dispatches_{0};
    std::uint32_t last_tile_id_{0};
    bool has_last_tile_{false};
    bool watermark_seen_{false};
    bool completion_emitted_{false};
    std::uint64_t first_data_sequence_{0};
    bool has_first_data_sequence_{false};
};

} // namespace sar
