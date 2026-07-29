// SPDX-License-Identifier: MIT

/**
 * @file test_gpu_capability_bootstrap.cpp
 * @brief Test GPU Capability Bootstrap GPU acceleration support.
 *
 * @details Provides GPU test coverage for accelerator contracts and runtime behavior. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include "gpu/accel/types/AccelValidation.hpp"
#include "graph/CapabilityBus.hpp"

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/nodes/DeviceShardNodeMetal.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#endif
#include <array>
#include <memory>
#include <stdexcept>

namespace {

TEST(GpuCapabilityBootstrap, RegistersExpectedCapabilitiesFromFeatureFlags) {
    graph::CapabilityBus bus;

    graph::gpu::RegisterDefaultGpuCapabilities(bus);

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    constexpr bool expect_cuda = true;
#else
    constexpr bool expect_cuda = false;
#endif

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    constexpr bool expect_metal = true;
#else
    constexpr bool expect_metal = false;
#endif

    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaContextCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaTransferCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaKernelCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaTelemetryCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaCollectiveCapability>(), expect_cuda);

    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalContextCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalTransferCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalKernelCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(), expect_metal);
    EXPECT_EQ(bus.Has<graph::gpu::metal::capabilities::IMetalCollectiveCapability>(), expect_metal);
}

TEST(GpuCapabilityBootstrap, MetalNodesUseBootstrappedSharedQueue) {
    graph::CapabilityBus bus;

    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_metal = true;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    auto shared_queue = bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    ASSERT_NE(shared_queue, nullptr);
    const auto shared_queue_id = shared_queue->GetOrCreateQueueId();
    ASSERT_NE(shared_queue_id, 0U);

    graph::gpu::metal::nodes::H2DAsyncNodeMetal h2d;
    graph::gpu::metal::nodes::D2HAsyncNodeMetal d2h;
    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;

    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));

    const auto h2d_queue_id = h2d.GetParameters().TryGetInt("queue_id");
    ASSERT_TRUE(h2d_queue_id.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(h2d_queue_id.value()), shared_queue_id);

    const auto d2h_queue_id = d2h.GetParameters().TryGetInt("queue_id");
    ASSERT_TRUE(d2h_queue_id.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(d2h_queue_id.value()), shared_queue_id);

    const auto shard_queue_id = shard.GetParameters().TryGetInt("queue_id");
    ASSERT_TRUE(shard_queue_id.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(shard_queue_id.value()), shared_queue_id);
#else
    GTEST_SKIP() << "Metal graph nodes are disabled in this build configuration.";
#endif
}

TEST(GpuCapabilityBootstrap, DefaultMetalContextEventWaitSemantics) {
    graph::gpu::metal::capabilities::DefaultMetalContextCapability context;

    const auto event_id = context.CreateEvent();
    ASSERT_NE(event_id, 0U);
    EXPECT_FALSE(context.IsEventComplete(event_id));
    EXPECT_TRUE(context.WaitEvent(event_id, 0U));
    EXPECT_TRUE(context.IsEventComplete(event_id));

    context.DestroyEvent(event_id);
    EXPECT_FALSE(context.IsEventComplete(event_id));
    EXPECT_FALSE(context.WaitEvent(event_id, 0U));
}

TEST(GpuCapabilityBootstrap, ExplicitDisablePreventsRegistration) {
    graph::CapabilityBus bus;

    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_metal = false;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    EXPECT_FALSE(bus.Has<graph::gpu::cuda::capabilities::ICudaContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::metal::capabilities::IMetalContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>());
}

TEST(GpuCapabilityBootstrap, StrictNativeMetalRequirementBehavior) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_metal = true;
    options.require_native_metal_runtime = true;

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    const bool native_available = graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
    if (native_available) {
        EXPECT_NO_THROW(graph::gpu::RegisterDefaultGpuCapabilities(bus, options));
        auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
        ASSERT_NE(context, nullptr);
        auto native_context = std::dynamic_pointer_cast<
            graph::gpu::metal::capabilities::NativeMetalContextCapability>(context);
        EXPECT_NE(native_context, nullptr);
    } else {
        EXPECT_THROW(graph::gpu::RegisterDefaultGpuCapabilities(bus, options), std::runtime_error);
    }
#else
    EXPECT_THROW(graph::gpu::RegisterDefaultGpuCapabilities(bus, options), std::runtime_error);
#endif
#else
    EXPECT_THROW(graph::gpu::RegisterDefaultGpuCapabilities(bus, options), std::runtime_error);
#endif
}

} // namespace
