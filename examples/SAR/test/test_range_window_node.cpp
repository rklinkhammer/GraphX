// SPDX-License-Identifier: MIT

/**
 * @file test_range_window_node.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/RangeWindowNode.hpp"

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

std::string RangeWindowPluginFilename() {
    return std::string("librange_window_node") + kSharedLibraryExtension;
}

sar::SarControlToken MakeToken() {
    sar::SarControlToken token{};
    token.token_id = 44u;
    token.sidecar.sequence_id = 3u;
    token.sidecar.stream_id = 9u;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.payload_byte_count = 16u;
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1001u));
    token.host_view.bytes = 16u;
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 4;
    token.host_view.layout.stride[0] = 1;
    token.has_host_view = true;
    return token;
}

TEST(RangeWindowNodeTest, EnabledWindowPreservesTokenIdentityAndUpdatesTiming) {
    sar::RangeWindowConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 2.0f;

    sar::RangeWindowNode node(cfg);
    auto out = node.Transfer(
        MakeToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.sequence_id, 3u);
    EXPECT_EQ(out->sidecar.stream_id, 9u);
    EXPECT_EQ(out->sidecar.payload_byte_count, 16u);
    EXPECT_GT(out->sidecar.stage_timings.range_window_time_us, 0u);
}

TEST(RangeWindowNodeTest, DisabledWindowPassesTokenThroughWithoutTiming) {
    sar::RangeWindowConfig cfg{};
    cfg.enabled = false;

    sar::RangeWindowNode node(cfg);
    const auto input = MakeToken();
    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.sequence_id, input.sidecar.sequence_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.sidecar.payload_byte_count);
    EXPECT_EQ(out->sidecar.stage_timings.range_window_time_us, 0u);
}

TEST(RangeWindowNodeTest, EndOfStreamPassesThroughWithoutWindowing) {
    sar::RangeWindowNode node;
    auto input = MakeToken();
    input.sidecar.marker = sar::SarFrameMarker::EndOfStream;

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->sidecar.stage_timings.range_window_time_us, 0u);
}

TEST(RangeWindowNodeTest, NumericalWindowingIsDeferredAndStageIsTokenTimingOnly) {
    sar::RangeWindowConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 7.5f;

    sar::RangeWindowNode node(cfg);
    const auto input = MakeToken();

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.sequence_id, input.sidecar.sequence_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.sidecar.payload_byte_count);
    EXPECT_EQ(out->host_view.bytes, input.host_view.bytes);
    EXPECT_GT(out->sidecar.stage_timings.range_window_time_us, 0u);
}

TEST(RangeWindowNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(RangeWindowPluginFilename()));

    auto created = registry->CreateNodeExpected("RangeWindowNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::RangeWindowNode>();
    ASSERT_TRUE(node);

    sar::RangeWindowConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 2.0f;
    node->SetConfig(cfg);

    auto out = node->Transfer(
        MakeToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_GT(out->sidecar.stage_timings.range_window_time_us, 0u);
}

} // namespace
