#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"

TEST(MetalNativeRuntimeStressTest, RepeatedTransfersAndKernelLaunchesUpdateTelemetry) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    auto transfer = bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();
    auto kernel = bus.Get<graph::gpu::metal::capabilities::IMetalKernelCapability>();
    auto telemetry = bus.Get<graph::gpu::metal::capabilities::IMetalTelemetryCapability>();

    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);
    ASSERT_NE(transfer, nullptr);
    ASSERT_NE(kernel, nullptr);
    ASSERT_NE(telemetry, nullptr);

    if (!graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable()) {
        GTEST_SKIP() << "Native Metal runtime unavailable on this machine.";
    }

    auto native_telemetry = std::dynamic_pointer_cast<
        graph::gpu::metal::capabilities::NativeMetalTelemetryCapability>(telemetry);
    ASSERT_NE(native_telemetry, nullptr);
    graph::gpu::metal::capabilities::NativeMetalTelemetryCapability::ResetForTesting();

    ASSERT_TRUE(context->SelectDevice(0U));
    const auto queue_id = context->CreateCommandQueue();
    const auto event_id = context->CreateEvent();
    ASSERT_NE(queue_id, 0U);
    ASSERT_NE(event_id, 0U);

    graph::gpu::accel::BufferLease device_lease{};
    graph::gpu::accel::BufferLease shared_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(128U, 0U, device_lease));
    ASSERT_TRUE(memory_pool->AllocateShared(128U, 0U, shared_lease));

    constexpr std::uint64_t kKernelId = 9100;
    ASSERT_TRUE(kernel->RegisterKernel(kKernelId, "graphx_stress_noop_kernel"));

    const auto transfer_before = native_telemetry->TransferSamples();
    const auto kernel_before = native_telemetry->KernelSamples();

    constexpr std::size_t kIterations = 5;
    for (std::size_t iter = 0; iter < kIterations; ++iter) {
        std::array<std::uint8_t, 128> host_src{};
        std::array<std::uint8_t, 128> host_dst{};

        for (std::size_t i = 0; i < host_src.size(); ++i) {
            host_src[i] = static_cast<std::uint8_t>((iter + i) % 251);
        }

        graph::gpu::accel::HostPinnedBufferView host_src_view{};
        host_src_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_src_view.host_ptr = host_src.data();
        host_src_view.bytes = host_src.size();
        host_src_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_src_view.layout.rank = 1;
        host_src_view.layout.shape[0] = host_src.size();
        host_src_view.layout.stride[0] = 1;
        host_src_view.allocator_id = 5001;

        graph::gpu::accel::HostPinnedBufferView host_dst_view{};
        host_dst_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_dst_view.host_ptr = host_dst.data();
        host_dst_view.bytes = host_dst.size();
        host_dst_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_dst_view.layout.rank = 1;
        host_dst_view.layout.shape[0] = host_dst.size();
        host_dst_view.layout.stride[0] = 1;
        host_dst_view.allocator_id = 5002;

        graph::gpu::accel::TransferTicket h2d_ticket{};
        ASSERT_TRUE(transfer->EnqueueH2D(host_src_view, device_lease.device_view, queue_id, h2d_ticket));
        ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d_ticket));
        telemetry->RecordTransfer(h2d_ticket, 100 + iter);

        graph::gpu::accel::TransferTicket d2d_ticket{};
        ASSERT_TRUE(transfer->EnqueueD2D(device_lease.device_view, shared_lease.device_view, queue_id, d2d_ticket));
        ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2d_ticket));
        telemetry->RecordTransfer(d2d_ticket, 200 + iter);

        graph::gpu::accel::TransferTicket d2h_ticket{};
        ASSERT_TRUE(transfer->EnqueueD2H(device_lease.device_view, host_dst_view, queue_id, d2h_ticket));
        ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h_ticket));
        telemetry->RecordTransfer(d2h_ticket, 300 + iter);

        EXPECT_EQ(host_src, host_dst);

        graph::gpu::accel::KernelTicket kernel_ticket{};
        kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
        kernel_ticket.kernel_id = kKernelId;
        kernel_ticket.launch.grid_x = 1;
        kernel_ticket.launch.grid_y = 1;
        kernel_ticket.launch.grid_z = 1;
        kernel_ticket.launch.block_x = 1;
        kernel_ticket.launch.block_y = 1;
        kernel_ticket.launch.block_z = 1;
        kernel_ticket.arg_count = 1;
        kernel_ticket.execution_queue_id = queue_id;
        kernel_ticket.completion_event = event_id;

        graph::gpu::accel::DeviceBufferView* arg0 = &device_lease.device_view;
        void* args[] = {arg0};
        ASSERT_TRUE(kernel->Launch(kernel_ticket, args, 1));
        telemetry->RecordKernel(kernel_ticket, 400 + iter);
    }

    EXPECT_EQ(native_telemetry->TransferSamples(), transfer_before + (kIterations * 3));
    EXPECT_EQ(native_telemetry->KernelSamples(), kernel_before + kIterations);

    EXPECT_TRUE(memory_pool->Release(device_lease));
    EXPECT_TRUE(memory_pool->Release(shared_lease));

    context->DestroyEvent(event_id);
    context->DestroyCommandQueue(queue_id);
}

TEST(MetalNativeRuntimeStressTest, ResourceLifecycleChurnRemainsStable) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    auto transfer = bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();
    auto kernel = bus.Get<graph::gpu::metal::capabilities::IMetalKernelCapability>();

    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);
    ASSERT_NE(transfer, nullptr);
    ASSERT_NE(kernel, nullptr);

    if (!graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable()) {
        GTEST_SKIP() << "Native Metal runtime unavailable on this machine.";
    }

    ASSERT_TRUE(context->SelectDevice(0U));

    constexpr std::size_t kIterations = 12;
    for (std::size_t iter = 0; iter < kIterations; ++iter) {
        const auto queue_id = context->CreateCommandQueue();
        const auto event_id = context->CreateEvent();
        ASSERT_NE(queue_id, 0U);
        ASSERT_NE(event_id, 0U);

        graph::gpu::accel::BufferLease device_lease{};
        graph::gpu::accel::BufferLease shared_lease{};
        ASSERT_TRUE(memory_pool->AllocateDevice(64U, 0U, device_lease));
        ASSERT_TRUE(memory_pool->AllocateShared(64U, 0U, shared_lease));

        std::array<std::uint8_t, 64> host_src{};
        std::array<std::uint8_t, 64> host_dst{};
        for (std::size_t i = 0; i < host_src.size(); ++i) {
            host_src[i] = static_cast<std::uint8_t>((iter + i * 3) % 251);
        }

        graph::gpu::accel::HostPinnedBufferView host_src_view{};
        host_src_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_src_view.host_ptr = host_src.data();
        host_src_view.bytes = host_src.size();
        host_src_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_src_view.layout.rank = 1;
        host_src_view.layout.shape[0] = host_src.size();
        host_src_view.layout.stride[0] = 1;
        host_src_view.allocator_id = 7001;

        graph::gpu::accel::HostPinnedBufferView host_dst_view{};
        host_dst_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_dst_view.host_ptr = host_dst.data();
        host_dst_view.bytes = host_dst.size();
        host_dst_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_dst_view.layout.rank = 1;
        host_dst_view.layout.shape[0] = host_dst.size();
        host_dst_view.layout.stride[0] = 1;
        host_dst_view.allocator_id = 7002;

        graph::gpu::accel::TransferTicket h2d_ticket{};
        ASSERT_TRUE(transfer->EnqueueH2D(host_src_view, device_lease.device_view, queue_id, h2d_ticket));
        ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d_ticket));

        graph::gpu::accel::TransferTicket d2h_ticket{};
        ASSERT_TRUE(transfer->EnqueueD2H(device_lease.device_view, host_dst_view, queue_id, d2h_ticket));
        ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h_ticket));
        EXPECT_EQ(host_src, host_dst);

        const std::uint64_t kernel_id = 9200 + iter;
        ASSERT_TRUE(kernel->RegisterKernel(kernel_id, "graphx_lifecycle_noop_kernel"));
        // Re-register same ID to ensure overwrite/replace path remains stable.
        ASSERT_TRUE(kernel->RegisterKernel(kernel_id, "graphx_lifecycle_noop_kernel"));

        graph::gpu::accel::KernelTicket kernel_ticket{};
        kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
        kernel_ticket.kernel_id = kernel_id;
        kernel_ticket.launch.grid_x = 1;
        kernel_ticket.launch.grid_y = 1;
        kernel_ticket.launch.grid_z = 1;
        kernel_ticket.launch.block_x = 1;
        kernel_ticket.launch.block_y = 1;
        kernel_ticket.launch.block_z = 1;
        kernel_ticket.arg_count = 1;
        kernel_ticket.execution_queue_id = queue_id;
        kernel_ticket.completion_event = event_id;

        graph::gpu::accel::DeviceBufferView* arg0 = &device_lease.device_view;
        void* args[] = {arg0};
        ASSERT_TRUE(kernel->Launch(kernel_ticket, args, 1));

        EXPECT_TRUE(memory_pool->Release(device_lease));
        EXPECT_TRUE(memory_pool->Release(shared_lease));

        context->DestroyEvent(event_id);
        context->DestroyCommandQueue(queue_id);
    }
}
