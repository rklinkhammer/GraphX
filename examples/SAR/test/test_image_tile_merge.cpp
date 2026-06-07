#include <gtest/gtest.h>

#include "sar/ImageTileMergeNode.hpp"

#include <cstddef>

namespace {

sar::SarImageTileMessage MakeTile(std::uint64_t sequence_id, std::uint32_t tile_id, sar::SarFrameMarker marker) {
    sar::SarImageTileMessage msg{};
    msg.envelope.sequence_id = sequence_id;
    msg.envelope.stream_id = 1;
    msg.envelope.tile_id = tile_id;
    msg.envelope.tile_count = 4;
    msg.envelope.marker = marker;
    msg.buffer.byte_count = 16;
    msg.dispatch.kernel_id = 3301;
    msg.width = 2;
    msg.height = 2;
    msg.pixels = {1.0f, 2.0f, 3.0f, 4.0f};
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
