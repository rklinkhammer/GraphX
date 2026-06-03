#include <gtest/gtest.h>

#include "gpu/accel/types/AccelFormatting.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

#include <sstream>

namespace {

using graph::gpu::accel::BackendKind;
using graph::gpu::accel::CollectiveKind;
using graph::gpu::accel::CollectiveTicket;
using graph::gpu::accel::DataType;
using graph::gpu::accel::DeviceBufferView;
using graph::gpu::accel::DeviceShardDescriptor;
using graph::gpu::accel::HostPinnedBufferView;
using graph::gpu::accel::IsValidLease;
using graph::gpu::accel::IsValidCollectiveTicket;
using graph::gpu::accel::IsValidKernelTicket;
using graph::gpu::accel::IsValidLayout;
using graph::gpu::accel::IsValidView;
using graph::gpu::accel::IsValidTransferTicket;
using graph::gpu::accel::KernelTicket;
using graph::gpu::accel::BufferLease;
using graph::gpu::accel::TransferTicket;
using graph::gpu::accel::TensorLayout;

TensorLayout MakeLayout(std::uint64_t bytes) {
    TensorLayout layout{};
    layout.rank = 1;
    layout.shape[0] = bytes;
    layout.stride[0] = 1;
    return layout;
}

DeviceBufferView MakeDeviceView() {
    DeviceBufferView view{};
    view.backend = BackendKind::CUDA;
    view.device_ptr = reinterpret_cast<void*>(0x1000);
    view.bytes = 1024;
    view.dtype = DataType::Float32;
    view.layout = MakeLayout(1024);
    view.device_id = 0;
    view.execution_queue_id = 1;
    view.ready_event = 2;
    return view;
}

HostPinnedBufferView MakeHostView() {
    HostPinnedBufferView view{};
    view.backend = BackendKind::CUDA;
    view.host_ptr = reinterpret_cast<void*>(0x2000);
    view.bytes = 1024;
    view.dtype = DataType::Float32;
    view.layout = MakeLayout(1024);
    view.allocator_id = 9;
    return view;
}

} // namespace

TEST(AccelValidation, ValidLayoutAccepted) {
    const auto layout = MakeLayout(256);
    EXPECT_TRUE(IsValidLayout(layout));
}

TEST(AccelValidation, ZeroRankEntryRejected) {
    auto layout = MakeLayout(256);
    layout.shape[0] = 0;
    EXPECT_FALSE(IsValidLayout(layout));
}

TEST(AccelValidation, ValidDeviceAndHostViewsAccepted) {
    const auto device_view = MakeDeviceView();
    const auto host_view = MakeHostView();

    EXPECT_TRUE(IsValidView(device_view));
    EXPECT_TRUE(IsValidView(host_view));
}

TEST(AccelValidation, InvalidViewsRejected) {
    auto device_view = MakeDeviceView();
    device_view.device_ptr = nullptr;
    EXPECT_FALSE(IsValidView(device_view));

    auto host_view = MakeHostView();
    host_view.dtype = DataType::Unknown;
    EXPECT_FALSE(IsValidView(host_view));
}

TEST(AccelValidation, LeaseValidationRequiresAllocationAndValidPayload) {
    BufferLease lease{};
    lease.pool_id = 17;
    lease.allocation_id = 99;
    lease.device_view = MakeDeviceView();
    EXPECT_TRUE(IsValidLease(lease));

    lease.allocation_id = 0;
    EXPECT_FALSE(IsValidLease(lease));
}

TEST(AccelValidation, TransferTicketValidationRequiresDirectionAndIds) {
    TransferTicket ticket{};
    ticket.backend = BackendKind::SYCL;
    ticket.transfer_id = 4;
    ticket.execution_queue_id = 11;
    ticket.completion_event = 12;
    ticket.src_host = MakeHostView();
    ticket.dst_device = MakeDeviceView();
    EXPECT_TRUE(IsValidTransferTicket(ticket));

    ticket.transfer_id = 0;
    EXPECT_FALSE(IsValidTransferTicket(ticket));
}

TEST(AccelValidation, KernelTicketValidation) {
    KernelTicket ticket{};
    ticket.backend = BackendKind::SYCL;
    ticket.kernel_id = 77;
    ticket.arg_count = 2;
    ticket.launch.grid_x = 2;
    ticket.launch.grid_y = 1;
    ticket.launch.grid_z = 1;
    ticket.launch.block_x = 64;
    ticket.launch.block_y = 1;
    ticket.launch.block_z = 1;
    EXPECT_TRUE(IsValidKernelTicket(ticket));

    ticket.kernel_id = 0;
    EXPECT_FALSE(IsValidKernelTicket(ticket));
}

TEST(AccelValidation, CollectiveTicketValidation) {
    CollectiveTicket ticket{};
    ticket.backend = BackendKind::CUDA;
    ticket.kind = CollectiveKind::AllReduce;
    ticket.rank = 1;
    ticket.world_size = 2;
    EXPECT_TRUE(IsValidCollectiveTicket(ticket));

    ticket.rank = 2;
    EXPECT_FALSE(IsValidCollectiveTicket(ticket));
}

TEST(AccelValidation, ShardDescriptorValidation) {
    DeviceShardDescriptor shard{};
    shard.global_layout = MakeLayout(256);
    shard.shard_index = 0;
    shard.shard_count = 2;
    shard.element_length = 128;
    EXPECT_TRUE(graph::gpu::accel::IsValidShardDescriptor(shard));

    shard.shard_index = 2;
    EXPECT_FALSE(graph::gpu::accel::IsValidShardDescriptor(shard));
}

TEST(AccelValidation, PayloadFormattingProducesUsefulDiagnostics) {
    BufferLease lease{};
    lease.pool_id = 3;
    lease.allocation_id = 12;
    lease.device_view = MakeDeviceView();

    TransferTicket ticket{};
    ticket.backend = BackendKind::SYCL;
    ticket.transfer_id = 44;
    ticket.execution_queue_id = 55;
    ticket.completion_event = 66;
    ticket.src_host = MakeHostView();
    ticket.dst_device = MakeDeviceView();

    DeviceShardDescriptor shard{};
    shard.global_layout = MakeLayout(128);
    shard.shard_index = 1;
    shard.shard_count = 4;
    shard.element_length = 32;

    CollectiveTicket collective{};
    collective.backend = BackendKind::CUDA;
    collective.kind = CollectiveKind::ReduceScatter;
    collective.group_id = 9;
    collective.rank = 2;
    collective.world_size = 4;

    std::ostringstream out;
    out << lease << '\n' << ticket << '\n' << shard << '\n' << collective;
    const auto text = out.str();

    EXPECT_NE(text.find("BufferLease{"), std::string::npos);
    EXPECT_NE(text.find("TransferTicket{"), std::string::npos);
    EXPECT_NE(text.find("DeviceShardDescriptor{"), std::string::npos);
    EXPECT_NE(text.find("CollectiveTicket{"), std::string::npos);
    EXPECT_NE(text.find("SYCL"), std::string::npos);
    EXPECT_NE(text.find("ReduceScatter"), std::string::npos);
}
