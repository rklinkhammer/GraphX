#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"

TEST(MetalNativeRuntimeFailureInjectionTest, InvalidInputsFailGracefullyAndTrackTelemetryErrors) {
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
    ASSERT_TRUE(memory_pool->AllocateDevice(64U, 0U, device_lease));

    std::array<std::uint8_t, 64> host_src{};
    std::array<std::uint8_t, 64> host_dst{};

    graph::gpu::accel::HostPinnedBufferView host_src_view{};
    host_src_view.backend = graph::gpu::accel::BackendKind::Metal;
    host_src_view.host_ptr = host_src.data();
    host_src_view.bytes = host_src.size();
    host_src_view.dtype = graph::gpu::accel::DataType::UInt8;
    host_src_view.layout.rank = 1;
    host_src_view.layout.shape[0] = host_src.size();
    host_src_view.layout.stride[0] = 1;
    host_src_view.allocator_id = 8101;

    graph::gpu::accel::HostPinnedBufferView host_dst_view{};
    host_dst_view.backend = graph::gpu::accel::BackendKind::Metal;
    host_dst_view.host_ptr = host_dst.data();
    host_dst_view.bytes = host_dst.size();
    host_dst_view.dtype = graph::gpu::accel::DataType::UInt8;
    host_dst_view.layout.rank = 1;
    host_dst_view.layout.shape[0] = host_dst.size();
    host_dst_view.layout.stride[0] = 1;
    host_dst_view.allocator_id = 8102;

    graph::gpu::accel::TransferTicket ticket{};

    // Invalid queue id must fail.
    EXPECT_FALSE(transfer->EnqueueH2D(host_src_view, device_lease.device_view, 0U, ticket));

    // Invalid device pointer (not allocated by native pool) must fail lookup.
    graph::gpu::accel::DeviceBufferView invalid_device_view = device_lease.device_view;
    invalid_device_view.device_ptr = host_dst.data();
    invalid_device_view.backend = graph::gpu::accel::BackendKind::Metal;
    EXPECT_FALSE(transfer->EnqueueD2D(invalid_device_view, device_lease.device_view, queue_id, ticket));

    // Invalid registration inputs and launch contracts must fail.
    EXPECT_FALSE(kernel->RegisterKernel(0U, "graphx_bad_kernel"));
    EXPECT_FALSE(kernel->RegisterKernel(7001U, "bad-kernel-name"));

    constexpr std::uint64_t kKernelId = 7002;
    ASSERT_TRUE(kernel->RegisterKernel(kKernelId, "graphx_failure_noop_kernel"));

    graph::gpu::accel::KernelTicket kernel_ticket{};
    kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
    kernel_ticket.kernel_id = kKernelId;
    kernel_ticket.launch.grid_x = 1;
    kernel_ticket.launch.grid_y = 1;
    kernel_ticket.launch.grid_z = 1;
    kernel_ticket.launch.block_x = 1;
    kernel_ticket.launch.block_y = 1;
    kernel_ticket.launch.block_z = 1;
    kernel_ticket.execution_queue_id = queue_id;
    kernel_ticket.completion_event = event_id;

    // Arg mismatch should fail.
    kernel_ticket.arg_count = 1;
    EXPECT_FALSE(kernel->Launch(kernel_ticket, nullptr, 0));

    // Unknown kernel id should fail.
    kernel_ticket.kernel_id = 999999;
    kernel_ticket.arg_count = 0;
    EXPECT_FALSE(kernel->Launch(kernel_ticket, nullptr, 0));

    // Telemetry error accounting path on invalid tickets.
    const auto error_before = native_telemetry->ErrorCount();
    graph::gpu::accel::TransferTicket invalid_transfer{};
    graph::gpu::accel::KernelTicket invalid_kernel{};
    telemetry->RecordTransfer(invalid_transfer, 12);
    telemetry->RecordKernel(invalid_kernel, 34);
    EXPECT_EQ(native_telemetry->ErrorCount(), error_before + 2);

    EXPECT_TRUE(memory_pool->Release(device_lease));
    context->DestroyEvent(event_id);
    context->DestroyCommandQueue(queue_id);
}
