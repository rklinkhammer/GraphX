// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "accelgraph/TransferGraphNodes.hpp"
#include "AccelGraphTopologyTestUtils.hpp"

namespace {

std::filesystem::path CudaTransferTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase5_transfer_cuda_topology.json");
}

}  // namespace

TEST(AccelGraphPhase5CudaGraphExecutorTest, CudaTransferTopologyExecutesViaGraphExecutorOrSkipsWithExactDiagnostic) {
#if !ACCELGRAPH_ENABLE_CUDA
    GTEST_SKIP() << accelgraph::kCudaSupportNotCompiledDiagnostic;
#endif

    const auto config_path = CudaTransferTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto ingress = accelgraph::test::ResolveNode<accelgraph::HostIngressNode>(graph_manager);
    auto h2d = accelgraph::test::ResolveNode<accelgraph::HostToDeviceNode>(graph_manager);
    auto d2h = accelgraph::test::ResolveNode<accelgraph::DeviceToHostNode>(graph_manager);
    auto egress = accelgraph::test::ResolveNode<accelgraph::HostEgressNode>(graph_manager);
    ASSERT_NE(ingress, nullptr);
    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(d2h, nullptr);
    ASSERT_NE(egress, nullptr);

    try {
        const auto run_result = executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedCudaDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}
