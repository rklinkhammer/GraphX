#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace sar::detail {

struct AccelTokenImagePayload {
    std::uint64_t sequence_id{0};
    std::uint32_t tile_id{0};
    std::vector<float> pixels{};
};

void StoreAccelTokenImagePayload(std::uint64_t token, AccelTokenImagePayload payload);
std::optional<AccelTokenImagePayload> ConsumeAccelTokenImagePayload(std::uint64_t token);

} // namespace sar::detail
