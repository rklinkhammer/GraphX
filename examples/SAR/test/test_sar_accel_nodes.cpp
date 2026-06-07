#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "sar/D2HAsyncAccelNode.hpp"
#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/SarBackprojectionTransformAccelNode.hpp"

#include <cstddef>

namespace {

graph::gpu::accel::HostPinnedBufferView MakeHostView() {
    graph::gpu::accel::HostPinnedBufferView view{};
    view.backend = graph::gpu::accel::BackendKind::Metal;
    view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000u));
    view.bytes = 16u * sizeof(float);
    view.dtype = graph::gpu::accel::DataType::Float32;
    view.layout.rank = 1;
    view.layout.shape[0] = 16;
    view.layout.stride[0] = 1;
    view.allocator_id = 7;
    return view;
}

} // namespace

TEST(SarAccelNodesTest, H2DTransformD2HContractFlowUsesAccelTypes) {
    sar::H2DAsyncAccelConfig h2d_cfg{};
    h2d_cfg.override_backend = true;
    h2d_cfg.backend_id = 3;
    h2d_cfg.queue_id = 7;
    h2d_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::H2DAsyncAccelNode h2d;
    h2d.SetConfig(h2d_cfg);

    auto device_in = h2d.Transfer(
        MakeHostView(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(device_in.has_value());
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*device_in));
    EXPECT_EQ(device_in->execution_queue_id, 7u);
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(h2d.last_lease()));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d.last_transfer_ticket()));

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.backend_id = 3;
    bp_cfg.queue_id = 9;
    bp_cfg.kernel_id = 4402;
    bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);

    auto device_out = bp.Transfer(
        *device_in,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(device_out.has_value());
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*device_out));
    EXPECT_EQ(device_out->execution_queue_id, 9u);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 4402u);

    sar::D2HAsyncAccelConfig d2h_cfg{};
    d2h_cfg.override_backend = true;
    d2h_cfg.backend_id = 3;
    d2h_cfg.queue_id = 11;
    d2h_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::D2HAsyncAccelNode d2h;
    d2h.SetConfig(d2h_cfg);

    auto host_out = d2h.Transfer(
        *device_out,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(host_out.has_value());
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*host_out));
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(d2h.last_lease()));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h.last_transfer_ticket()));
    EXPECT_EQ(d2h.last_transfer_ticket().execution_queue_id, 11u);
}

TEST(SarAccelNodesTest, H2DRejectsUnknownBackendWhenNotOverridden) {
    sar::H2DAsyncAccelNode h2d;

    auto host = MakeHostView();
    host.backend = graph::gpu::accel::BackendKind::Unknown;

    auto out = h2d.Transfer(
        host,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    EXPECT_FALSE(out.has_value());
}
