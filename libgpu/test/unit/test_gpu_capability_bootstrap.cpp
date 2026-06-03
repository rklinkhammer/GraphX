#include <gtest/gtest.h>

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include "graph/CapabilityBus.hpp"

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"

namespace {

TEST(GpuCapabilityBootstrap, RegistersExpectedCapabilitiesFromFeatureFlags) {
    graph::CapabilityBus bus;

    graph::gpu::RegisterDefaultGpuCapabilities(bus);

#if GRAPHX_ENABLE_CUDA_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    constexpr bool expect_cuda = true;
#else
    constexpr bool expect_cuda = false;
#endif

#if GRAPHX_ENABLE_SYCL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    constexpr bool expect_sycl = true;
#else
    constexpr bool expect_sycl = false;
#endif

    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaContextCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaTransferCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaKernelCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaTelemetryCapability>(), expect_cuda);
    EXPECT_EQ(bus.Has<graph::gpu::cuda::capabilities::ICudaCollectiveCapability>(), expect_cuda);

    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclContextCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclTransferCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclKernelCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclTelemetryCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclCollectiveCapability>(), expect_sycl);
}

TEST(GpuCapabilityBootstrap, ExplicitDisablePreventsRegistration) {
    graph::CapabilityBus bus;

    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_sycl = false;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    EXPECT_FALSE(bus.Has<graph::gpu::cuda::capabilities::ICudaContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::sycl::capabilities::ISyclContextCapability>());
}

} // namespace
