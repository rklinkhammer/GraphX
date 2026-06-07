#include <gtest/gtest.h>

#include "sar/RangeWindowNode.hpp"

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

std::string RangeWindowPluginFilename() {
    return std::string("librange_window_node") + kSharedLibraryExtension;
}

sar::SarPulseBlockMessage MakePulse() {
    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = 3;
    msg.envelope.stream_id = 9;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.buffer.buffer_id = 44;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;
    msg.iq_samples = {
        sar::SarIqSample(1.0f, 1.0f),
        sar::SarIqSample(2.0f, 2.0f),
        sar::SarIqSample(3.0f, 3.0f),
        sar::SarIqSample(4.0f, 4.0f),
    };
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
}

TEST(RangeWindowNodeTest, AppliesDeterministicHannWindowAndGain) {
    sar::RangeWindowConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 2.0f;

    sar::RangeWindowNode node(cfg);
    auto out = node.Transfer(
        MakePulse(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), 4u);
    EXPECT_FLOAT_EQ(out->iq_samples[0].real(), 0.0f);
    EXPECT_FLOAT_EQ(out->iq_samples[0].imag(), 0.0f);
    EXPECT_NEAR(out->iq_samples[1].real(), 3.0f, 1.0e-5f);
    EXPECT_NEAR(out->iq_samples[1].imag(), 3.0f, 1.0e-5f);
    EXPECT_NEAR(out->iq_samples[2].real(), 4.5f, 1.0e-5f);
    EXPECT_NEAR(out->iq_samples[2].imag(), 4.5f, 1.0e-5f);
    EXPECT_NEAR(out->iq_samples[3].real(), 0.0f, 1.0e-5f);
    EXPECT_NEAR(out->iq_samples[3].imag(), 0.0f, 1.0e-5f);
    EXPECT_EQ(out->buffer.byte_count, out->iq_samples.size() * sizeof(sar::SarIqSample));
}

TEST(RangeWindowNodeTest, DisabledWindowPassesPulseThrough) {
    sar::RangeWindowConfig cfg{};
    cfg.enabled = false;

    sar::RangeWindowNode node(cfg);
    const auto input = MakePulse();
    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());
    for (std::size_t i = 0; i < input.iq_samples.size(); ++i) {
        EXPECT_EQ(out->iq_samples[i], input.iq_samples[i]);
    }
}

TEST(RangeWindowNodeTest, EndOfStreamPassesThroughWithoutWindowing) {
    sar::RangeWindowNode node;
    auto input = MakePulse();
    input.envelope.marker = sar::SarFrameMarker::EndOfStream;

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::EndOfStream);
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_EQ(out->iq_samples[1], input.iq_samples[1]);
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
        MakePulse(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_NEAR(out->iq_samples[1].real(), 3.0f, 1.0e-5f);
}

} // namespace
