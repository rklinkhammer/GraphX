// SPDX-License-Identifier: MIT

/**
 * @file test_image_tile_merge.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/ImageTileMergeNode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace {

sar::SarAccelControlToken MakeTile(
    std::uint64_t sequence_id,
    std::uint32_t tile_id,
    sar::SarFrameMarker marker,
    std::size_t byte_count = 16u,
    std::uint32_t stream_id = 1u) {
    sar::SarAccelControlToken msg{};
    msg.token_id = (sequence_id << 16u) | static_cast<std::uint64_t>(tile_id);
    msg.sidecar.sequence_id = sequence_id;
    msg.sidecar.batch_id = stream_id;
    msg.sidecar.aperture_id = sequence_id;
    msg.sidecar.pulse_range_start = sequence_id;
    msg.sidecar.pulse_range_count = (marker == sar::SarFrameMarker::Data) ? 1u : 0u;
    msg.sidecar.stream_id = stream_id;
    msg.sidecar.tile_id = tile_id;
    msg.sidecar.tile_count = 4u;
    msg.sidecar.backend_id = 5u;
    msg.sidecar.backend = sar::SarBackendKind::NativeDevice;
    msg.sidecar.marker = marker;
    msg.sidecar.payload_byte_count = byte_count;

    msg.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(msg.token_id + 1u));
    msg.host_view.bytes = static_cast<std::uint64_t>(byte_count);
    msg.host_view.dtype = graph::gpu::accel::DataType::Float32;
    msg.host_view.layout = sar::MakeAccelVectorLayout(
        static_cast<std::uint64_t>(std::max<std::size_t>(1u, byte_count / sizeof(float))));
    msg.host_view.allocator_id = 5;
    msg.has_host_view = true;
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
        EXPECT_FALSE(status->sidecar.merge_complete);
    }

    auto eos = node.Transfer(
        MakeTile(4, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(eos.has_value());
    EXPECT_TRUE(eos->sidecar.merge_complete);
    EXPECT_EQ(eos->sidecar.missing_tiles, 0u);
    EXPECT_EQ(eos->sidecar.duplicate_tiles, 0u);
}

} // namespace
