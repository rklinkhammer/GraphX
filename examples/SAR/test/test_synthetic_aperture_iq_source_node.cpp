#include <gtest/gtest.h>

#include "sar/SyntheticApertureIqSourceNode.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

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

std::string SyntheticApertureIqSourcePluginFilename() {
    return std::string("libsynthetic_aperture_iq_source_node") + kSharedLibraryExtension;
}

TEST(SyntheticApertureIqSourceNodeTest, EmitsDeterministicPulseBlocksThenEos) {
    sar::SyntheticApertureIqSourceConfig cfg{};
    cfg.stream_id = 7;
    cfg.total_pulses = 2;
    cfg.samples_per_pulse = 4;
    cfg.backend_id = 3;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::SyntheticApertureIqSourceNode node(cfg);

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->envelope.sequence_id, 0u);
    EXPECT_EQ(first->envelope.stream_id, cfg.stream_id);
    EXPECT_EQ(first->envelope.backend_id, cfg.backend_id);
    EXPECT_EQ(first->envelope.backend, cfg.backend);
    EXPECT_EQ(first->iq_samples.size(), cfg.samples_per_pulse);
    EXPECT_FLOAT_EQ(first->iq_samples[0].real(), 0.0f);
    EXPECT_FLOAT_EQ(first->iq_samples[0].imag(), 0.0f);
    EXPECT_FLOAT_EQ(first->iq_samples[1].real(), 0.001f);
    EXPECT_FLOAT_EQ(first->iq_samples[1].imag(), -0.0015f);

    auto second = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(second->envelope.sequence_id, 1u);
    EXPECT_EQ(second->iq_samples.size(), cfg.samples_per_pulse);
    EXPECT_FLOAT_EQ(second->iq_samples[0].real(), 1.0f);
    EXPECT_FLOAT_EQ(second->iq_samples[0].imag(), 0.25f);

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos->envelope.sequence_id, 2u);
    EXPECT_TRUE(eos->iq_samples.empty());
    EXPECT_EQ(eos->buffer.byte_count, 0u);

    auto after_eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(after_eos.has_value());
}

TEST(SyntheticApertureIqSourceNodeTest, ResetRestartsSequenceAndData) {
    sar::SyntheticApertureIqSourceConfig cfg{};
    cfg.total_pulses = 1;
    cfg.samples_per_pulse = 2;

    sar::SyntheticApertureIqSourceNode node(cfg);

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->envelope.sequence_id, 0u);

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->envelope.marker, sar::SarFrameMarker::EndOfStream);

    node.Reset();

    auto restarted = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(restarted.has_value());
    EXPECT_EQ(restarted->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(restarted->envelope.sequence_id, 0u);
}

TEST(SyntheticApertureIqSourceNodeTest, SetConfigAppliesAndResetsState) {
    sar::SyntheticApertureIqSourceNode node;

    sar::SyntheticApertureIqSourceConfig cfg{};
    cfg.stream_id = 11;
    cfg.total_pulses = 1;
    cfg.samples_per_pulse = 3;
    cfg.backend_id = 9;
    cfg.backend = sar::SarBackendKind::NativeDevice;

    node.SetConfig(cfg);
    const auto& active = node.GetConfig();
    EXPECT_EQ(active.stream_id, 11u);
    EXPECT_EQ(active.total_pulses, 1u);
    EXPECT_EQ(active.samples_per_pulse, 3u);
    EXPECT_EQ(active.backend_id, 9u);
    EXPECT_EQ(active.backend, sar::SarBackendKind::NativeDevice);

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->envelope.sequence_id, 0u);
    EXPECT_EQ(first->envelope.stream_id, 11u);
    EXPECT_EQ(first->iq_samples.size(), 3u);
}

TEST(SyntheticApertureIqSourceNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(SyntheticApertureIqSourcePluginFilename()));

    auto created = registry->CreateNodeExpected("SyntheticApertureIqSourceNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::SyntheticApertureIqSourceNode>();
    ASSERT_TRUE(node);

    sar::SyntheticApertureIqSourceConfig cfg{};
    cfg.stream_id = 5;
    cfg.total_pulses = 1;
    cfg.samples_per_pulse = 2;
    cfg.backend_id = 4;
    cfg.backend = sar::SarBackendKind::SimulatedDevice;
    node->SetConfig(cfg);

    auto pulse = node->Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(pulse.has_value());
    EXPECT_EQ(pulse->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(pulse->envelope.sequence_id, 0u);
    EXPECT_EQ(pulse->envelope.stream_id, 5u);
    EXPECT_EQ(pulse->iq_samples.size(), 2u);

    auto eos = node->Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->envelope.marker, sar::SarFrameMarker::EndOfStream);

    auto after = node->Produce(std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(after.has_value());
}

} // namespace
