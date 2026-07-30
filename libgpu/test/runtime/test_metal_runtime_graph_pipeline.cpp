// SPDX-License-Identifier: MIT

/**
 * @file test_metal_runtime_graph_pipeline.cpp
 * @brief Test Metal Runtime Graph Pipeline GPU acceleration support.
 *
 * @details Provides GPU test coverage for accelerator contracts and runtime behavior. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <future>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphBuilder.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceReduceNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceShardNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceTransformNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/HostEgressSinkNodeMetal.hpp"
#include "gpu/metal/nodes/HostIngressPinnedSourceNodeMetal.hpp"
#include "gpu/metal/nodes/LeaseReleaseNodeMetal.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

struct MetalPipelineNodes {
    std::shared_ptr<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal> ingress;
    std::shared_ptr<graph::gpu::metal::nodes::H2DAsyncNodeMetal> h2d;
    std::shared_ptr<graph::gpu::metal::nodes::DeviceShardNodeMetal> shard;
    std::shared_ptr<graph::gpu::metal::nodes::DeviceTransformNodeMetal> transform;
    std::shared_ptr<graph::gpu::metal::nodes::DeviceReduceNodeMetal> reduce;
    std::shared_ptr<graph::gpu::metal::nodes::D2HAsyncNodeMetal> d2h;
    std::shared_ptr<graph::gpu::metal::nodes::HostEgressSinkNodeMetal> egress;

    [[nodiscard]] bool IsComplete() const {
        return ingress != nullptr && h2d != nullptr && shard != nullptr &&
               transform != nullptr && reduce != nullptr && d2h != nullptr &&
               egress != nullptr;
    }
};

MetalPipelineNodes ResolveMetalPipelineNodes(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    MetalPipelineNodes nodes{};
    if (graph_manager == nullptr) {
        return nodes;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (wrapper == nullptr) {
            continue;
        }

        if (!nodes.ingress) {
            nodes.ingress = wrapper->GetNode<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal>();
        }
        if (!nodes.h2d) {
            nodes.h2d = wrapper->GetNode<graph::gpu::metal::nodes::H2DAsyncNodeMetal>();
        }
        if (!nodes.shard) {
            nodes.shard = wrapper->GetNode<graph::gpu::metal::nodes::DeviceShardNodeMetal>();
        }
        if (!nodes.transform) {
            nodes.transform = wrapper->GetNode<graph::gpu::metal::nodes::DeviceTransformNodeMetal>();
        }
        if (!nodes.reduce) {
            nodes.reduce = wrapper->GetNode<graph::gpu::metal::nodes::DeviceReduceNodeMetal>();
        }
        if (!nodes.d2h) {
            nodes.d2h = wrapper->GetNode<graph::gpu::metal::nodes::D2HAsyncNodeMetal>();
        }
        if (!nodes.egress) {
            nodes.egress = wrapper->GetNode<graph::gpu::metal::nodes::HostEgressSinkNodeMetal>();
        }

        if (nodes.IsComplete()) {
            break;
        }
    }

    return nodes;
}

/**
 * @brief Get vibration health pipeline json config path.
 */
std::filesystem::path GetVibrationHealthPipelineJsonConfigPath() {
    const auto test_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    return test_root / "config" / "topologies" / "metal_vibration_health_pipeline.json";
}

[[nodiscard]] bool NativeMetalRuntimeAvailableForTest() {
    return graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
}

}  // namespace

TEST(MetalNativeRuntimeGraphPipelineTest, VibrationHealthPipelineRunsEndToEnd) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    const bool native_available = NativeMetalRuntimeAvailableForTest();
#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    ASSERT_TRUE(native_available)
        << "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal runtime unavailable: "
        << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#else
    if (!native_available) {
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
        return;
    }
#endif

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);

    ASSERT_TRUE(context->SelectDevice(0U));
    const auto queue_id = context->CreateCommandQueue();
    ASSERT_NE(queue_id, 0U);

    graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal ingress;
    graph::gpu::metal::nodes::H2DAsyncNodeMetal h2d;
    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;
    graph::gpu::metal::nodes::DeviceTransformNodeMetal transform;
    graph::gpu::metal::nodes::DeviceReduceNodeMetal reduce;
    graph::gpu::metal::nodes::D2HAsyncNodeMetal d2h;
    graph::gpu::metal::nodes::HostEgressSinkNodeMetal egress;
    graph::gpu::metal::nodes::LeaseReleaseNodeMetal lease_release;

    ASSERT_TRUE(ingress.BindGpuCapabilities(bus));
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));
    ASSERT_TRUE(transform.BindGpuCapabilities(bus));
    ASSERT_TRUE(reduce.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));
    ASSERT_TRUE(lease_release.BindGpuCapabilities(bus));

    h2d.SetQueueAndDevice(queue_id, 0U);
    d2h.SetQueue(queue_id);

    // 3-axis accelerometer window encoded as bytes (X,Y,Z interleaved by contiguous blocks).
    constexpr std::uint64_t kFrameBytes = 96;
    graph::gpu::accel::HostPinnedBufferView ingress_view{};
    graph::gpu::accel::BufferLease ingress_lease{};
    ASSERT_TRUE(ingress.ProduceForTest(kFrameBytes, ingress_view, ingress_lease));

    auto* ingress_bytes = static_cast<std::byte*>(ingress_view.host_ptr);
    ASSERT_NE(ingress_bytes, nullptr);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kFrameBytes); ++i) {
        ingress_bytes[i] = std::byte{static_cast<std::uint8_t>((i * 11U) & 0xFFU)};
    }

    auto device_frame = h2d.Transfer(
        ingress_view,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_frame.has_value());
    device_frame->execution_queue_id = queue_id;

    shard.ConfigureShard(1U, 3U);
    auto sharded = shard.Transfer(
        *device_frame,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(sharded.has_value());
    ASSERT_EQ(sharded->bytes, 32U);

    transform.ConfigureKernel(10001U, "graphx_transform_xor_u8_inplace", 0U, queue_id);
    auto transformed = transform.Transfer(
        *sharded,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(transformed.has_value());

    reduce.ConfigureKernel(10002U, "graphx_reduce_health_metrics_u8", 0U, queue_id);
    auto reduced = reduce.Transfer(
        *transformed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(reduced.has_value());
    ASSERT_EQ(reduced->bytes, 8U);

    auto host_out = d2h.Transfer(
        *reduced,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_out.has_value());
    ASSERT_TRUE(egress.ConsumeForTest(*host_out));
    EXPECT_EQ(egress.ConsumeCount(), 1U);

    const auto* metrics = static_cast<const std::uint32_t*>(egress.LastView().host_ptr);
    ASSERT_NE(metrics, nullptr);

    std::uint64_t sumsq = 0;
    std::uint32_t peak = 0;
    for (std::size_t i = 0; i < 32U; ++i) {
        const auto raw = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(ingress_bytes[32U + i]));
        const auto transformed_value = static_cast<std::uint32_t>(raw ^ 0xA5U);
        sumsq += static_cast<std::uint64_t>(transformed_value * transformed_value);
        peak = std::max(peak, transformed_value);
    }

    const auto expected_rms = static_cast<std::uint32_t>(
        std::sqrt(static_cast<double>(sumsq) / 32.0));
    EXPECT_EQ(metrics[0], expected_rms);
    EXPECT_EQ(metrics[1], peak);

    // Explicitly exercise lease release at the end of the pipeline lifecycle.
    EXPECT_TRUE(lease_release.ConsumeForTest(ingress_lease));

    context->DestroyCommandQueue(queue_id);
}

TEST(MetalNativeRuntimeGraphPipelineTest,
     JsonLoadedVibrationHealthPipelineRunsEndToEnd) {
    const bool native_available = NativeMetalRuntimeAvailableForTest();
#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    ASSERT_TRUE(native_available)
        << "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal runtime unavailable: "
        << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#else
    if (!native_available) {
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
        return;
    }
#endif

    const auto config_path = GetVibrationHealthPipelineJsonConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto capability = std::make_shared<capabilities::GraphCapability>();
    capability->SetNodeProvider(test::PluginInfrastructure::GetProvider());
    capability->SetJsonConfigPath(config_path.string());

    app::GraphBuilder builder(capability);
    const auto build_result = builder.Build();

    ASSERT_TRUE(build_result.success) << build_result.error_message;
    ASSERT_NE(build_result.graph, nullptr);
    EXPECT_EQ(build_result.node_count, 7U);
    EXPECT_EQ(build_result.edge_count, 6U);

    auto nodes = ResolveMetalPipelineNodes(build_result.graph);
    ASSERT_TRUE(nodes.IsComplete());

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->SelectDevice(0U));
    const auto queue_id = context->CreateCommandQueue();
    ASSERT_NE(queue_id, 0U);

    ASSERT_TRUE(nodes.ingress->BindGpuCapabilities(bus));
    ASSERT_TRUE(nodes.h2d->BindGpuCapabilities(bus));
    ASSERT_TRUE(nodes.shard->BindGpuCapabilities(bus));
    ASSERT_TRUE(nodes.transform->BindGpuCapabilities(bus));
    ASSERT_TRUE(nodes.reduce->BindGpuCapabilities(bus));
    ASSERT_TRUE(nodes.d2h->BindGpuCapabilities(bus));

    nodes.h2d->SetQueueAndDevice(queue_id, 0U);
    nodes.d2h->SetQueue(queue_id);

    graph::gpu::accel::HostPinnedBufferView ingress_view{};
    auto produced = nodes.ingress->Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(produced.has_value());
    ingress_view = *produced;

    auto* ingress_bytes = static_cast<std::byte*>(ingress_view.host_ptr);
    ASSERT_NE(ingress_bytes, nullptr);
    for (std::size_t i = 0; i < static_cast<std::size_t>(ingress_view.bytes); ++i) {
        ingress_bytes[i] = std::byte{static_cast<std::uint8_t>((i * 11U) & 0xFFU)};
    }

    auto device_frame = nodes.h2d->Transfer(
        ingress_view,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_frame.has_value());
    device_frame->execution_queue_id = queue_id;

    auto sharded = nodes.shard->Transfer(
        *device_frame,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(sharded.has_value());
    ASSERT_EQ(sharded->bytes, 32U);

    // Queue ids are runtime-assigned, so bind kernels to the live queue here.
    nodes.transform->ConfigureKernel(10001U, "graphx_transform_xor_u8_inplace", 0U, queue_id);
    auto transformed = nodes.transform->Transfer(
        *sharded,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(transformed.has_value());

    nodes.reduce->ConfigureKernel(10002U, "graphx_reduce_health_metrics_u8", 0U, queue_id);
    auto reduced = nodes.reduce->Transfer(
        *transformed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(reduced.has_value());
    ASSERT_EQ(reduced->bytes, 8U);

    auto host_out = nodes.d2h->Transfer(
        *reduced,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_out.has_value());
    ASSERT_TRUE(nodes.egress->ConsumeForTest(*host_out));
    EXPECT_EQ(nodes.egress->ConsumeCount(), 1U);

    const auto* metrics = static_cast<const std::uint32_t*>(nodes.egress->LastView().host_ptr);
    ASSERT_NE(metrics, nullptr);

    std::uint64_t sumsq = 0;
    std::uint32_t peak = 0;
    for (std::size_t i = 0; i < 32U; ++i) {
        const auto raw = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(ingress_bytes[32U + i]));
        const auto transformed_value = static_cast<std::uint32_t>(raw ^ 0xA5U);
        sumsq += static_cast<std::uint64_t>(transformed_value * transformed_value);
        peak = std::max(peak, transformed_value);
    }

    const auto expected_rms = static_cast<std::uint32_t>(
        std::sqrt(static_cast<double>(sumsq) / 32.0));
    EXPECT_EQ(metrics[0], expected_rms);
    EXPECT_EQ(metrics[1], peak);

    context->DestroyCommandQueue(queue_id);
}

TEST(MetalNativeRuntimeGraphPipelineTest,
     JsonLoadedVibrationHealthPipelineRunsEndToEndWithGraphExecutor) {
    const bool native_available = NativeMetalRuntimeAvailableForTest();
#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    ASSERT_TRUE(native_available)
        << "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal runtime unavailable: "
        << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#else
    if (!native_available) {
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
        return;
    }
#endif

    const auto config_path = GetVibrationHealthPipelineJsonConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(PLUGIN_OUTPUT_DIRECTORY)
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 7U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 6U);

    auto nodes = ResolveMetalPipelineNodes(executor->GetGraphManager());
    ASSERT_TRUE(nodes.IsComplete());

    const auto ingress_param_names = nodes.ingress->GetParameterNames();
    EXPECT_NE(std::find(ingress_param_names.begin(), ingress_param_names.end(), "staged_bytes"),
              ingress_param_names.end());
    const auto ingress_params = nodes.ingress->GetParameters();
    const auto ingress_staged_bytes = ingress_params.TryGetInt("staged_bytes");
    ASSERT_TRUE(ingress_staged_bytes.has_value());
    EXPECT_EQ(ingress_staged_bytes.value(), 96);
    const auto ingress_staged_desc = nodes.ingress->GetParameterDescription("staged_bytes");
    const auto ingress_staged_type = ingress_staged_desc.TryGetString("type");
    ASSERT_TRUE(ingress_staged_type.has_value());
    EXPECT_EQ(ingress_staged_type.value(), "integer");

    const auto shard_params = nodes.shard->GetParameters();
    const auto shard_index = shard_params.TryGetInt("shard_index");
    const auto shard_count = shard_params.TryGetInt("shard_count");
    ASSERT_TRUE(shard_index.has_value());
    ASSERT_TRUE(shard_count.has_value());
    EXPECT_EQ(shard_index.value(), 1);
    EXPECT_EQ(shard_count.value(), 3);

    const auto egress_params = nodes.egress->GetParameters();
    const auto expected_message_count = egress_params.TryGetInt("expected_message_count");
    ASSERT_TRUE(expected_message_count.has_value());
    EXPECT_EQ(expected_message_count.value(), 1);

    const auto transform_params = nodes.transform->GetParameters();
    const auto transform_kernel_desc_obj = transform_params.TryGetObject("kernel_descriptor");
    ASSERT_TRUE(transform_kernel_desc_obj.has_value());
    const auto transform_function_name = transform_kernel_desc_obj->TryGetString("function_name");
    ASSERT_TRUE(transform_function_name.has_value());
    EXPECT_EQ(transform_function_name.value(), "graphx_transform_xor_u8_inplace");

    const auto reduce_params = nodes.reduce->GetParameters();
    const auto reduce_kernel_desc_obj = reduce_params.TryGetObject("kernel_descriptor");
    ASSERT_TRUE(reduce_kernel_desc_obj.has_value());
    const auto reduce_function_name = reduce_kernel_desc_obj->TryGetString("function_name");
    ASSERT_TRUE(reduce_function_name.has_value());
    EXPECT_EQ(reduce_function_name.value(), "graphx_reduce_health_metrics_u8");

    auto context = executor->GetCapability<graph::gpu::metal::capabilities::IMetalContextCapability>();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->SelectDevice(0U));

    const auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message << " " << start_result.error_details;

    auto run_future = std::async(std::launch::async, [&executor]() {
        return executor->Run();
    });
    constexpr auto kRunTimeout = std::chrono::seconds(8);
    if (run_future.wait_for(kRunTimeout) != std::future_status::ready) {
        const auto timed_stop_result = executor->Stop();
        const auto timed_join_result = executor->Join();
        const auto timed_run_result = run_future.get();
        FAIL() << "GraphExecutor::Run() timed out in Metal runtime test. stop="
               << timed_stop_result.success << " join=" << timed_join_result.success
               << " run=" << timed_run_result.success;
    }
    const auto run_result = run_future.get();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    const auto stop_result = executor->Stop();
    ASSERT_TRUE(stop_result.success) << stop_result.message << " " << stop_result.error_details;

    const auto join_result = executor->Join();
    ASSERT_TRUE(join_result.success) << join_result.message << " " << join_result.error_details;

    EXPECT_TRUE(executor->IsCompletionSignaled());
    EXPECT_EQ(nodes.egress->ConsumeCount(), 1U);

    const auto last_view = nodes.egress->LastView();
    EXPECT_NE(last_view.host_ptr, nullptr);
    EXPECT_EQ(last_view.bytes, 8U);
}
