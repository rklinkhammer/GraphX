#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncAccelNode.hpp"
#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
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

sar::SarPulseBlockMessage MakePulseForTokenSidecar() {
    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = 9;
    msg.envelope.batch_id = 5;
    msg.envelope.aperture_id = 9;
    msg.envelope.pulse_range_start = 9;
    msg.envelope.pulse_range_count = 1;
    msg.envelope.stream_id = 23;
    msg.envelope.tile_count = 4;
    msg.envelope.backend = sar::SarBackendKind::SimulatedDevice;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.iq_samples = {
        sar::SarIqSample(1.0f, 0.0f),
        sar::SarIqSample(2.0f, 0.0f),
        sar::SarIqSample(3.0f, 0.0f),
        sar::SarIqSample(4.0f, 0.0f),
    };
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
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

TEST(SarAccelNodesTest, PreservesTokenSidecarIdentityThroughDeviceStagesAndMerge) {
    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = 4;
    split_cfg.fixed_tile_id = 2;
    split_cfg.backend_id = 3;
    split_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::AzimuthTileSplitNode split(split_cfg);

    auto host_tile = split.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_tile.has_value());

    sar::H2DAsyncAccelConfig h2d_cfg{};
    h2d_cfg.override_backend = true;
    h2d_cfg.backend_id = 3;
    h2d_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::H2DAsyncAccelNode h2d;
    h2d.SetConfig(h2d_cfg);

    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.backend_id = 3;
    bp_cfg.kernel_id = 4402;
    bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);

    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    sar::D2HAsyncAccelConfig d2h_cfg{};
    d2h_cfg.override_backend = true;
    d2h_cfg.backend_id = 3;
    d2h_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::D2HAsyncAccelNode d2h;
    d2h.SetConfig(d2h_cfg);

    auto final_host_tile = d2h.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(final_host_tile.has_value());

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = 4;
    merge_cfg.backend_id = 3;
    merge_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::ImageTileMergeNode merge(merge_cfg);

    auto status = merge.Transfer(
        *final_host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->envelope.sequence_id, 9u);
    EXPECT_EQ(status->envelope.batch_id, 23u);
    EXPECT_EQ(status->envelope.aperture_id, 9u);
    EXPECT_EQ(status->envelope.pulse_range_start, 9u);
    EXPECT_EQ(status->envelope.pulse_range_count, 1u);
    EXPECT_EQ(status->envelope.stream_id, 23u);
    EXPECT_EQ(status->envelope.tile_id, 2u);
    EXPECT_EQ(status->envelope.tile_count, 4u);
    EXPECT_EQ(status->bytes_h2d, 16u);
    EXPECT_EQ(status->bytes_d2h, 16u);
    EXPECT_TRUE(status->gpu.has_host_view);
    EXPECT_TRUE(status->gpu.has_transfer_ticket);
    EXPECT_TRUE(status->gpu.has_kernel_ticket);
}
