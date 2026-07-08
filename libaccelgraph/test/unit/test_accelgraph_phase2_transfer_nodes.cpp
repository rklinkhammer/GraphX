// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstddef>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "accelgraph/AcceleratorSessionRegistry.hpp"
#include "accelgraph/CpuAcceleratorProvider.hpp"
#include "accelgraph/TransferGraphNodes.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/RegisteredNodeProvider.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

std::shared_ptr<accelgraph::IAcceleratorSession> CreateCpuSession() {
    auto provider = std::make_shared<accelgraph::CpuAcceleratorProvider>();
    auto session_result = provider->CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
    EXPECT_TRUE(session_result.has_value());
    return session_result.value();
}

std::vector<std::byte> BuildPayload(std::size_t size) {
    std::vector<std::byte> payload(size);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>((i * 37U + 11U) & 0xFFU);
    }
    return payload;
}

std::string PluginFilename(const std::string& stem) {
    return "lib" + stem + kSharedLibraryExtension;
}

std::filesystem::path TransferTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase2_transfer_topology.json";
}

template <typename NodeT>
std::shared_ptr<NodeT> ResolveNode(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }

        auto typed = wrapper->GetNode<NodeT>();
        if (typed) {
            return typed;
        }
    }

    return nullptr;
}

}  // namespace

TEST(AccelGraphPhase2TransferNodesTest, NodesRequireExactlyOneSessionAtInitialization) {
    accelgraph::AcceleratorSessionRegistry registry;

    accelgraph::HostIngressNode ingress;
    EXPECT_FALSE(ingress.Initialize(registry, "graph.default"));

    auto session = CreateCpuSession();
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(registry.RegisterSession("graph.default", session));
    EXPECT_TRUE(ingress.Initialize(registry, "graph.default"));

    accelgraph::AcceleratorSessionRegistry ambiguous_registry;
    ASSERT_TRUE(ambiguous_registry.RegisterSession("graph.ambiguous", CreateCpuSession()));
    ASSERT_TRUE(ambiguous_registry.RegisterSession("graph.ambiguous", CreateCpuSession()));

    accelgraph::HostToDeviceNode h2d;
    EXPECT_FALSE(h2d.Initialize(ambiguous_registry, "graph.ambiguous"));
}

TEST(AccelGraphPhase2TransferNodesTest, CpuTransferTopologyRoundTripAndRelease) {
    auto session = CreateCpuSession();
    ASSERT_NE(session, nullptr);

    accelgraph::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.RegisterSession("graph.default", session));

    accelgraph::HostIngressNode ingress;
    accelgraph::HostToDeviceNode h2d;
    accelgraph::DeviceToHostNode d2h;
    accelgraph::HostEgressNode egress;
    accelgraph::ReleaseLeaseNode release;

    ASSERT_TRUE(ingress.Initialize(registry, "graph.default"));
    ASSERT_TRUE(h2d.Initialize(registry, "graph.default"));
    ASSERT_TRUE(d2h.Initialize(registry, "graph.default"));
    ASSERT_TRUE(egress.Initialize(registry, "graph.default"));
    ASSERT_TRUE(release.Initialize(registry, "graph.default"));

    const auto input_payload = BuildPayload(256);

    auto ingress_out = ingress.Execute(input_payload, "phase2.ingress");
    ASSERT_TRUE(ingress_out.has_value());

    auto h2d_out = h2d.Execute(ingress_out->host_buffer, "phase2.h2d");
    ASSERT_TRUE(h2d_out.has_value());

    auto d2h_out = d2h.Execute(*h2d_out, "phase2.d2h");
    ASSERT_TRUE(d2h_out.has_value());

    auto output_payload = egress.Execute(d2h_out->output_host_buffer);
    ASSERT_TRUE(output_payload.has_value());
    EXPECT_EQ(output_payload.value(), input_payload);

    auto release_h2d_completion = release.Execute(h2d_out->transfer_completion);
    ASSERT_TRUE(release_h2d_completion.has_value());
    EXPECT_TRUE(release_h2d_completion->released);

    auto release_d2h_completion = release.Execute(d2h_out->transfer_completion);
    ASSERT_TRUE(release_d2h_completion.has_value());
    EXPECT_TRUE(release_d2h_completion->released);

    auto release_queue = release.Execute(h2d_out->queue);
    ASSERT_TRUE(release_queue.has_value());
    EXPECT_TRUE(release_queue->released);

    auto release_device = release.Execute(h2d_out->device_buffer);
    ASSERT_TRUE(release_device.has_value());
    EXPECT_TRUE(release_device->released);

    auto release_input_host = release.Execute(ingress_out->host_buffer);
    ASSERT_TRUE(release_input_host.has_value());
    EXPECT_TRUE(release_input_host->released);

    auto release_output_host = release.Execute(d2h_out->output_host_buffer);
    ASSERT_TRUE(release_output_host.has_value());
    EXPECT_TRUE(release_output_host->released);
}

TEST(AccelGraphPhase2TransferNodesTest, EarlyStopReleasesNodeOwnedLeases) {
    auto session = CreateCpuSession();
    ASSERT_NE(session, nullptr);

    accelgraph::AcceleratorSessionRegistry registry;
    ASSERT_TRUE(registry.RegisterSession("graph.default", session));

    accelgraph::HostIngressNode ingress;
    accelgraph::HostToDeviceNode h2d;
    accelgraph::DeviceToHostNode d2h;

    ASSERT_TRUE(ingress.Initialize(registry, "graph.default"));
    ASSERT_TRUE(h2d.Initialize(registry, "graph.default"));
    ASSERT_TRUE(d2h.Initialize(registry, "graph.default"));

    const auto input_payload = BuildPayload(64);

    auto ingress_out = ingress.Execute(input_payload, "phase2.early.ingress");
    ASSERT_TRUE(ingress_out.has_value());

    auto h2d_out = h2d.Execute(ingress_out->host_buffer, "phase2.early.h2d");
    ASSERT_TRUE(h2d_out.has_value());

    ingress.Stop();
    h2d.Stop();
    d2h.Stop();

    EXPECT_TRUE(ingress_out->host_buffer.handle.IsReleased());
    EXPECT_TRUE(h2d_out->device_buffer.handle.IsReleased());
    EXPECT_TRUE(h2d_out->queue.handle.IsReleased());
    EXPECT_TRUE(h2d_out->transfer_completion.completion.IsReleased());
    EXPECT_TRUE(h2d_out->transfer_completion.completion.Event().IsReleased());
}

TEST(AccelGraphPhase2TransferNodesTest, PluginDiscoveryAndRoundTripViaProvider) {
    auto plugin_registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, plugin_registry);

    const auto expected_plugins = std::vector<std::string>{
        PluginFilename("accelgraph_host_ingress_node"),
        PluginFilename("accelgraph_host_to_device_node"),
        PluginFilename("accelgraph_device_to_host_node"),
        PluginFilename("accelgraph_host_egress_node"),
        PluginFilename("accelgraph_release_lease_node"),
    };

    for (const auto& plugin : expected_plugins) {
        ASSERT_TRUE(std::filesystem::exists(std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY) / plugin));
    }

    auto summary = loader.LoadAllPluginsSafe();
    ASSERT_TRUE(summary.has_value());
    EXPECT_GE(summary->loaded_count, expected_plugins.size());

    graph::RegisteredNodeProvider provider(plugin_registry);
    provider.Initialize();

    EXPECT_TRUE(provider.IsNodeTypeAvailable("HostIngressNode"));
    EXPECT_TRUE(provider.IsNodeTypeAvailable("HostToDeviceNode"));
    EXPECT_TRUE(provider.IsNodeTypeAvailable("DeviceToHostNode"));
    EXPECT_TRUE(provider.IsNodeTypeAvailable("HostEgressNode"));
    EXPECT_TRUE(provider.IsNodeTypeAvailable("ReleaseLeaseNode"));

    auto ingress_adapter = provider.CreateNodeExpected("HostIngressNode");
    auto h2d_adapter = provider.CreateNodeExpected("HostToDeviceNode");
    auto d2h_adapter = provider.CreateNodeExpected("DeviceToHostNode");
    auto egress_adapter = provider.CreateNodeExpected("HostEgressNode");
    auto release_adapter = provider.CreateNodeExpected("ReleaseLeaseNode");

    ASSERT_TRUE(ingress_adapter.has_value());
    ASSERT_TRUE(h2d_adapter.has_value());
    ASSERT_TRUE(d2h_adapter.has_value());
    ASSERT_TRUE(egress_adapter.has_value());
    ASSERT_TRUE(release_adapter.has_value());

    auto ingress = ingress_adapter->GetNode<accelgraph::HostIngressNode>();
    auto h2d = h2d_adapter->GetNode<accelgraph::HostToDeviceNode>();
    auto d2h = d2h_adapter->GetNode<accelgraph::DeviceToHostNode>();
    auto egress = egress_adapter->GetNode<accelgraph::HostEgressNode>();
    auto release = release_adapter->GetNode<accelgraph::ReleaseLeaseNode>();

    ASSERT_NE(ingress, nullptr);
    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(d2h, nullptr);
    ASSERT_NE(egress, nullptr);
    ASSERT_NE(release, nullptr);

    auto session = CreateCpuSession();
    ASSERT_NE(session, nullptr);

    accelgraph::AcceleratorSessionRegistry session_registry;
    ASSERT_TRUE(session_registry.RegisterSession("graph.default", session));

    ASSERT_TRUE(ingress->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(h2d->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(d2h->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(egress->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(release->Initialize(session_registry, "graph.default"));

    const auto payload = BuildPayload(192);

    auto ingress_out = ingress->Execute(payload, "phase2.plugin.ingress");
    ASSERT_TRUE(ingress_out.has_value());

    auto h2d_out = h2d->Execute(ingress_out->host_buffer, "phase2.plugin.h2d");
    ASSERT_TRUE(h2d_out.has_value());

    auto d2h_out = d2h->Execute(*h2d_out, "phase2.plugin.d2h");
    ASSERT_TRUE(d2h_out.has_value());

    auto egress_payload = egress->Execute(d2h_out->output_host_buffer);
    ASSERT_TRUE(egress_payload.has_value());
    EXPECT_EQ(egress_payload.value(), payload);

    ASSERT_TRUE(release->Execute(h2d_out->transfer_completion).has_value());
    ASSERT_TRUE(release->Execute(d2h_out->transfer_completion).has_value());
    ASSERT_TRUE(release->Execute(h2d_out->queue).has_value());
    ASSERT_TRUE(release->Execute(h2d_out->device_buffer).has_value());
    ASSERT_TRUE(release->Execute(ingress_out->host_buffer).has_value());
    ASSERT_TRUE(release->Execute(d2h_out->output_host_buffer).has_value());
}

TEST(AccelGraphPhase2TransferNodesTest, JsonTopologyLoadsThroughGraphExecutorBuilder) {
    const auto config_path = TransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto plugin_dir = std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    EXPECT_EQ(graph_manager->GetNodes().size(), 5u);
    EXPECT_EQ(graph_manager->GetEdges().size(), 3u);

    auto ingress = ResolveNode<accelgraph::HostIngressNode>(graph_manager);
    auto h2d = ResolveNode<accelgraph::HostToDeviceNode>(graph_manager);
    auto d2h = ResolveNode<accelgraph::DeviceToHostNode>(graph_manager);
    auto egress = ResolveNode<accelgraph::HostEgressNode>(graph_manager);
    auto release = ResolveNode<accelgraph::ReleaseLeaseNode>(graph_manager);

    ASSERT_NE(ingress, nullptr);
    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(d2h, nullptr);
    ASSERT_NE(egress, nullptr);
    ASSERT_NE(release, nullptr);

    auto session = CreateCpuSession();
    ASSERT_NE(session, nullptr);

    accelgraph::AcceleratorSessionRegistry session_registry;
    ASSERT_TRUE(session_registry.RegisterSession("graph.default", session));

    ASSERT_TRUE(ingress->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(h2d->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(d2h->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(egress->Initialize(session_registry, "graph.default"));
    ASSERT_TRUE(release->Initialize(session_registry, "graph.default"));

    const auto payload = BuildPayload(128);

    auto ingress_out = ingress->Execute(payload, "phase2.executor.ingress");
    ASSERT_TRUE(ingress_out.has_value());

    auto h2d_out = h2d->Execute(ingress_out->host_buffer, "phase2.executor.h2d");
    ASSERT_TRUE(h2d_out.has_value());

    auto d2h_out = d2h->Execute(*h2d_out, "phase2.executor.d2h");
    ASSERT_TRUE(d2h_out.has_value());

    auto egress_payload = egress->Execute(d2h_out->output_host_buffer);
    ASSERT_TRUE(egress_payload.has_value());
    EXPECT_EQ(egress_payload.value(), payload);

    ASSERT_TRUE(release->Execute(h2d_out->transfer_completion).has_value());
    ASSERT_TRUE(release->Execute(d2h_out->transfer_completion).has_value());
    ASSERT_TRUE(release->Execute(h2d_out->queue).has_value());
    ASSERT_TRUE(release->Execute(h2d_out->device_buffer).has_value());
    ASSERT_TRUE(release->Execute(ingress_out->host_buffer).has_value());
    ASSERT_TRUE(release->Execute(d2h_out->output_host_buffer).has_value());
}
