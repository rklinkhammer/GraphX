#include <gtest/gtest.h>

#include "sar/AzimuthTileSplitNode.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

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

std::string AzimuthTileSplitPluginFilename() {
    return std::string("libazimuth_tile_split_node") + kSharedLibraryExtension;
}

sar::SarAccelControlToken MakeToken(
    std::uint64_t sequence_id,
    sar::SarFrameMarker marker = sar::SarFrameMarker::Data) {
    sar::SarAccelControlToken token{};
    token.token_id = sequence_id;
    token.sidecar.sequence_id = sequence_id;
    token.sidecar.batch_id = 5;
    token.sidecar.aperture_id = sequence_id;
    token.sidecar.pulse_range_start = sequence_id;
    token.sidecar.pulse_range_count = 1;
    token.sidecar.stream_id = 17;
    token.sidecar.backend_id = 2;
    token.sidecar.backend = sar::SarBackendKind::SimulatedDevice;
    token.sidecar.marker = marker;
    token.sidecar.synthetic = true;
    token.sidecar.payload_byte_count = 12u;
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1001u));
    token.host_view.bytes = 12u;
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 3;
    token.host_view.layout.stride[0] = 1;
    token.has_host_view = true;
    return token;
}

TEST(AzimuthTileSplitNodeTest, SplitsToDeterministicTileAndEmitsAccelToken) {
    sar::AzimuthTileSplitConfig cfg{};
    cfg.tile_count = 4;
    cfg.tile_id_offset = 2;
    cfg.backend_id = 9;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::AzimuthTileSplitNode node(cfg);
    const sar::SarAccelControlToken input = MakeToken(7);

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->has_host_view);
    EXPECT_EQ(out->host_view.dtype, graph::gpu::accel::DataType::Float32);
    EXPECT_EQ(out->host_view.bytes, input.host_view.bytes);
    EXPECT_EQ(out->host_view.allocator_id, 10u);
    EXPECT_GT(out->token_id, 0u);
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->sidecar.sequence_id, 7u);
    EXPECT_EQ(out->sidecar.tile_id, 1u);
    EXPECT_EQ(out->sidecar.stream_id, input.sidecar.stream_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.sidecar.payload_byte_count);
}

TEST(AzimuthTileSplitNodeTest, PropagatesEndOfStreamWithStableTileMetadata) {
    sar::AzimuthTileSplitConfig cfg{};
    cfg.tile_count = 8;
    cfg.tile_id_offset = 3;
    cfg.backend_id = 1;
    cfg.backend = sar::SarBackendKind::Host;

    sar::AzimuthTileSplitNode node(cfg);
    const sar::SarAccelControlToken eos_input = MakeToken(10, sar::SarFrameMarker::EndOfStream);

    auto out = node.Transfer(
        eos_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->has_host_view);
    EXPECT_EQ(out->host_view.bytes, static_cast<std::uint64_t>(sizeof(float)));
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->sidecar.sequence_id, 10u);
    EXPECT_EQ(out->sidecar.tile_id, 5u);
    EXPECT_EQ(out->sidecar.stream_id, eos_input.sidecar.stream_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, 0u);
}

TEST(AzimuthTileSplitNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(AzimuthTileSplitPluginFilename()));

    auto created = registry->CreateNodeExpected("AzimuthTileSplitNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::AzimuthTileSplitNode>();
    ASSERT_TRUE(node);

    sar::AzimuthTileSplitConfig cfg{};
    cfg.tile_count = 5;
    cfg.tile_id_offset = 2;
    cfg.backend_id = 4;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;
    node->SetConfig(cfg);

    auto out = node->Transfer(
        MakeToken(9),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->has_host_view);
    EXPECT_EQ(out->host_view.bytes, 12u);
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->sidecar.tile_id, 1u);
}

} // namespace
