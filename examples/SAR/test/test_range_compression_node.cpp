#include <gtest/gtest.h>

#include "sar/RangeCompressionNode.hpp"

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

std::string RangeCompressionPluginFilename() {
    return std::string("librange_compression_node") + kSharedLibraryExtension;
}

sar::SarAccelControlToken MakeToken() {
    sar::SarAccelControlToken token{};
    token.token_id = 44u;
    token.sidecar.sequence_id = 3u;
    token.sidecar.stream_id = 9u;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.payload_byte_count = 256u;
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1001u));
    token.host_view.bytes = 256u;
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 64;
    token.host_view.layout.stride[0] = 1;
    token.has_host_view = true;
    return token;
}

TEST(RangeCompressionNodeTest, EnabledCompressionPreservesTokenIdentityAndUpdatesTiming) {
    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 48000.0;

    sar::RangeCompressionNode node(cfg);
    const auto input = MakeToken();

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.sequence_id, input.sidecar.sequence_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.sidecar.payload_byte_count);
    EXPECT_GT(out->sidecar.stage_timings.range_compression_time_us, 0u);
}

TEST(RangeCompressionNodeTest, EndOfStreamPassesThroughWithoutCompression) {
    sar::RangeCompressionNode node;
    auto input = MakeToken();
    input.sidecar.marker = sar::SarFrameMarker::EndOfStream;

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->sidecar.stage_timings.range_compression_time_us, 0u);
}

TEST(RangeCompressionNodeTest, NumericalCompressionIsDeferredAndStageIsTokenTimingOnly) {
    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.mode = sar::RangeCompressionMode::MatchedFilter;
    cfg.output = sar::RangeCompressionOutput::Complex;
    cfg.gain = 4.0f;
    cfg.sample_rate_hz = 16000000.0;
    cfg.bandwidth_hz = 4000000.0;
    cfg.chirp_duration_s = 0.000001;
    cfg.range_origin_m = 10.0;
    cfg.range_spacing_m = 0.5;

    sar::RangeCompressionNode node(cfg);
    const auto input = MakeToken();

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.sequence_id, input.sidecar.sequence_id);
    EXPECT_EQ(out->sidecar.payload_byte_count, input.sidecar.payload_byte_count);
    EXPECT_EQ(out->host_view.bytes, input.host_view.bytes);
    EXPECT_GT(out->sidecar.stage_timings.range_compression_time_us, 0u);
}

TEST(RangeCompressionNodeTest, ConfigureEnablesMatchedFilterModeFromJson) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"mode", "matched_filter"},
        {"output", "complex"},
        {"gain", 0.5},
        {"sample_rate_hz", 16000000.0},
        {"bandwidth_hz", 4000000.0},
        {"chirp_duration_s", 0.000001},
        {"range_origin_m", 0.0},
        {"range_spacing_m", 0.25},
    };
    node.Configure(graph::JsonView(cfg_json));

    const auto& cfg = node.GetConfig();
    EXPECT_EQ(cfg.mode, sar::RangeCompressionMode::MatchedFilter);
    EXPECT_EQ(cfg.output, sar::RangeCompressionOutput::Complex);
    EXPECT_FLOAT_EQ(cfg.gain, 0.5f);
    EXPECT_DOUBLE_EQ(cfg.sample_rate_hz, 16000000.0);
    EXPECT_DOUBLE_EQ(cfg.bandwidth_hz, 4000000.0);
}

TEST(RangeCompressionNodeTest, ConfigureRejectsIncompleteMatchedFilterModeFromJson) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"mode", "matched_filter"},
        {"gain", 1.0},
    };

    EXPECT_THROW(node.Configure(graph::JsonView(cfg_json)), graph::ConfigError);
}

TEST(RangeCompressionNodeTest, DefaultModeRemainsFftMagnitudeWhenModeIsOmitted) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"gain", 1.0},
        {"sample_rate_hz", 48000.0},
    };

    node.Configure(graph::JsonView(cfg_json));

    const auto& cfg = node.GetConfig();
    EXPECT_EQ(cfg.mode, sar::RangeCompressionMode::FftMagnitude);
    EXPECT_EQ(cfg.output, sar::RangeCompressionOutput::Magnitude);

    const auto input = MakeToken();
    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_GT(out->sidecar.stage_timings.range_compression_time_us, 0u);
}

TEST(RangeCompressionNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(RangeCompressionPluginFilename()));

    auto created = registry->CreateNodeExpected("RangeCompressionNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::RangeCompressionNode>();
    ASSERT_TRUE(node);

    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 48000.0;
    node->SetConfig(cfg);

    auto out = node->Transfer(
        MakeToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_GT(out->sidecar.stage_timings.range_compression_time_us, 0u);
}

} // namespace
