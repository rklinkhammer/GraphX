#include <gtest/gtest.h>

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"

#include "gpu/accel/types/AccelValidation.hpp"
#include "graph/CapabilityBus.hpp"

#include "gpu/cuda/capabilities/ICudaCapabilities.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#endif
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#include "gpu/sycl/capabilities/ISyclCapabilities.hpp"

#include <array>
#include <memory>
#include <stdexcept>

namespace {

graph::gpu::accel::HostPinnedBufferView MakeValidSyclHostView(void* host_ptr) {
    graph::gpu::accel::HostPinnedBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::SYCL;
    view.host_ptr = host_ptr;
    view.bytes = 64;
    view.dtype = graph::gpu::accel::DataType::UInt8;
    view.layout.rank = 1;
    view.layout.shape[0] = 64;
    view.layout.stride[0] = 1;
    view.allocator_id = 7;
    return view;
}

graph::gpu::accel::DeviceBufferView MakeValidSyclDeviceView(void* device_ptr) {
    graph::gpu::accel::DeviceBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::SYCL;
    view.device_ptr = device_ptr;
    view.bytes = 64;
    view.dtype = graph::gpu::accel::DataType::UInt8;
    view.layout.rank = 1;
    view.layout.shape[0] = 64;
    view.layout.stride[0] = 1;
    view.device_id = 2;
    view.execution_queue_id = 11;
    return view;
}

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

    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclContextCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclTransferCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclKernelCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclTelemetryCapability>(), expect_sycl);
    EXPECT_EQ(bus.Has<graph::gpu::sycl::capabilities::ISyclCollectiveCapability>(), expect_sycl);

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
    options.enable_sycl = false;
    options.enable_metal = true;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

#if GRAPHX_ENABLE_METAL_GRAPH_NODES || GRAPHX_GPU_STUB_BACKENDS
    auto shared_queue = bus.Get<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>();
    ASSERT_NE(shared_queue, nullptr);
    const auto shared_queue_id = shared_queue->GetOrCreateQueueId();
    ASSERT_NE(shared_queue_id, 0U);

    graph::gpu::metal::nodes::H2DAsyncNodeMetal h2d;
    graph::gpu::metal::nodes::D2HAsyncNodeMetal d2h;

    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));

    const auto h2d_queue_id = h2d.GetParameters().TryGetInt("queue_id");
    ASSERT_TRUE(h2d_queue_id.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(h2d_queue_id.value()), shared_queue_id);

    const auto d2h_queue_id = d2h.GetParameters().TryGetInt("queue_id");
    ASSERT_TRUE(d2h_queue_id.has_value());
    EXPECT_EQ(static_cast<std::uint64_t>(d2h_queue_id.value()), shared_queue_id);
#else
    GTEST_SKIP() << "Metal graph nodes are disabled in this build configuration.";
#endif
}

TEST(GpuCapabilityBootstrap, DefaultSyclCapabilitiesSupportSmokeOperations) {
    using graph::gpu::accel::BackendKind;
    using graph::gpu::accel::BufferLease;
    using graph::gpu::accel::CollectiveKind;
    using graph::gpu::accel::CollectiveTicket;
    using graph::gpu::accel::DataType;
    using graph::gpu::accel::DeviceBufferView;
    using graph::gpu::accel::KernelTicket;
    using graph::gpu::accel::ReleasePolicy;
    using graph::gpu::accel::TransferTicket;

    graph::gpu::sycl::capabilities::DefaultSyclContextCapability context;
    EXPECT_EQ(context.CurrentDevice(), 0U);
    EXPECT_TRUE(context.SelectDevice(2U));
    EXPECT_EQ(context.CurrentDevice(), 2U);

    const auto queue_id = context.CreateQueue();
    const auto event_id = context.CreateEvent();
    EXPECT_NE(queue_id, 0U);
    EXPECT_NE(event_id, 0U);
    context.DestroyQueue(queue_id);
    context.DestroyEvent(event_id);

    graph::gpu::sycl::capabilities::DefaultSyclMemoryPoolCapability memory_pool;
    BufferLease device_lease{};
    EXPECT_TRUE(memory_pool.AllocateDevice(64U, 2U, device_lease));
    EXPECT_EQ(device_lease.release_policy, ReleasePolicy::Manual);
    EXPECT_EQ(device_lease.device_view.backend, BackendKind::SYCL);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_lease.device_view));
    EXPECT_TRUE(memory_pool.Release(device_lease));

    BufferLease host_lease{};
    EXPECT_TRUE(memory_pool.AllocateHost(64U, host_lease));
    EXPECT_EQ(host_lease.host_view.backend, BackendKind::SYCL);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_lease.host_view));
    EXPECT_TRUE(memory_pool.Release(host_lease));

    graph::gpu::sycl::capabilities::DefaultSyclTransferCapability transfer;
    std::array<std::uint8_t, 64> host_src{};
    std::array<std::uint8_t, 64> host_dst_buf{};
    std::array<std::uint8_t, 64> device_src{};
    std::array<std::uint8_t, 64> device_dst{};

    auto host_view = MakeValidSyclHostView(host_src.data());
    auto device_view = MakeValidSyclDeviceView(device_src.data());
    TransferTicket h2d_ticket{};
    EXPECT_TRUE(transfer.EnqueueH2D(host_view, device_view, queue_id, h2d_ticket));
    EXPECT_EQ(h2d_ticket.backend, BackendKind::SYCL);
    EXPECT_EQ(h2d_ticket.execution_queue_id, queue_id);
    EXPECT_NE(h2d_ticket.completion_event, 0U);
    EXPECT_EQ(device_view.ready_event, h2d_ticket.completion_event);

    DeviceBufferView d2d_dst = MakeValidSyclDeviceView(device_dst.data());
    TransferTicket d2d_ticket{};
    EXPECT_TRUE(transfer.EnqueueD2D(device_view, d2d_dst, queue_id, d2d_ticket));
    EXPECT_EQ(d2d_ticket.backend, BackendKind::SYCL);
    EXPECT_NE(d2d_ticket.completion_event, 0U);

    auto host_dst = MakeValidSyclHostView(host_dst_buf.data());
    TransferTicket d2h_ticket{};
    EXPECT_TRUE(transfer.EnqueueD2H(device_view, host_dst, queue_id, d2h_ticket));
    EXPECT_EQ(d2h_ticket.backend, BackendKind::SYCL);
    EXPECT_NE(d2h_ticket.completion_event, 0U);

    graph::gpu::sycl::capabilities::DefaultSyclKernelCapability kernel;
    KernelTicket kernel_ticket{};
    kernel_ticket.backend = BackendKind::SYCL;
    kernel_ticket.kernel_id = 42U;
    kernel_ticket.launch.grid_x = 1U;
    kernel_ticket.launch.grid_y = 1U;
    kernel_ticket.launch.grid_z = 1U;
    kernel_ticket.launch.block_x = 1U;
    kernel_ticket.launch.block_y = 1U;
    kernel_ticket.launch.block_z = 1U;
    kernel_ticket.arg_count = 1U;
    kernel_ticket.execution_queue_id = queue_id;
    kernel_ticket.completion_event = event_id;

    void* args[] = {&device_view};
    EXPECT_FALSE(kernel.Launch(kernel_ticket, args, 1U));
    EXPECT_TRUE(kernel.RegisterKernel(kernel_ticket.kernel_id, "sycl_test_kernel"));
    EXPECT_TRUE(kernel.Launch(kernel_ticket, args, 1U));

    graph::gpu::sycl::capabilities::DefaultSyclTelemetryCapability telemetry;
    telemetry.RecordTransfer(h2d_ticket, 17U);
    telemetry.RecordKernel(kernel_ticket, 23U);
    telemetry.IncrementErrorCounter("invalid-state");
    EXPECT_EQ(telemetry.TransferSamples(), 1U);
    EXPECT_EQ(telemetry.KernelSamples(), 1U);
    EXPECT_EQ(telemetry.ErrorCount(), 1U);

    graph::gpu::sycl::capabilities::DefaultSyclCollectiveCapability collective;
    DeviceBufferView collective_buffer = MakeValidSyclDeviceView(device_src.data());
    CollectiveTicket collective_ticket{};
    collective_ticket.backend = BackendKind::SYCL;
    collective_ticket.kind = CollectiveKind::AllReduce;
    collective_ticket.group_id = 9U;
    collective_ticket.rank = 0U;
    collective_ticket.world_size = 2U;
    collective_ticket.execution_queue_id = queue_id;
    collective_ticket.completion_event = event_id;

    EXPECT_TRUE(collective.AllReduce(collective_buffer, collective_ticket));
    EXPECT_TRUE(collective.AllGather(collective_buffer, d2d_dst, collective_ticket));
    EXPECT_TRUE(collective.ReduceScatter(collective_buffer, d2d_dst, collective_ticket));
}

TEST(GpuCapabilityBootstrap, ExplicitDisablePreventsRegistration) {
    graph::CapabilityBus bus;

    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_sycl = false;
    options.enable_metal = false;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    EXPECT_FALSE(bus.Has<graph::gpu::cuda::capabilities::ICudaContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::sycl::capabilities::ISyclContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::metal::capabilities::IMetalContextCapability>());
    EXPECT_FALSE(bus.Has<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>());
}

TEST(GpuCapabilityBootstrap, StrictNativeMetalRequirementBehavior) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_cuda = false;
    options.enable_sycl = false;
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
