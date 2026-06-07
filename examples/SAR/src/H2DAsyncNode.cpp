#include "sar/H2DAsyncNode.hpp"

namespace sar {

std::optional<SarRangeTileMessage> H2DAsyncNode::Transfer(
    const SarRangeTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    SarRangeTileMessage out = input;
    out.buffer.direction = SarTransferDirection::HostToDevice;
    return out;
}

} // namespace sar
