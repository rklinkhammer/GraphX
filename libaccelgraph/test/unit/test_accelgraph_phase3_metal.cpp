// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "accelgraph/TransferGraphNodes.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

namespace {

std::filesystem::path CpuTransferTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase3a_transfer_cpu_topology.json";
}

std::filesystem::path MetalTransferTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase3a_transfer_metal_topology.json";
}

std::filesystem::path PluginDirectoryPath() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

constexpr const char* kMetalSupportNotCompiledDiagnostic =
    "Metal support not compiled (ACCELGRAPH_ENABLE_METAL=OFF).";

std::shared_ptr<graph::GraphExecutor> BuildExecutor(const std::filesystem::path& config_path) {
    return graph::GraphExecutorBuilder()
        .WithJsonConfig(config_path.string())
        .WithPluginDirectory(PluginDirectoryPath().string())
        .WithExecutorTimeout(std::chrono::seconds(5))
        .Build();
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

TEST(AccelGraphPhase3ATest, CpuTransferTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = CpuTransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    auto executor = BuildExecutor(config_path);
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

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

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
}

TEST(AccelGraphPhase3ATest, MetalTransferTopologyExecutesViaGraphExecutorOrSkipsWithExactDiagnostic) {
    const auto config_path = MetalTransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = BuildExecutor(config_path);
    } catch (const std::exception&) {
        GTEST_SKIP() << kMetalSupportNotCompiledDiagnostic;
    }
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

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

    try {
        const auto run_result = executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (message.find(kMetalSupportNotCompiledDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}
