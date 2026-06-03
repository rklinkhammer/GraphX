#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"

namespace {

using graph::gpu::accel::BackendKind;
using graph::gpu::accel::CollectiveKind;
using graph::gpu::accel::CollectiveTicket;
using graph::gpu::accel::DataType;
using graph::gpu::accel::DeviceBufferView;
using graph::gpu::accel::HostPinnedBufferView;
using graph::gpu::accel::IsValidCollectiveTicket;
using graph::gpu::accel::IsValidKernelTicket;
using graph::gpu::accel::IsValidLayout;
using graph::gpu::accel::IsValidView;
using graph::gpu::accel::KernelTicket;
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
