#include <gtest/gtest.h>

#include "sar/RangeCompressionNode.hpp"

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

std::string RangeCompressionPluginFilename() {
    return std::string("librange_compression_node") + kSharedLibraryExtension;
}

sar::SarPulseBlockMessage MakePulse(std::size_t sample_count = 256u) {
    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = 3;
    msg.envelope.stream_id = 9;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.buffer.buffer_id = 44;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;
    msg.iq_samples.reserve(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        msg.iq_samples.emplace_back(static_cast<float>(i % 17) * 0.1f, static_cast<float>(i % 11) * 0.07f);
    }
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
}

TEST(RangeCompressionNodeTest, AppliesDeterministicCompressionWhenEnabled) {
    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 48000.0;

    sar::RangeCompressionNode node(cfg);
    const auto input = MakePulse(256u);

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_NE(out->iq_samples[0].real(), input.iq_samples[0].real());
    EXPECT_EQ(out->iq_samples[0].imag(), 0.0f);
    EXPECT_EQ(out->buffer.byte_count, out->iq_samples.size() * sizeof(sar::SarIqSample));
}

TEST(RangeCompressionNodeTest, EndOfStreamPassesThroughWithoutCompression) {
    sar::RangeCompressionNode node;
    auto input = MakePulse(256u);
    input.envelope.marker = sar::SarFrameMarker::EndOfStream;

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_EQ(out->iq_samples[3], input.iq_samples[3]);
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
        MakePulse(256u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->iq_samples.size(), 256u);
}

} // namespace
