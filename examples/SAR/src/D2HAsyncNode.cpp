#include "sar/D2HAsyncNode.hpp"

namespace sar {

std::optional<SarImageTileMessage> D2HAsyncNode::Transfer(
    const SarImageTileMessage& input,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    SarImageTileMessage out = input;
    out.buffer.direction = SarTransferDirection::DeviceToHost;
    return out;
}

} // namespace sar
