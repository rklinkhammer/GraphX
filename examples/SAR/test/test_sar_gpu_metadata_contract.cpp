#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "sar/D2HAsyncNode.hpp"
#include "sar/H2DAsyncNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/SarBackprojectionTransformNode.hpp"

#include <cstddef>

namespace {

sar::SarRangeTileMessage MakeRangeTile() {
    sar::SarRangeTileMessage msg{};
    msg.envelope.sequence_id = 4;
    msg.envelope.stream_id = 2;
    msg.envelope.tile_id = 1;
    msg.envelope.tile_count = 4;
    msg.envelope.backend_id = 3;
    msg.envelope.backend = sar::SarBackendKind::SimulatedDevice;
    msg.envelope.marker = sar::SarFrameMarker::Data;

    msg.buffer.buffer_id = 44;
    msg.buffer.device_index = 3;
    msg.buffer.backend = sar::SarBackendKind::SimulatedDevice;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;
    msg.range_bins = {1.0f, 2.0f, 3.0f, 4.0f};
    msg.buffer.byte_count = msg.range_bins.size() * sizeof(float);
    return msg;
}

} // namespace

TEST(SarGpuMetadataContractTest, PropagatesAccelMetadataAcrossDeviceStagesAndMerge) {
    sar::H2DAsyncConfig h2d_cfg{};
    h2d_cfg.override_backend = true;
    h2d_cfg.backend_id = 3;
    h2d_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::H2DAsyncNode h2d;
    h2d.SetConfig(h2d_cfg);

    auto device_tile = h2d.Transfer(
        MakeRangeTile(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(device_tile.has_value());
    EXPECT_TRUE(device_tile->gpu.has_host_view);
    EXPECT_TRUE(device_tile->gpu.has_device_view);
    EXPECT_TRUE(device_tile->gpu.has_lease);
    EXPECT_TRUE(device_tile->gpu.has_transfer_ticket);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_tile->gpu.host_view));
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_tile->gpu.device_view));
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(device_tile->gpu.lease));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(device_tile->gpu.transfer_ticket));
    EXPECT_NE(device_tile->gpu.host_view.host_ptr, device_tile->range_bins.data());

    sar::SarBackprojectionTransformConfig bp_cfg{};
    bp_cfg.image_width = 2;
    bp_cfg.backend_id = 3;
    bp_cfg.queue_id = 7;
    bp_cfg.kernel_id = 4402;
    bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::SarBackprojectionTransformNode bp(bp_cfg);

    auto image_tile = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(image_tile.has_value());
    EXPECT_TRUE(image_tile->gpu.has_device_view);
    EXPECT_TRUE(image_tile->gpu.has_kernel_ticket);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(image_tile->gpu.device_view));
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(image_tile->gpu.kernel_ticket));
    EXPECT_EQ(image_tile->gpu.kernel_ticket.kernel_id, 4402u);
    EXPECT_EQ(image_tile->gpu.kernel_ticket.execution_queue_id, 8u);

    sar::D2HAsyncConfig d2h_cfg{};
    d2h_cfg.override_backend = true;
    d2h_cfg.backend_id = 3;
    d2h_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::D2HAsyncNode d2h;
    d2h.SetConfig(d2h_cfg);

    auto host_tile = d2h.Transfer(
        *image_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(host_tile.has_value());
    EXPECT_TRUE(host_tile->gpu.has_host_view);
    EXPECT_TRUE(host_tile->gpu.has_device_view);
    EXPECT_TRUE(host_tile->gpu.has_transfer_ticket);
    EXPECT_TRUE(host_tile->gpu.has_kernel_ticket);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_tile->gpu.host_view));
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_tile->gpu.device_view));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(host_tile->gpu.transfer_ticket));
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(host_tile->gpu.kernel_ticket));
    EXPECT_NE(host_tile->gpu.host_view.host_ptr, host_tile->pixels.data());

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = 1;
    sar::ImageTileMergeNode merge(merge_cfg);
    auto status = merge.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status.has_value());
    EXPECT_TRUE(status->gpu.has_host_view);
    EXPECT_TRUE(status->gpu.has_transfer_ticket);
    EXPECT_TRUE(status->gpu.has_kernel_ticket);
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(status->gpu.transfer_ticket));
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(status->gpu.kernel_ticket));
}
