#include <gtest/gtest.h>

#include "sar/ImageTileMergeNode.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

std::string ImageTileMergePluginFilename() {
    return std::string("libimage_tile_merge_node") + kSharedLibraryExtension;
}

sar::SarAccelControlToken MakeImageTile(
    std::uint64_t sequence_id,
    std::uint32_t tile_id,
    sar::SarFrameMarker marker = sar::SarFrameMarker::Data,
    std::size_t byte_count = 16u,
    std::uint32_t stream_id = 3u) {
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
    msg.sidecar.backend_id = 7u;
    msg.sidecar.backend = sar::SarBackendKind::NativeDevice;
    msg.sidecar.marker = marker;
    msg.sidecar.synthetic = true;
    msg.sidecar.payload_byte_count = byte_count;

    msg.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    msg.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(msg.token_id + 1u));
    msg.host_view.bytes = static_cast<std::uint64_t>(byte_count);
    msg.host_view.dtype = graph::gpu::accel::DataType::Float32;
    msg.host_view.layout = sar::MakeAccelVectorLayout(
        static_cast<std::uint64_t>(std::max<std::size_t>(1u, byte_count / sizeof(float))));
    msg.host_view.allocator_id = 7;
    msg.has_host_view = true;
    return msg;
}

TEST(ImageTileMergeNodeTest, CompletesAtEndOfStreamWhenAllTilesArrive) {
    sar::ImageTileMergeConfig cfg{};
    cfg.expected_tiles = 3;
    cfg.backend_id = 4;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::ImageTileMergeNode node(cfg);

    auto status0 = node.Transfer(
        MakeImageTile(10, 0),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto status1 = node.Transfer(
        MakeImageTile(11, 2),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto status2 = node.Transfer(
        MakeImageTile(12, 1),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto eos = node.Transfer(
        MakeImageTile(13, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status0.has_value());
    ASSERT_TRUE(status1.has_value());
    ASSERT_TRUE(status2.has_value());
    ASSERT_TRUE(eos.has_value());

    EXPECT_EQ(eos->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos->expected_tiles, 3u);
    EXPECT_EQ(eos->received_tiles, 3u);
    EXPECT_EQ(eos->duplicate_tiles, 0u);
    EXPECT_EQ(eos->missing_tiles, 0u);
    EXPECT_EQ(eos->out_of_order_tiles, 1u);
    EXPECT_EQ(eos->bytes_h2d, 48u);
    EXPECT_EQ(eos->bytes_d2h, 48u);
    EXPECT_EQ(eos->kernel_dispatches, 3u);
    EXPECT_FALSE(eos->watermark_seen);
    EXPECT_TRUE(eos->complete);
}

TEST(ImageTileMergeNodeTest, ReportsDuplicateAndMissingTiles) {
    sar::ImageTileMergeConfig cfg{};
    cfg.expected_tiles = 4;

    sar::ImageTileMergeNode node(cfg);

    ASSERT_TRUE(node.Transfer(
        MakeImageTile(20, 0),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));
    ASSERT_TRUE(node.Transfer(
        MakeImageTile(21, 1),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));
    ASSERT_TRUE(node.Transfer(
        MakeImageTile(22, 1),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));

    auto eos = node.Transfer(
        MakeImageTile(23, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->expected_tiles, 4u);
    EXPECT_EQ(eos->received_tiles, 2u);
    EXPECT_EQ(eos->duplicate_tiles, 1u);
    EXPECT_EQ(eos->missing_tiles, 2u);
    EXPECT_EQ(eos->bytes_h2d, 48u);
    EXPECT_EQ(eos->bytes_d2h, 48u);
    EXPECT_EQ(eos->kernel_dispatches, 3u);
    EXPECT_FALSE(eos->complete);
}

TEST(ImageTileMergeNodeTest, RequiresWatermarkWhenConfigured) {
    sar::ImageTileMergeConfig cfg{};
    cfg.expected_tiles = 1;
    cfg.require_watermark_before_complete = true;

    sar::ImageTileMergeNode node(cfg);

    ASSERT_TRUE(node.Transfer(
        MakeImageTile(30, 0),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));

    auto eos_without_watermark = node.Transfer(
        MakeImageTile(31, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(eos_without_watermark.has_value());
    EXPECT_EQ(eos_without_watermark->bytes_h2d, 16u);
    EXPECT_EQ(eos_without_watermark->bytes_d2h, 16u);
    EXPECT_EQ(eos_without_watermark->kernel_dispatches, 1u);
    EXPECT_FALSE(eos_without_watermark->watermark_seen);
    EXPECT_FALSE(eos_without_watermark->complete);

    node.Reset();

    ASSERT_TRUE(node.Transfer(
        MakeImageTile(40, 0),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));
    auto watermark = node.Transfer(
        MakeImageTile(41, 0, sar::SarFrameMarker::Watermark),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto eos_with_watermark = node.Transfer(
        MakeImageTile(42, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(watermark.has_value());
    ASSERT_TRUE(eos_with_watermark.has_value());
    EXPECT_EQ(eos_with_watermark->bytes_h2d, 16u);
    EXPECT_EQ(eos_with_watermark->bytes_d2h, 16u);
    EXPECT_EQ(eos_with_watermark->kernel_dispatches, 1u);
    EXPECT_TRUE(eos_with_watermark->watermark_seen);
    EXPECT_TRUE(eos_with_watermark->complete);
}

TEST(ImageTileMergeNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(ImageTileMergePluginFilename()));

    auto created = registry->CreateNodeExpected("ImageTileMergeNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::ImageTileMergeNode>();
    ASSERT_TRUE(node);

    sar::ImageTileMergeConfig cfg{};
    cfg.expected_tiles = 2;
    cfg.backend_id = 9;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;
    node->SetConfig(cfg);

    ASSERT_TRUE(node->Transfer(
        MakeImageTile(50, 0),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));
    ASSERT_TRUE(node->Transfer(
        MakeImageTile(51, 1),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{}));

    auto eos = node->Transfer(
        MakeImageTile(52, 0, sar::SarFrameMarker::EndOfStream),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->expected_tiles, 2u);
    EXPECT_EQ(eos->received_tiles, 2u);
    EXPECT_EQ(eos->missing_tiles, 0u);
    EXPECT_EQ(eos->bytes_h2d, 32u);
    EXPECT_EQ(eos->bytes_d2h, 32u);
    EXPECT_EQ(eos->kernel_dispatches, 2u);
    EXPECT_TRUE(eos->complete);
}

} // namespace
