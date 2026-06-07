#pragma once

#include "sar/SarMessages.hpp"

#include "graph/NamedNodes.hpp"

#include <cstddef>
#include <optional>

namespace sar {

class H2DAsyncNode
    : public graph::NamedInteriorNode<
          graph::TypeList<SarRangeTileMessage>,
          graph::TypeList<SarRangeTileMessage>,
          H2DAsyncNode> {
public:
    H2DAsyncNode() = default;

    std::optional<SarRangeTileMessage> Transfer(
        const SarRangeTileMessage& input,
        std::integral_constant<std::size_t, 0>,
        std::integral_constant<std::size_t, 0>) override;
};

} // namespace sar
