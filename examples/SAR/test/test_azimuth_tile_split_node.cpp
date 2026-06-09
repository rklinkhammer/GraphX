#include <gtest/gtest.h>

#include "sar/AzimuthTileSplitNode.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <cstddef>
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

sar::SarPulseBlockMessage MakePulse(
    std::uint64_t sequence_id,
    sar::SarFrameMarker marker = sar::SarFrameMarker::Data) {
    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = sequence_id;
    msg.envelope.batch_id = 5;
    msg.envelope.aperture_id = sequence_id;
    msg.envelope.pulse_range_start = sequence_id;
    msg.envelope.pulse_range_count = 1;
    msg.envelope.stream_id = 17;
    msg.envelope.backend_id = 2;
    msg.envelope.backend = sar::SarBackendKind::SimulatedDevice;
    msg.envelope.marker = marker;
    msg.envelope.synthetic = true;

    msg.buffer.buffer_id = sequence_id;
    msg.buffer.byte_count = 0;
    msg.buffer.device_index = 2;
    msg.buffer.backend = sar::SarBackendKind::SimulatedDevice;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;

    msg.iq_samples = {
        sar::SarIqSample(1.0f, 0.5f),
        sar::SarIqSample(2.0f, 1.5f),
        sar::SarIqSample(3.0f, 2.5f),
    };
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
}

TEST(AzimuthTileSplitNodeTest, SplitsToDeterministicTileAndEmitsAccelToken) {
    sar::AzimuthTileSplitConfig cfg{};
    cfg.tile_count = 4;
    cfg.tile_id_offset = 2;
    cfg.backend_id = 9;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::AzimuthTileSplitNode node(cfg);
    const sar::SarPulseBlockMessage input = MakePulse(7);

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->has_host_view);
    EXPECT_EQ(out->host_view.dtype, graph::gpu::accel::DataType::Float32);
    EXPECT_EQ(out->host_view.bytes, static_cast<std::uint64_t>(input.iq_samples.size() * sizeof(float)));
    EXPECT_EQ(out->host_view.allocator_id, 10u);
    EXPECT_GT(out->token_id, 0u);
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->sidecar.sequence_id, 7u);
    EXPECT_EQ(out->sidecar.tile_id, 1u);
    EXPECT_EQ(out->sidecar.stream_id, input.envelope.stream_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.iq_samples.size() * sizeof(float));
}

TEST(AzimuthTileSplitNodeTest, PropagatesEndOfStreamWithStableTileMetadata) {
    sar::AzimuthTileSplitConfig cfg{};
    cfg.tile_count = 8;
    cfg.tile_id_offset = 3;
    cfg.backend_id = 1;
    cfg.backend = sar::SarBackendKind::Host;

    sar::AzimuthTileSplitNode node(cfg);
    const sar::SarPulseBlockMessage eos_input = MakePulse(10, sar::SarFrameMarker::EndOfStream);

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
    EXPECT_EQ(out->sidecar.stream_id, eos_input.envelope.stream_id);
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
        MakePulse(9),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(out->has_host_view);
    EXPECT_EQ(out->host_view.bytes, 12u);
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->sidecar.tile_id, 1u);
}

} // namespace
