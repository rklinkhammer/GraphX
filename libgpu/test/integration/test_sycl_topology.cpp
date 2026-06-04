// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#include "gpu/sycl/nodes/D2HAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/H2DAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/HostEgressSinkNodeSycl.hpp"
#include "gpu/sycl/nodes/HostIngressPinnedSourceNodeSycl.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

TEST(GpuSyclIntegration, DynamicPluginRoundTripUsesRuntimeBackedCapabilities) {
#if !GRAPHX_HAS_SYCL_RUNTIME
    GTEST_SKIP() << "SYCL runtime not available in this build";
#else
    auto graph_manager = std::make_shared<graph::GraphManager>();
    auto factory = test::PluginInfrastructure::GetFactory();

    auto ingress = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostIngressPinnedSourceNodeSycl"));
    auto h2d = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "H2DAsyncNodeSycl"));
    auto d2h = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "D2HAsyncNodeSycl"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostEgressSinkNodeSycl"));

    auto ingress_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(ingress);
    auto h2d_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(h2d);
    auto d2h_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(d2h);
    auto sink_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph_manager->AddNode(ingress_wrapper);
    graph_manager->AddNode(h2d_wrapper);
    graph_manager->AddNode(d2h_wrapper);
    graph_manager->AddNode(sink_wrapper);

    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::H2DAsyncNodeSycl, 0>(
        graph_manager,
        ingress_wrapper,
        h2d_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::H2DAsyncNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::D2HAsyncNodeSycl, 0>(
        graph_manager,
        h2d_wrapper,
        d2h_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::D2HAsyncNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::HostEgressSinkNodeSycl, 0>(
        graph_manager,
        d2h_wrapper,
        sink_wrapper)));

    auto ingress_node = ingress_wrapper->GetNode<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl>();
    auto sink_node = sink_wrapper->GetNode<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl>();
    ASSERT_NE(ingress_node, nullptr);
    ASSERT_NE(sink_node, nullptr);

    constexpr std::uint64_t kBytes = 4096;
    ingress_node->StageNextBufferBytes(kBytes);
    sink_node->SetExpectedMessageCount(1);

    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph_manager)
                        .Build();

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message << " " << init_result.error_details;

    EXPECT_TRUE(executor->Has<graph::gpu::sycl::capabilities::ISyclContextCapability>());
    EXPECT_TRUE(executor->Has<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>());
    EXPECT_TRUE(executor->Has<graph::gpu::sycl::capabilities::ISyclTransferCapability>());

    auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message << " " << start_result.error_details;

    auto run_result = executor->Run();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto stop_result = executor->Stop();
    EXPECT_TRUE(stop_result.success) << stop_result.message << " " << stop_result.error_details;

    auto join_result = executor->Join();
    EXPECT_TRUE(join_result.success) << join_result.message << " " << join_result.error_details;

    EXPECT_EQ(sink_node->ConsumeCount(), 1U);
    EXPECT_EQ(sink_node->LastView().backend, graph::gpu::accel::BackendKind::SYCL);
    EXPECT_EQ(sink_node->LastView().bytes, kBytes);

    const auto* sink_data = static_cast<const std::byte*>(sink_node->LastView().host_ptr);
    ASSERT_NE(sink_data, nullptr);
    for (std::uint64_t index = 0; index < kBytes; ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(sink_data[index]), 0U);
    }
#endif
}

} // namespace
