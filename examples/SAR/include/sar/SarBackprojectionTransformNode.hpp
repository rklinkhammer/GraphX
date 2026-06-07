#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

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
          SarBackprojectionTransformNode> {
public:
    SarBackprojectionTransformNode() = default;
    explicit SarBackprojectionTransformNode(SarBackprojectionTransformConfig config);

    std::optional<SarImageTileMessage> Transfer(
        const SarRangeTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;

    void SetConfig(const SarBackprojectionTransformConfig& config);
    const SarBackprojectionTransformConfig& GetConfig() const noexcept;

private:
    SarImageTileMessage BuildDataTile(const SarRangeTileMessage& input) const;
    SarImageTileMessage BuildEndOfStreamTile(const SarRangeTileMessage& input) const;

    std::uint32_t ResolveImageWidth() const noexcept;

    SarBackprojectionTransformConfig config_{};
};

} // namespace sar
