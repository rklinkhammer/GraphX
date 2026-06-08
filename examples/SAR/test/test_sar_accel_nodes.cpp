#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
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

TEST(SarAccelNodesTest, NativeBackprojectionDelegatesToMetalKernelNodeAndPreservesSidecarToken) {
    graph::CapabilityBus bus;
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto kernel = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>();
    auto telemetry = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>();

    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(64, 0, input_lease));
    auto input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.dtype = graph::gpu::accel::DataType::Float32;
    input.layout.rank = 1;
    input.layout.shape[0] = 16;
    input.layout.stride[0] = 1;
    input.device_id = 0;
    input.execution_queue_id = 5;
    input.ready_event = 0xCAFEu;

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.image_width = 16;
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 5;
    bp_cfg.kernel_id = 8801;
    bp_cfg.backend = sar::SarBackendKind::NativeDevice;
    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);
    ASSERT_TRUE(bp.BindGpuCapabilities(bus));
    EXPECT_TRUE(bp.native_kernel_bound());

    auto output = bp.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(output.has_value());
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*output));
    EXPECT_EQ(output->bytes, input.bytes);
    EXPECT_EQ(output->ready_event, input.ready_event);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 8801u);
    EXPECT_EQ(bp.last_kernel_ticket().arg_count, 2u);
    EXPECT_EQ(bp.last_kernel_ticket().execution_queue_id, 5u);
    EXPECT_EQ(telemetry->KernelSamples(), 1u);
}

TEST(SarAccelNodesTest, NativeBackprojectionSupportsConfigurableKernelShapingParameters) {
    graph::CapabilityBus bus;
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto kernel = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>();
    auto telemetry = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>();

    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(128, 0, input_lease));
    auto input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.dtype = graph::gpu::accel::DataType::Float32;
    input.layout.rank = 1;
    input.layout.shape[0] = 32;
    input.layout.stride[0] = 1;
    input.device_id = 0;
    input.execution_queue_id = 9;
    input.ready_event = 0x55AAu;

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.image_width = 32;
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 9;
    bp_cfg.kernel_id = 9901;
    bp_cfg.tap_count = 12;
    bp_cfg.delay_step = 0.75f;
    bp_cfg.phase_tap_scale = 0.5f;
    bp_cfg.phase_aperture_scale = 0.3f;
    bp_cfg.backend = sar::SarBackendKind::NativeDevice;

    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);
    ASSERT_TRUE(bp.BindGpuCapabilities(bus));
    EXPECT_TRUE(bp.native_kernel_bound());

    const auto params = bp.GetParameters();
    auto tap_count = params.TryGetInt("tap_count");
    ASSERT_TRUE(tap_count.has_value());
    EXPECT_EQ(tap_count.value(), 12);
    auto delay_step = params.TryGetFloat("delay_step");
    ASSERT_TRUE(delay_step.has_value());
    EXPECT_FLOAT_EQ(delay_step.value(), 0.75f);
    auto phase_tap_scale = params.TryGetFloat("phase_tap_scale");
    ASSERT_TRUE(phase_tap_scale.has_value());
    EXPECT_FLOAT_EQ(phase_tap_scale.value(), 0.5f);
    auto phase_aperture_scale = params.TryGetFloat("phase_aperture_scale");
    ASSERT_TRUE(phase_aperture_scale.has_value());
    EXPECT_FLOAT_EQ(phase_aperture_scale.value(), 0.3f);

    auto output = bp.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(output.has_value());
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*output));
    EXPECT_EQ(output->bytes, input.bytes);
    EXPECT_EQ(output->ready_event, input.ready_event);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 9901u);
    EXPECT_EQ(bp.last_kernel_ticket().arg_count, 2u);
    EXPECT_EQ(bp.last_kernel_ticket().execution_queue_id, 9u);
    EXPECT_EQ(telemetry->KernelSamples(), 1u);
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
