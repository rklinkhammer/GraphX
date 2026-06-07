#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <optional>

namespace sar {

class D2HAsyncNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarImageTileMessage>,
          graph::TypeList<SarImageTileMessage>,
          D2HAsyncNode> {
public:
    D2HAsyncNode() = default;

    std::optional<SarImageTileMessage> Transfer(
        const SarImageTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;
};

} // namespace sar
