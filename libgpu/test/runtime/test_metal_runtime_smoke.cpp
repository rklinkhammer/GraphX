#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"

namespace {

void AssertNativeMetalRuntimeIfStrict() {
#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
    ASSERT_TRUE(graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable())
        << "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal runtime unavailable: "
        << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#endif
}

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value)
        : key_(key) {
        const char* current = std::getenv(key_);
        if (current != nullptr) {
            had_previous_value_ = true;
            previous_value_ = current;
        }
        setenv(key_, value, 1);
    }

    ~ScopedEnvVar() {
        if (had_previous_value_) {
            setenv(key_, previous_value_.c_str(), 1);
        } else {
            unsetenv(key_);
        }
    }

private:
    const char* key_;
    bool had_previous_value_{false};
    std::string previous_value_;
};

}  // namespace

TEST(MetalNativeRuntimeSmokeTest, RegistersNativeOrFallsBackSafely) {
    AssertNativeMetalRuntimeIfStrict();

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;

    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    auto transfer = bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();
    auto kernel = bus.Get<graph::gpu::metal::capabilities::IMetalKernelCapability>();
    auto telemetry = bus.Get<graph::gpu::metal::capabilities::IMetalTelemetryCapability>();
    auto collective = bus.Get<graph::gpu::metal::capabilities::IMetalCollectiveCapability>();

    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);
    ASSERT_NE(transfer, nullptr);
    ASSERT_NE(kernel, nullptr);
    ASSERT_NE(telemetry, nullptr);
    ASSERT_NE(collective, nullptr);

    const bool native_available = graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
    const bool using_native = std::dynamic_pointer_cast<
        graph::gpu::metal::capabilities::NativeMetalContextCapability>(context) != nullptr;

    if (native_available) {
        EXPECT_TRUE(using_native);

        ASSERT_TRUE(context->SelectDevice(0U));
        EXPECT_EQ(context->CurrentDevice(), 0U);

        const auto queue_id = context->CreateCommandQueue();
        const auto event_id = context->CreateEvent();
        EXPECT_NE(queue_id, 0U);
        EXPECT_NE(event_id, 0U);
        EXPECT_FALSE(context->IsEventComplete(event_id));

        graph::gpu::accel::BufferLease device_lease{};
        graph::gpu::accel::BufferLease shared_lease{};
        graph::gpu::accel::BufferLease host_lease{};

        EXPECT_TRUE(memory_pool->AllocateDevice(64U, 0U, device_lease));
        EXPECT_TRUE(graph::gpu::accel::IsValidView(device_lease.device_view));
        EXPECT_TRUE(memory_pool->AllocateShared(64U, 0U, shared_lease));
        EXPECT_TRUE(graph::gpu::accel::IsValidView(shared_lease.device_view));
        EXPECT_TRUE(memory_pool->AllocateHost(64U, host_lease));
        EXPECT_TRUE(graph::gpu::accel::IsValidView(host_lease.host_view));

        std::array<std::uint8_t, 64> host_src{};
        std::array<std::uint8_t, 64> host_dst{};
        for (std::size_t i = 0; i < host_src.size(); ++i) {
            host_src[i] = static_cast<std::uint8_t>(i);
        }

        graph::gpu::accel::HostPinnedBufferView host_src_view{};
        host_src_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_src_view.host_ptr = host_src.data();
        host_src_view.bytes = host_src.size();
        host_src_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_src_view.layout.rank = 1;
        host_src_view.layout.shape[0] = host_src.size();
        host_src_view.layout.stride[0] = 1;
        host_src_view.allocator_id = 1001;

        graph::gpu::accel::HostPinnedBufferView host_dst_view{};
        host_dst_view.backend = graph::gpu::accel::BackendKind::Metal;
        host_dst_view.host_ptr = host_dst.data();
        host_dst_view.bytes = host_dst.size();
        host_dst_view.dtype = graph::gpu::accel::DataType::UInt8;
        host_dst_view.layout.rank = 1;
        host_dst_view.layout.shape[0] = host_dst.size();
        host_dst_view.layout.stride[0] = 1;
        host_dst_view.allocator_id = 1002;

        graph::gpu::accel::TransferTicket h2d_ticket{};
        EXPECT_TRUE(transfer->EnqueueH2D(host_src_view, device_lease.device_view, queue_id, h2d_ticket));
        EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d_ticket));
        EXPECT_EQ(device_lease.device_view.ready_event, h2d_ticket.completion_event);
        EXPECT_TRUE(context->WaitEvent(h2d_ticket.completion_event, 2000U));
        EXPECT_TRUE(context->IsEventComplete(h2d_ticket.completion_event));

        graph::gpu::accel::TransferTicket d2h_ticket{};
        EXPECT_TRUE(transfer->EnqueueD2H(device_lease.device_view, host_dst_view, queue_id, d2h_ticket));
        EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h_ticket));
        EXPECT_EQ(host_src, host_dst);

        graph::gpu::accel::TransferTicket d2d_ticket{};
        EXPECT_TRUE(transfer->EnqueueD2D(device_lease.device_view, shared_lease.device_view, queue_id, d2d_ticket));
        EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2d_ticket));
        EXPECT_EQ(shared_lease.device_view.ready_event, d2d_ticket.completion_event);

        auto native_telemetry = std::dynamic_pointer_cast<
            graph::gpu::metal::capabilities::NativeMetalTelemetryCapability>(telemetry);
        ASSERT_NE(native_telemetry, nullptr);
        native_telemetry->ResetForTesting();

        auto native_kernel = std::dynamic_pointer_cast<
            graph::gpu::metal::capabilities::NativeMetalKernelCapability>(kernel);
        ASSERT_NE(native_kernel, nullptr);

        const auto transfer_before = native_telemetry->TransferSamples();
        const auto kernel_before = native_telemetry->KernelSamples();
        const auto error_before = native_telemetry->ErrorCount();

        telemetry->RecordTransfer(h2d_ticket, 111);
        telemetry->RecordTransfer(d2h_ticket, 222);
        telemetry->RecordTransfer(d2d_ticket, 333);
        EXPECT_EQ(native_telemetry->TransferSamples(), transfer_before + 3U);

        constexpr std::uint64_t kKernelId = 9001;
        EXPECT_TRUE(kernel->RegisterKernel(kKernelId, "graphx_identity_u8_inplace"));

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
        EXPECT_TRUE(kernel->Launch(kernel_ticket, args, 1));
        EXPECT_TRUE(context->WaitEvent(event_id, 2000U));
        EXPECT_TRUE(context->IsEventComplete(event_id));

        constexpr std::uint64_t kSourceKernelId = 9002;
        constexpr std::string_view kSourceKernelName = "graphx_source_identity";
        constexpr std::string_view kSourceKernelText =
            "#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "kernel void graphx_source_identity(device uchar* data [[buffer(0)]], "
            "uint gid [[thread_position_in_grid]]) { data[gid] = data[gid]; }\n";
        EXPECT_TRUE(native_kernel->RegisterKernelFromSource(
            kSourceKernelId, kSourceKernelName, kSourceKernelText));

        graph::gpu::accel::KernelTicket source_ticket = kernel_ticket;
        source_ticket.kernel_id = kSourceKernelId;
        EXPECT_TRUE(kernel->Launch(source_ticket, args, 1));

        constexpr std::uint64_t kPrefixedKernelId = 9003;
        EXPECT_TRUE(kernel->RegisterKernel(kPrefixedKernelId, "builtin:graphx_prefixed_identity"));
        graph::gpu::accel::KernelTicket prefixed_ticket = kernel_ticket;
        prefixed_ticket.kernel_id = kPrefixedKernelId;
        EXPECT_TRUE(kernel->Launch(prefixed_ticket, args, 1));

        constexpr std::uint64_t kTypedKernelId = 9004;
        graph::gpu::metal::capabilities::MetalKernelDescriptor typed_descriptor{};
        typed_descriptor.kernel_id = kTypedKernelId;
        typed_descriptor.function_name = "graphx_typed_identity";
        typed_descriptor.source_kind = graph::gpu::metal::capabilities::MetalKernelSourceKind::Builtin;
        typed_descriptor.dispatch.default_block_x = 32;
        typed_descriptor.arg_layout.push_back(
            graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
                graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
                graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadWrite});
        EXPECT_TRUE(native_kernel->RegisterKernel(typed_descriptor));

        graph::gpu::metal::capabilities::IMetalKernelCapability::RegisteredKernelExecution
            typed_execution{};
        ASSERT_TRUE(kernel->TryGetRegisteredKernelExecution(kTypedKernelId, typed_execution));
        EXPECT_EQ(typed_execution.arg_count, 1U);
        EXPECT_EQ(typed_execution.dispatch.block_x, 32U);

        graph::gpu::accel::KernelTicket typed_ticket = kernel_ticket;
        typed_ticket.kernel_id = kTypedKernelId;
        typed_ticket.launch.block_x = typed_execution.dispatch.block_x;
        EXPECT_TRUE(kernel->Launch(typed_ticket, args, 1));

        graph::gpu::accel::KernelTicket typed_bad_ticket = typed_ticket;
        typed_bad_ticket.arg_count = 0;
        EXPECT_FALSE(kernel->Launch(typed_bad_ticket, nullptr, 0));

        telemetry->RecordKernel(kernel_ticket, 444);
        EXPECT_EQ(native_telemetry->KernelSamples(), kernel_before + 1U);
        telemetry->IncrementErrorCounter("phase-e-synthetic");
        EXPECT_EQ(native_telemetry->ErrorCount(), error_before + 1U);

        graph::gpu::accel::CollectiveTicket collective_ticket{};
        collective_ticket.backend = graph::gpu::accel::BackendKind::Metal;
        collective_ticket.kind = graph::gpu::accel::CollectiveKind::AllReduce;
        collective_ticket.group_id = 1;
        collective_ticket.rank = 0;
        collective_ticket.world_size = 1;
        collective_ticket.execution_queue_id = queue_id;
        collective_ticket.completion_event = event_id;

        EXPECT_FALSE(collective->AllReduce(device_lease.device_view, collective_ticket));

        collective_ticket.kind = graph::gpu::accel::CollectiveKind::AllGather;
        EXPECT_FALSE(collective->AllGather(device_lease.device_view, shared_lease.device_view, collective_ticket));

        collective_ticket.kind = graph::gpu::accel::CollectiveKind::ReduceScatter;
        EXPECT_FALSE(collective->ReduceScatter(device_lease.device_view, shared_lease.device_view, collective_ticket));

        EXPECT_TRUE(memory_pool->Release(device_lease));
        EXPECT_TRUE(memory_pool->Release(shared_lease));
        EXPECT_TRUE(memory_pool->Release(host_lease));

        context->DestroyEvent(event_id);
        context->DestroyCommandQueue(queue_id);
    } else {
        EXPECT_FALSE(using_native);
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
    }
}

TEST(MetalNativeRuntimeTelemetryTest, InvalidTicketsIncreaseErrorCounter) {
    AssertNativeMetalRuntimeIfStrict();

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto telemetry = bus.Get<graph::gpu::metal::capabilities::IMetalTelemetryCapability>();
    ASSERT_NE(telemetry, nullptr);

    const bool native_available = graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
    auto native_telemetry = std::dynamic_pointer_cast<
        graph::gpu::metal::capabilities::NativeMetalTelemetryCapability>(telemetry);

    if (!native_available) {
        EXPECT_EQ(native_telemetry, nullptr);
        return;
    }

    ASSERT_NE(native_telemetry, nullptr);
    native_telemetry->ResetForTesting();

    const auto transfer_before = native_telemetry->TransferSamples();
    const auto kernel_before = native_telemetry->KernelSamples();
    const auto errors_before = native_telemetry->ErrorCount();

    graph::gpu::accel::TransferTicket invalid_transfer{};
    graph::gpu::accel::KernelTicket invalid_kernel{};

    telemetry->RecordTransfer(invalid_transfer, 5);
    telemetry->RecordKernel(invalid_kernel, 7);

    EXPECT_EQ(native_telemetry->TransferSamples(), transfer_before);
    EXPECT_EQ(native_telemetry->KernelSamples(), kernel_before);
    EXPECT_EQ(native_telemetry->ErrorCount(), errors_before + 2);
}

TEST(MetalNativeRuntimeCollectiveTest, RejectsInvalidCollectiveInputs) {
    AssertNativeMetalRuntimeIfStrict();

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    auto collective = bus.Get<graph::gpu::metal::capabilities::IMetalCollectiveCapability>();
    ASSERT_NE(memory_pool, nullptr);
    ASSERT_NE(collective, nullptr);

    const bool native_available = graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
    if (!native_available) {
        return;
    }

    graph::gpu::accel::BufferLease device_lease{};
    graph::gpu::accel::BufferLease output_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(64U, 0U, device_lease));
    ASSERT_TRUE(memory_pool->AllocateDevice(64U, 0U, output_lease));

    graph::gpu::accel::CollectiveTicket valid_ticket{};
    valid_ticket.backend = graph::gpu::accel::BackendKind::Metal;
    valid_ticket.kind = graph::gpu::accel::CollectiveKind::AllReduce;
    valid_ticket.group_id = 1;
    valid_ticket.rank = 0;
    valid_ticket.world_size = 1;
    valid_ticket.execution_queue_id = 1;
    valid_ticket.completion_event = 42;

    graph::gpu::accel::CollectiveTicket invalid_ticket = valid_ticket;
    invalid_ticket.kind = graph::gpu::accel::CollectiveKind::Unknown;
    EXPECT_FALSE(collective->AllReduce(device_lease.device_view, invalid_ticket));

    EXPECT_FALSE(collective->AllReduce(device_lease.device_view, valid_ticket));
    valid_ticket.kind = graph::gpu::accel::CollectiveKind::AllGather;
    EXPECT_FALSE(collective->AllGather(device_lease.device_view, output_lease.device_view, valid_ticket));
    valid_ticket.kind = graph::gpu::accel::CollectiveKind::ReduceScatter;
    EXPECT_FALSE(collective->ReduceScatter(device_lease.device_view, output_lease.device_view, valid_ticket));

    graph::gpu::accel::DeviceBufferView invalid_view{};
    EXPECT_FALSE(collective->AllReduce(invalid_view, valid_ticket));
    EXPECT_FALSE(collective->AllGather(device_lease.device_view, invalid_view, valid_ticket));
    EXPECT_FALSE(collective->ReduceScatter(device_lease.device_view, invalid_view, valid_ticket));

    EXPECT_TRUE(memory_pool->Release(device_lease));
    EXPECT_TRUE(memory_pool->Release(output_lease));
}

TEST(MetalNativeRuntimeMemoryModeTest, PrivateDeviceStorageUsesStagedTransfers) {
    AssertNativeMetalRuntimeIfStrict();

    if (!graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable()) {
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
        return;
    }

    ScopedEnvVar private_storage_mode("GRAPHX_METAL_DEVICE_STORAGE_MODE", "private");

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    auto transfer = bus.Get<graph::gpu::metal::capabilities::IMetalTransferCapability>();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);
    ASSERT_NE(transfer, nullptr);

    ASSERT_TRUE(context->SelectDevice(0U));
    const auto queue_id = context->CreateCommandQueue();
    ASSERT_NE(queue_id, 0U);

    graph::gpu::accel::BufferLease device_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(128U, 0U, device_lease));
    ASSERT_NE(device_lease.device_view.device_ptr, nullptr);

    std::array<std::uint8_t, 128> host_src{};
    std::array<std::uint8_t, 128> host_dst{};
    for (std::size_t i = 0; i < host_src.size(); ++i) {
        host_src[i] = static_cast<std::uint8_t>((i * 7U) & 0xFFU);
    }

    graph::gpu::accel::HostPinnedBufferView host_src_view{};
    host_src_view.backend = graph::gpu::accel::BackendKind::Metal;
    host_src_view.host_ptr = host_src.data();
    host_src_view.bytes = host_src.size();
    host_src_view.dtype = graph::gpu::accel::DataType::UInt8;
    host_src_view.layout.rank = 1;
    host_src_view.layout.shape[0] = host_src.size();
    host_src_view.layout.stride[0] = 1;
    host_src_view.allocator_id = 4101;

    graph::gpu::accel::HostPinnedBufferView host_dst_view{};
    host_dst_view.backend = graph::gpu::accel::BackendKind::Metal;
    host_dst_view.host_ptr = host_dst.data();
    host_dst_view.bytes = host_dst.size();
    host_dst_view.dtype = graph::gpu::accel::DataType::UInt8;
    host_dst_view.layout.rank = 1;
    host_dst_view.layout.shape[0] = host_dst.size();
    host_dst_view.layout.stride[0] = 1;
    host_dst_view.allocator_id = 4102;

    graph::gpu::accel::TransferTicket h2d_ticket{};
    ASSERT_TRUE(transfer->EnqueueH2D(host_src_view, device_lease.device_view, queue_id, h2d_ticket));
    ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d_ticket));

    graph::gpu::accel::TransferTicket d2h_ticket{};
    ASSERT_TRUE(transfer->EnqueueD2H(device_lease.device_view, host_dst_view, queue_id, d2h_ticket));
    ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h_ticket));

    EXPECT_EQ(host_src, host_dst);

    EXPECT_TRUE(memory_pool->Release(device_lease));
    context->DestroyCommandQueue(queue_id);
}
