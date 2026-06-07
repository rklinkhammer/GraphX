#include <gtest/gtest.h>

#include "sar/SarBackprojectionTransformNode.hpp"

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

std::string BackprojectionPluginFilename() {
    return std::string("libsar_backprojection_transform_node") + kSharedLibraryExtension;
}

sar::SarRangeTileMessage MakeRangeTile(
    std::uint64_t sequence_id,
    std::uint32_t tile_id,
    sar::SarFrameMarker marker = sar::SarFrameMarker::Data) {
    sar::SarRangeTileMessage msg{};
    msg.envelope.sequence_id = sequence_id;
    msg.envelope.batch_id = 8;
    msg.envelope.aperture_id = sequence_id;
    msg.envelope.pulse_range_start = sequence_id;
    msg.envelope.pulse_range_count = 1;
    msg.envelope.stream_id = 99;
    msg.envelope.tile_id = tile_id;
    msg.envelope.tile_count = 4;
    msg.envelope.backend_id = 2;
    msg.envelope.backend = sar::SarBackendKind::SimulatedDevice;
    msg.envelope.marker = marker;
    msg.envelope.synthetic = true;

    msg.buffer.buffer_id = (sequence_id << 8u) + tile_id;
    msg.buffer.device_index = 2;
    msg.buffer.backend = sar::SarBackendKind::SimulatedDevice;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;

    msg.range_bins = {0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    msg.buffer.byte_count = msg.range_bins.size() * sizeof(float);
    return msg;
}

TEST(SarBackprojectionTransformNodeTest, ProducesDeterministicImageTileWithDispatchMetadata) {
    sar::SarBackprojectionTransformConfig cfg{};
    cfg.image_width = 2;
    cfg.backend_id = 11;
    cfg.queue_id = 7;
    cfg.kernel_id = 4402;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::SarBackprojectionTransformNode node(cfg);
    const sar::SarRangeTileMessage input = MakeRangeTile(13, 3);

    auto first = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto second = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->pixels.size(), input.range_bins.size());
    ASSERT_EQ(second->pixels.size(), input.range_bins.size());

    EXPECT_EQ(first->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->envelope.batch_id, input.envelope.batch_id);
    EXPECT_EQ(first->envelope.aperture_id, input.envelope.aperture_id);
    EXPECT_EQ(first->envelope.pulse_range_start, input.envelope.pulse_range_start);
    EXPECT_EQ(first->envelope.pulse_range_count, input.envelope.pulse_range_count);
    EXPECT_EQ(first->envelope.tile_id, 3u);
    EXPECT_EQ(first->envelope.tile_count, 4u);
    EXPECT_EQ(first->envelope.backend_id, 11u);
    EXPECT_EQ(first->buffer.direction, sar::SarTransferDirection::DeviceToHost);

    EXPECT_EQ(first->width, 2u);
    EXPECT_EQ(first->height, 3u);
    EXPECT_EQ(first->dispatch.queue_id, 7u);
    EXPECT_EQ(first->dispatch.kernel_id, 4402u);
    EXPECT_EQ(first->dispatch.dispatch_width, 2u);
    EXPECT_EQ(first->dispatch.dispatch_height, 3u);
    EXPECT_EQ(first->dispatch.dispatch_depth, 1u);

    for (std::size_t i = 0; i < first->pixels.size(); ++i) {
        EXPECT_FLOAT_EQ(first->pixels[i], second->pixels[i]);
    }
}

TEST(SarBackprojectionTransformNodeTest, PropagatesEndOfStreamWithoutPixelPayload) {
    sar::SarBackprojectionTransformConfig cfg{};
    cfg.image_width = 8;
    cfg.backend_id = 5;
    cfg.queue_id = 9;
    cfg.kernel_id = 7001;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::SarBackprojectionTransformNode node(cfg);
    const sar::SarRangeTileMessage eos_input =
        MakeRangeTile(21, 1, sar::SarFrameMarker::EndOfStream);

    auto out = node.Transfer(
        eos_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->envelope.batch_id, eos_input.envelope.batch_id);
    EXPECT_EQ(out->envelope.aperture_id, eos_input.envelope.aperture_id);
    EXPECT_EQ(out->envelope.pulse_range_start, eos_input.envelope.pulse_range_start);
    EXPECT_EQ(out->envelope.pulse_range_count, eos_input.envelope.pulse_range_count);
    EXPECT_TRUE(out->pixels.empty());
    EXPECT_EQ(out->buffer.byte_count, 0u);
    EXPECT_EQ(out->dispatch.queue_id, 9u);
    EXPECT_EQ(out->dispatch.kernel_id, 7001u);
    EXPECT_EQ(out->dispatch.dispatch_width, 8u);
    EXPECT_EQ(out->dispatch.dispatch_height, 1u);
    EXPECT_TRUE(out->gpu.has_kernel_ticket);
    EXPECT_EQ(out->gpu.kernel_ticket.kernel_id, 7001u);
    EXPECT_EQ(out->gpu.kernel_ticket.execution_queue_id, 10u);
    EXPECT_EQ(out->gpu.kernel_ticket.arg_count, 0u);
}

TEST(SarBackprojectionTransformNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(BackprojectionPluginFilename()));

    auto created = registry->CreateNodeExpected("SarBackprojectionTransformNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::SarBackprojectionTransformNode>();
    ASSERT_TRUE(node);

    sar::SarBackprojectionTransformConfig cfg{};
    cfg.image_width = 3;
    cfg.backend_id = 4;
    cfg.queue_id = 2;
    cfg.kernel_id = 901;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;
    node->SetConfig(cfg);

    auto out = node->Transfer(
        MakeRangeTile(4, 2),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->envelope.tile_id, 2u);
    EXPECT_EQ(out->dispatch.queue_id, 2u);
    EXPECT_EQ(out->dispatch.kernel_id, 901u);
    EXPECT_EQ(out->width, 3u);
}

} // namespace
