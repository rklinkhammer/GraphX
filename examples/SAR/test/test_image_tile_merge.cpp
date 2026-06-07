#include <gtest/gtest.h>

#include "sar/ImageTileMergeNode.hpp"

#include "gpu/accel/types/AccelTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {

std::uint64_t EncodeToken(std::uint64_t sequence_id,
                          std::uint32_t tile_id,
                          std::size_t byte_count,
                          std::uint32_t stream_id,
                          sar::SarFrameMarker marker) {
    const auto marker_bits = static_cast<std::uint64_t>(marker) & 0x3u;
    const auto tile_bits = static_cast<std::uint64_t>(tile_id) & 0xFFFu;
    const auto sequence_bits = sequence_id & 0xFFFFFFu;
    const auto byte_bits = static_cast<std::uint64_t>(byte_count) & 0xFFFFu;
    const auto stream_bits = static_cast<std::uint64_t>(stream_id) & 0x3FFu;

    return marker_bits |
           (tile_bits << 2u) |
           (sequence_bits << 14u) |
           (byte_bits << 38u) |
           (stream_bits << 54u);
}

graph::gpu::accel::HostPinnedBufferView MakeTile(
    std::uint64_t sequence_id,
    std::uint32_t tile_id,
    sar::SarFrameMarker marker,
    std::size_t byte_count = 16u,
    std::uint32_t stream_id = 1u) {
    graph::gpu::accel::HostPinnedBufferView msg{};
    msg.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        EncodeToken(sequence_id, tile_id, byte_count, stream_id, marker)));
    msg.bytes = static_cast<std::uint64_t>(byte_count);
    msg.dtype = graph::gpu::accel::DataType::Float32;
    msg.layout = sar::MakeAccelVectorLayout(
        static_cast<std::uint64_t>(std::max<std::size_t>(1u, byte_count / sizeof(float))));
    msg.allocator_id = 5;
    return msg;
}

TEST(ImageTileMergeFileCoverageTest, HappyPathCompletesOnEos) {
    sar::ImageTileMergeConfig cfg{};
    cfg.expected_tiles = 4;

    sar::ImageTileMergeNode node(cfg);

    for (std::uint32_t tile_id = 0; tile_id < 4; ++tile_id) {
        auto status = node.Transfer(
            MakeTile(tile_id, tile_id, sar::SarFrameMarker::Data),
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        ASSERT_TRUE(status.has_value());
        EXPECT_FALSE(status->complete);
    }

    auto eos = node.Transfer(
        MakeTile(4, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(eos.has_value());
    EXPECT_TRUE(eos->complete);
    EXPECT_EQ(eos->missing_tiles, 0u);
    EXPECT_EQ(eos->duplicate_tiles, 0u);
}

} // namespace
