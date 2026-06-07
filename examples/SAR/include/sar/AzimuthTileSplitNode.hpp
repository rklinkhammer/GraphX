#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace sar {

struct AzimuthTileSplitConfig {
    std::uint32_t tile_count{4};
    std::uint32_t backend_id{0};
    SarBackendKind backend{SarBackendKind::Host};
};

class AzimuthTileSplitNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarPulseBlockMessage>,
          graph::TypeList<SarRangeTileMessage>,
          AzimuthTileSplitNode> {
public:
    AzimuthTileSplitNode() = default;
    explicit AzimuthTileSplitNode(AzimuthTileSplitConfig config);

    std::optional<SarRangeTileMessage> Transfer(
        const SarPulseBlockMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void SetConfig(const AzimuthTileSplitConfig& config);
    const AzimuthTileSplitConfig& GetConfig() const noexcept;

private:
    SarRangeTileMessage BuildDataTile(const SarPulseBlockMessage& input) const;
    SarRangeTileMessage BuildEndOfStreamTile(const SarPulseBlockMessage& input) const;

    AzimuthTileSplitConfig config_{};
};

} // namespace sar
