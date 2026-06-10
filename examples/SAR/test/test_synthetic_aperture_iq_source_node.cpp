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
    EXPECT_EQ(first->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->sidecar.sequence_id, 0u);
    EXPECT_EQ(first->sidecar.batch_id, cfg.stream_id);
    EXPECT_EQ(first->sidecar.aperture_id, 0u);
    EXPECT_EQ(first->sidecar.pulse_range_start, 0u);
    EXPECT_EQ(first->sidecar.pulse_range_count, 1u);
    EXPECT_EQ(first->sidecar.stream_id, cfg.stream_id);
    EXPECT_EQ(first->sidecar.backend_id, cfg.backend_id);
    EXPECT_EQ(first->sidecar.backend, cfg.backend);
    EXPECT_EQ(first->sidecar.payload_byte_count, cfg.samples_per_pulse * sizeof(float));
    EXPECT_TRUE(first->has_host_view);

    auto second = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(second->sidecar.sequence_id, 1u);
    EXPECT_EQ(second->sidecar.batch_id, cfg.stream_id);
    EXPECT_EQ(second->sidecar.aperture_id, 1u);
    EXPECT_EQ(second->sidecar.pulse_range_start, 1u);
    EXPECT_EQ(second->sidecar.pulse_range_count, 1u);
    EXPECT_EQ(second->sidecar.payload_byte_count, cfg.samples_per_pulse * sizeof(float));

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos->sidecar.sequence_id, 2u);
    EXPECT_EQ(eos->sidecar.batch_id, cfg.stream_id);
    EXPECT_EQ(eos->sidecar.aperture_id, 2u);
    EXPECT_EQ(eos->sidecar.pulse_range_start, 2u);
    EXPECT_EQ(eos->sidecar.pulse_range_count, 0u);
    EXPECT_EQ(eos->sidecar.payload_byte_count, 0u);
    EXPECT_TRUE(eos->has_host_view);
    EXPECT_EQ(eos->host_view.bytes, static_cast<std::uint64_t>(sizeof(float)));

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
    EXPECT_EQ(first->sidecar.sequence_id, 0u);

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);

    node.Reset();

    auto restarted = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(restarted.has_value());
    EXPECT_EQ(restarted->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(restarted->sidecar.sequence_id, 0u);
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
    EXPECT_EQ(first->sidecar.sequence_id, 0u);
    EXPECT_EQ(first->sidecar.stream_id, 11u);
    EXPECT_EQ(first->sidecar.payload_byte_count, 3u * sizeof(float));
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
    EXPECT_EQ(pulse->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(pulse->sidecar.sequence_id, 0u);
    EXPECT_EQ(pulse->sidecar.stream_id, 5u);
    EXPECT_EQ(pulse->sidecar.payload_byte_count, 2u * sizeof(float));

    auto eos = node->Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);

    auto after = node->Produce(std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(after.has_value());
}

TEST(SyntheticApertureIqSourceNodeTest, MovingTargetConfigPreservesTokenEnvelopeAndPayload) {
    sar::SyntheticApertureIqSourceConfig cfg{};
    cfg.stream_id = 3;
    cfg.total_pulses = 3;
    cfg.samples_per_pulse = 1;
    cfg.backend_id = 0;
    cfg.backend = sar::SarBackendKind::Host;
    cfg.moving_target_enabled = true;
    cfg.target_initial_range_m = 1500.0f;
    cfg.target_closing_velocity_mps = 300.0f;
    cfg.pulse_interval_s = 0.01f;
    cfg.target_reflectivity = 4.0f;

    sar::SyntheticApertureIqSourceNode node(cfg);

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    auto second = node.Produce(std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(first->sidecar.marker, sar::SarFrameMarker::Data);
    ASSERT_EQ(second->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->sidecar.payload_byte_count, second->sidecar.payload_byte_count);
    EXPECT_EQ(first->sidecar.payload_byte_count, cfg.samples_per_pulse * sizeof(float));
}

} // namespace
