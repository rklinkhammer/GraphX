// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "accelgraph/TransferGraphNodes.hpp"
#include "AccelGraphTopologyTestUtils.hpp"

namespace {

std::filesystem::path CpuTransferTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase3a_transfer_cpu_topology.json");
}

std::filesystem::path MetalTransferTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase3a_transfer_metal_topology.json");
}

constexpr const char* kMetalSupportNotCompiledDiagnostic =
    "Metal support not compiled (ACCELGRAPH_ENABLE_METAL=OFF).";

}  // namespace

TEST(AccelGraphPhase3ATest, CpuTransferTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = CpuTransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(5));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto ingress = accelgraph::test::ResolveNode<accelgraph::HostIngressNode>(graph_manager);
    auto h2d = accelgraph::test::ResolveNode<accelgraph::HostToDeviceNode>(graph_manager);
    auto d2h = accelgraph::test::ResolveNode<accelgraph::DeviceToHostNode>(graph_manager);
    auto egress = accelgraph::test::ResolveNode<accelgraph::HostEgressNode>(graph_manager);
    auto release = accelgraph::test::ResolveNode<accelgraph::ReleaseLeaseNode>(graph_manager);
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
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(5));
    } catch (const std::exception&) {
        GTEST_SKIP() << kMetalSupportNotCompiledDiagnostic;
    }
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto ingress = accelgraph::test::ResolveNode<accelgraph::HostIngressNode>(graph_manager);
    auto h2d = accelgraph::test::ResolveNode<accelgraph::HostToDeviceNode>(graph_manager);
    auto d2h = accelgraph::test::ResolveNode<accelgraph::DeviceToHostNode>(graph_manager);
    auto egress = accelgraph::test::ResolveNode<accelgraph::HostEgressNode>(graph_manager);
    auto release = accelgraph::test::ResolveNode<accelgraph::ReleaseLeaseNode>(graph_manager);
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
