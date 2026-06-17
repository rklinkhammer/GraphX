// SPDX-License-Identifier: MIT

/**
 * @file test_sar_accel_nodes.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncAccelNode.hpp"
#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/SarBackprojectionTransformAccelNode.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <cstddef>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace {

void ExpectCoreSidecarIdentityEq(const sar::SarSidecar& actual, const sar::SarSidecar& expected) {
    EXPECT_EQ(actual.sequence_id, expected.sequence_id);
    EXPECT_EQ(actual.batch_id, expected.batch_id);
    EXPECT_EQ(actual.aperture_id, expected.aperture_id);
    EXPECT_EQ(actual.pulse_range_start, expected.pulse_range_start);
    EXPECT_EQ(actual.pulse_range_count, expected.pulse_range_count);
    EXPECT_EQ(actual.stream_id, expected.stream_id);
    EXPECT_EQ(actual.tile_id, expected.tile_id);
    EXPECT_EQ(actual.tile_count, expected.tile_count);
    EXPECT_EQ(actual.marker, expected.marker);
    EXPECT_EQ(actual.synthetic, expected.synthetic);
    EXPECT_EQ(actual.payload_byte_count, expected.payload_byte_count);
}

sar::SarAccelControlToken MakeHostToken() {
    sar::SarAccelControlToken token{};
    token.token_id = 0x1000u;
    token.sidecar.sequence_id = 1u;
    token.sidecar.tile_id = 0u;
    token.sidecar.tile_count = 1u;
    token.sidecar.stream_id = 0u;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.payload_byte_count = 16u * sizeof(float);
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1001u));
    token.host_view.bytes = 16u * sizeof(float);
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 16;
    token.host_view.layout.stride[0] = 1;
    token.host_view.allocator_id = 7;
    token.has_host_view = true;
    return token;
}

sar::SarAccelControlToken MakePulseForTokenSidecar() {
    sar::SarAccelControlToken token{};
    token.token_id = 9u;
    token.sidecar.sequence_id = 9u;
    token.sidecar.batch_id = 5u;
    token.sidecar.aperture_id = 9u;
    token.sidecar.pulse_range_start = 9u;
    token.sidecar.pulse_range_count = 1u;
    token.sidecar.stream_id = 23u;
    token.sidecar.tile_id = 0u;
    token.sidecar.tile_count = 4u;
    token.sidecar.backend_id = 7u;
    token.sidecar.backend = sar::SarBackendKind::SimulatedDevice;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.sidecar.synthetic = false;
    token.sidecar.payload_byte_count = 16u;
    token.host_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1001u));
    token.host_view.bytes = 16u;
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout.rank = 1;
    token.host_view.layout.shape[0] = 4;
    token.host_view.layout.stride[0] = 1;
    token.has_host_view = true;
    return token;
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
        MakeHostToken(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(device_in.has_value());
    ASSERT_TRUE(device_in->has_device_view);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_in->device_view));
    EXPECT_EQ(device_in->device_view.ready_event, 0u);
    EXPECT_EQ(device_in->device_view.execution_queue_id, 7u);
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(h2d.last_lease()));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(h2d.last_transfer_ticket()));
    EXPECT_NE(h2d.last_transfer_ticket().completion_event, 0u);

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
    ASSERT_TRUE(device_out->has_device_view);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_out->device_view));
    EXPECT_EQ(device_out->device_view.ready_event, 0u);
    EXPECT_EQ(device_out->device_view.execution_queue_id, 9u);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 4402u);
    EXPECT_NE(bp.last_kernel_ticket().completion_event, 0u);

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
    ASSERT_TRUE(host_out->has_host_view);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_out->host_view));
    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(host_out->host_view.host_ptr),
        static_cast<std::uintptr_t>(0x1u));
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(d2h.last_lease()));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(d2h.last_transfer_ticket()));
    EXPECT_EQ(d2h.last_transfer_ticket().execution_queue_id, 11u);
    EXPECT_NE(d2h.last_transfer_ticket().completion_event, 0u);
}

TEST(SarAccelNodesTest, H2DRejectsUnknownBackendWhenNotOverridden) {
    sar::H2DAsyncAccelNode h2d;

    auto host = MakeHostToken();
    host.host_view.backend = graph::gpu::accel::BackendKind::Unknown;

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
    sar::SarAccelControlToken input{};
    input.token_id = 0xCAFEu;
    input.sidecar.sequence_id = 3u;
    input.sidecar.tile_id = 1u;
    input.sidecar.marker = sar::SarFrameMarker::Data;
    input.device_view = input_lease.device_view;
    input.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    input.device_view.dtype = graph::gpu::accel::DataType::Float32;
    input.device_view.layout.rank = 1;
    input.device_view.layout.shape[0] = 16;
    input.device_view.layout.stride[0] = 1;
    input.device_view.device_id = 0;
    input.device_view.execution_queue_id = 5;
    input.device_view.ready_event = 1u;
    input.has_device_view = true;

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
    ASSERT_TRUE(output->has_device_view);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(output->device_view));
    EXPECT_EQ(output->device_view.bytes, input.device_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 8801u);
    EXPECT_EQ(bp.last_kernel_ticket().arg_count, 2u);
    EXPECT_EQ(bp.last_kernel_ticket().execution_queue_id, 5u);
    EXPECT_EQ(telemetry->KernelSamples(), 1u);
    ExpectCoreSidecarIdentityEq(output->sidecar, input.sidecar);
    EXPECT_EQ(output->sidecar.backend_id, output->device_view.device_id);
    EXPECT_EQ(output->sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(output->sidecar.kernel_queue_id, bp.last_kernel_ticket().execution_queue_id);
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
    sar::SarAccelControlToken input{};
    input.token_id = 0x55AAu;
    input.sidecar.sequence_id = 4u;
    input.sidecar.tile_id = 2u;
    input.sidecar.marker = sar::SarFrameMarker::Data;
    input.device_view = input_lease.device_view;
    input.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    input.device_view.dtype = graph::gpu::accel::DataType::Float32;
    input.device_view.layout.rank = 1;
    input.device_view.layout.shape[0] = 32;
    input.device_view.layout.stride[0] = 1;
    input.device_view.device_id = 0;
    input.device_view.execution_queue_id = 9;
    input.device_view.ready_event = 2u;
    input.has_device_view = true;

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
    ASSERT_TRUE(output->has_device_view);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(output->device_view));
    EXPECT_EQ(output->device_view.bytes, input.device_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(bp.last_kernel_ticket()));
    EXPECT_EQ(bp.last_kernel_ticket().kernel_id, 9901u);
    EXPECT_EQ(bp.last_kernel_ticket().arg_count, 2u);
    EXPECT_EQ(bp.last_kernel_ticket().execution_queue_id, 9u);
    EXPECT_EQ(telemetry->KernelSamples(), 1u);
    ExpectCoreSidecarIdentityEq(output->sidecar, input.sidecar);
}

TEST(SarAccelNodesTest, H2DSidecarIdentityIsInvariantToHostPointerTransportMetadata) {
    auto host_a = MakePulseForTokenSidecar();
    auto host_b = host_a;
    host_b.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEF0001u));

    sar::H2DAsyncAccelConfig h2d_cfg{};
    h2d_cfg.override_backend = true;
    h2d_cfg.backend_id = 3;
    h2d_cfg.queue_id = 7;
    h2d_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::H2DAsyncAccelNode h2d_a;
    sar::H2DAsyncAccelNode h2d_b;
    h2d_a.SetConfig(h2d_cfg);
    h2d_b.SetConfig(h2d_cfg);

    auto device_a = h2d_a.Transfer(
        host_a,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto device_b = h2d_b.Transfer(
        host_b,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(device_a.has_value());
    ASSERT_TRUE(device_b.has_value());
    ASSERT_TRUE(device_a->has_device_view);
    ASSERT_TRUE(device_b->has_device_view);
    EXPECT_NE(host_a.host_view.host_ptr, host_b.host_view.host_ptr);
    EXPECT_EQ(device_a->device_view.ready_event, device_b->device_view.ready_event);
    ExpectCoreSidecarIdentityEq(device_a->sidecar, device_b->sidecar);
    EXPECT_EQ(device_a->sidecar.backend_id, device_b->sidecar.backend_id);
    EXPECT_EQ(device_a->sidecar.backend, device_b->sidecar.backend);
    EXPECT_EQ(device_a->sidecar.h2d_queue_id, device_b->sidecar.h2d_queue_id);
}

TEST(SarAccelNodesTest, BackprojectionSidecarIdentityIsInvariantToDeviceTransportMetadata) {
    sar::H2DAsyncAccelNode h2d;
    auto device = h2d.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device.has_value());

    auto mutated = *device;
    mutated.device_view.ready_event = device->device_view.ready_event + 77u;
    mutated.device_view.device_ptr =
        reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(device->device_view.device_ptr) + 4096u);

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.backend_id = 5;
    bp_cfg.queue_id = 9;
    bp_cfg.kernel_id = 4402;
    bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::SarBackprojectionTransformAccelNode bp_a(bp_cfg);
    sar::SarBackprojectionTransformAccelNode bp_b(bp_cfg);

    auto image_a = bp_a.Transfer(
        *device,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto image_b = bp_b.Transfer(
        mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(image_a.has_value());
    ASSERT_TRUE(image_b.has_value());
    ASSERT_TRUE(image_a->has_device_view);
    ASSERT_TRUE(image_b->has_device_view);
    EXPECT_NE(device->device_view.device_ptr, mutated.device_view.device_ptr);
    EXPECT_NE(device->device_view.ready_event, mutated.device_view.ready_event);
    ExpectCoreSidecarIdentityEq(image_a->sidecar, image_b->sidecar);
    EXPECT_EQ(image_a->sidecar.backend_id, image_b->sidecar.backend_id);
    EXPECT_EQ(image_a->sidecar.backend, image_b->sidecar.backend);
    EXPECT_EQ(image_a->sidecar.kernel_queue_id, image_b->sidecar.kernel_queue_id);
}

TEST(SarAccelNodesTest, NativeAndSyntheticBackprojectionPreserveEquivalentSidecarIdentity) {
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

    sar::SarAccelControlToken input{};
    input.token_id = 0xACEu;
    input.sidecar.sequence_id = 42u;
    input.sidecar.batch_id = 6u;
    input.sidecar.aperture_id = 42u;
    input.sidecar.pulse_range_start = 42u;
    input.sidecar.pulse_range_count = 1u;
    input.sidecar.stream_id = 17u;
    input.sidecar.tile_id = 3u;
    input.sidecar.tile_count = 8u;
    input.sidecar.marker = sar::SarFrameMarker::Data;
    input.sidecar.synthetic = false;
    input.sidecar.payload_byte_count = 64u;
    input.device_view = input_lease.device_view;
    input.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    input.device_view.dtype = graph::gpu::accel::DataType::Float32;
    input.device_view.layout.rank = 1;
    input.device_view.layout.shape[0] = 16;
    input.device_view.layout.stride[0] = 1;
    input.device_view.device_id = 0;
    input.device_view.execution_queue_id = 5;
    input.device_view.ready_event = 1u;
    input.has_device_view = true;

    sar::SarBackprojectionTransformAccelConfig native_cfg{};
    native_cfg.image_width = 16;
    native_cfg.backend_id = 0;
    native_cfg.queue_id = 5;
    native_cfg.kernel_id = 8801;
    native_cfg.backend = sar::SarBackendKind::NativeDevice;
    sar::SarBackprojectionTransformAccelNode native_bp(native_cfg);
    ASSERT_TRUE(native_bp.BindGpuCapabilities(bus));

    sar::SarBackprojectionTransformAccelConfig synthetic_cfg = native_cfg;
    synthetic_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::SarBackprojectionTransformAccelNode synthetic_bp(synthetic_cfg);

    auto native_output = native_bp.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto synthetic_output = synthetic_bp.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(native_output.has_value());
    ASSERT_TRUE(synthetic_output.has_value());
    ExpectCoreSidecarIdentityEq(native_output->sidecar, synthetic_output->sidecar);
    EXPECT_EQ(native_output->sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(synthetic_output->sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(native_output->sidecar.kernel_queue_id, synthetic_output->sidecar.kernel_queue_id);
    EXPECT_NE(native_output->device_view.device_ptr, synthetic_output->device_view.device_ptr);
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
    EXPECT_EQ(status->sidecar.sequence_id, 9u);
    EXPECT_EQ(status->sidecar.batch_id, 5u);
    EXPECT_EQ(status->sidecar.aperture_id, 9u);
    EXPECT_EQ(status->sidecar.pulse_range_start, 9u);
    EXPECT_EQ(status->sidecar.pulse_range_count, 1u);
    EXPECT_EQ(status->sidecar.stream_id, 23u);
    EXPECT_EQ(status->sidecar.tile_id, 2u);
    EXPECT_EQ(status->sidecar.tile_count, 4u);
    EXPECT_EQ(status->sidecar.backend_id, 3u);
    EXPECT_EQ(status->sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(status->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_FALSE(status->sidecar.synthetic);
    EXPECT_EQ(status->sidecar.bytes_h2d, 16u);
    EXPECT_EQ(status->sidecar.bytes_d2h, 16u);
    EXPECT_TRUE(status->has_host_view);
    EXPECT_TRUE(status->has_transfer_ticket);
    EXPECT_TRUE(status->has_kernel_ticket);
    EXPECT_EQ(status->transfer_ticket.execution_queue_id, 4u);
    EXPECT_EQ(status->kernel_ticket.execution_queue_id, 4u);
}

TEST(SarAccelNodesTest, MaterializedSinkExtractsPayloadFromAccelTokenFlow) {
    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = 4;
    split_cfg.fixed_tile_id = 1;
    split_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::AzimuthTileSplitNode split(split_cfg);

    auto host_tile = split.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_tile.has_value());

    sar::H2DAsyncAccelNode h2d;
    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelNode bp;
    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    sar::D2HAsyncAccelNode d2h;
    auto host_image = d2h.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_image.has_value());

    sar::SarMaterializedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{{"enabled", true}}));
    auto forwarded = sink.Transfer(
        *host_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(forwarded.has_value());

    EXPECT_TRUE(sink.has_materialized_image());
    EXPECT_EQ(sink.capture_count(), 1u);

    const auto metadata = sink.last_capture_metadata();
    const auto image = sink.last_materialized_image();
    ASSERT_EQ(metadata.element_count, image.size());
    EXPECT_EQ(metadata.sequence_id, 9u);
    EXPECT_EQ(metadata.tile_id, 1u);

    const auto reference = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id,
        metadata.tile_id,
        metadata.element_count);
    ASSERT_EQ(reference.size(), image.size());

    for (std::size_t i = 0; i < image.size(); ++i) {
        EXPECT_NEAR(image[i], reference[i], 1.0e-7f);
    }
}

TEST(SarAccelNodesTest, MergeIdentityIsInvariantToHostPointerWhenSidecarIsConstant) {
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

    sar::H2DAsyncAccelNode h2d;
    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelNode bp;
    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    sar::D2HAsyncAccelNode d2h;
    auto host_image = d2h.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_image.has_value());

    auto host_image_mutated = *host_image;
    host_image_mutated.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xABCu));

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = 4;
    merge_cfg.backend_id = 3;
    merge_cfg.backend = sar::SarBackendKind::SimulatedDevice;
    sar::ImageTileMergeNode merge_a(merge_cfg);
    sar::ImageTileMergeNode merge_b(merge_cfg);

    auto status_a = merge_a.Transfer(
        *host_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto status_b = merge_b.Transfer(
        host_image_mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status_a.has_value());
    ASSERT_TRUE(status_b.has_value());

    EXPECT_EQ(status_a->sidecar.sequence_id, status_b->sidecar.sequence_id);
    EXPECT_EQ(status_a->sidecar.batch_id, status_b->sidecar.batch_id);
    EXPECT_EQ(status_a->sidecar.aperture_id, status_b->sidecar.aperture_id);
    EXPECT_EQ(status_a->sidecar.pulse_range_start, status_b->sidecar.pulse_range_start);
    EXPECT_EQ(status_a->sidecar.pulse_range_count, status_b->sidecar.pulse_range_count);
    EXPECT_EQ(status_a->sidecar.stream_id, status_b->sidecar.stream_id);
    EXPECT_EQ(status_a->sidecar.tile_id, status_b->sidecar.tile_id);
    EXPECT_EQ(status_a->sidecar.tile_count, status_b->sidecar.tile_count);
    EXPECT_EQ(status_a->sidecar.marker, status_b->sidecar.marker);
    EXPECT_EQ(status_a->sidecar.bytes_h2d, status_b->sidecar.bytes_h2d);
    EXPECT_EQ(status_a->sidecar.bytes_d2h, status_b->sidecar.bytes_d2h);
    EXPECT_EQ(status_a->sidecar.kernel_dispatches, status_b->sidecar.kernel_dispatches);
}

TEST(SarAccelNodesTest, SplitDoesNotEncodeIdentityIntoHostPointerChannel) {
    sar::AzimuthTileSplitNode split;

    auto first = MakePulseForTokenSidecar();
    first.sidecar.sequence_id = 100u;
    first.sidecar.stream_id = 10u;
    first.sidecar.tile_id = 1u;

    auto second = MakePulseForTokenSidecar();
    second.sidecar.sequence_id = 200u;
    second.sidecar.stream_id = 20u;
    second.sidecar.tile_id = 3u;

    auto token_a = split.Transfer(
        first,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto token_b = split.Transfer(
        second,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(token_a.has_value());
    ASSERT_TRUE(token_b.has_value());
    ASSERT_TRUE(token_a->has_host_view);
    ASSERT_TRUE(token_b->has_host_view);

    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(token_a->host_view.host_ptr),
        reinterpret_cast<std::uintptr_t>(token_b->host_view.host_ptr));
    EXPECT_EQ(
        reinterpret_cast<std::uintptr_t>(token_a->host_view.host_ptr),
        static_cast<std::uintptr_t>(0x1u));

    EXPECT_NE(token_a->sidecar.sequence_id, token_b->sidecar.sequence_id);
    EXPECT_NE(token_a->sidecar.stream_id, token_b->sidecar.stream_id);
}

TEST(SarAccelNodesTest, D2HPreservesSidecarIdentityWhenReadyEventChanges) {
    sar::AzimuthTileSplitNode split;
    auto host_tile = split.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_tile.has_value());

    sar::H2DAsyncAccelNode h2d;
    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelNode bp;
    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    auto mutated = *device_image;
    mutated.device_view.ready_event = device_image->device_view.ready_event + 77u;

    sar::D2HAsyncAccelNode d2h_a;
    sar::D2HAsyncAccelNode d2h_b;

    auto host_a = d2h_a.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto host_b = d2h_b.Transfer(
        mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(host_a.has_value());
    ASSERT_TRUE(host_b.has_value());

    EXPECT_EQ(host_a->sidecar.sequence_id, host_b->sidecar.sequence_id);
    EXPECT_EQ(host_a->sidecar.batch_id, host_b->sidecar.batch_id);
    EXPECT_EQ(host_a->sidecar.aperture_id, host_b->sidecar.aperture_id);
    EXPECT_EQ(host_a->sidecar.pulse_range_start, host_b->sidecar.pulse_range_start);
    EXPECT_EQ(host_a->sidecar.pulse_range_count, host_b->sidecar.pulse_range_count);
    EXPECT_EQ(host_a->sidecar.stream_id, host_b->sidecar.stream_id);
    EXPECT_EQ(host_a->sidecar.tile_id, host_b->sidecar.tile_id);
    EXPECT_EQ(host_a->sidecar.tile_count, host_b->sidecar.tile_count);
    EXPECT_EQ(host_a->sidecar.marker, host_b->sidecar.marker);
}

TEST(SarAccelNodesTest, MergeIdentityIsInvariantToReadyEventWhenSidecarIsConstant) {
    sar::AzimuthTileSplitNode split;
    auto host_tile = split.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_tile.has_value());

    sar::H2DAsyncAccelNode h2d;
    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelNode bp;
    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    auto device_image_mutated = *device_image;
    device_image_mutated.device_view.ready_event = device_image->device_view.ready_event + 9999u;

    sar::D2HAsyncAccelNode d2h_a;
    sar::D2HAsyncAccelNode d2h_b;
    auto host_image_a = d2h_a.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto host_image_b = d2h_b.Transfer(
        device_image_mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_image_a.has_value());
    ASSERT_TRUE(host_image_b.has_value());

    sar::ImageTileMergeNode merge_a;
    sar::ImageTileMergeNode merge_b;
    auto status_a = merge_a.Transfer(
        *host_image_a,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto status_b = merge_b.Transfer(
        *host_image_b,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status_a.has_value());
    ASSERT_TRUE(status_b.has_value());

    EXPECT_EQ(status_a->sidecar.sequence_id, status_b->sidecar.sequence_id);
    EXPECT_EQ(status_a->sidecar.batch_id, status_b->sidecar.batch_id);
    EXPECT_EQ(status_a->sidecar.aperture_id, status_b->sidecar.aperture_id);
    EXPECT_EQ(status_a->sidecar.pulse_range_start, status_b->sidecar.pulse_range_start);
    EXPECT_EQ(status_a->sidecar.pulse_range_count, status_b->sidecar.pulse_range_count);
    EXPECT_EQ(status_a->sidecar.stream_id, status_b->sidecar.stream_id);
    EXPECT_EQ(status_a->sidecar.tile_id, status_b->sidecar.tile_id);
    EXPECT_EQ(status_a->sidecar.tile_count, status_b->sidecar.tile_count);
    EXPECT_EQ(status_a->sidecar.marker, status_b->sidecar.marker);
    EXPECT_EQ(status_a->sidecar.bytes_h2d, status_b->sidecar.bytes_h2d);
    EXPECT_EQ(status_a->sidecar.bytes_d2h, status_b->sidecar.bytes_d2h);
    EXPECT_EQ(status_a->sidecar.kernel_dispatches, status_b->sidecar.kernel_dispatches);
}

TEST(SarAccelNodesTest, MergeIdentityIsInvariantWhenReadyEventAndHostPointerBothChange) {
    sar::AzimuthTileSplitNode split;
    auto host_tile = split.Transfer(
        MakePulseForTokenSidecar(),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_tile.has_value());

    sar::H2DAsyncAccelNode h2d;
    auto device_tile = h2d.Transfer(
        *host_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_tile.has_value());

    sar::SarBackprojectionTransformAccelNode bp;
    auto device_image = bp.Transfer(
        *device_tile,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_image.has_value());

    auto device_image_mutated = *device_image;
    device_image_mutated.device_view.ready_event = device_image->device_view.ready_event + 1234u;

    sar::D2HAsyncAccelNode d2h_a;
    sar::D2HAsyncAccelNode d2h_b;
    auto host_image_a = d2h_a.Transfer(
        *device_image,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto host_image_b = d2h_b.Transfer(
        device_image_mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_image_a.has_value());
    ASSERT_TRUE(host_image_b.has_value());

    auto host_image_b_mutated = *host_image_b;
    host_image_b_mutated.host_view.host_ptr =
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEFu));

    sar::ImageTileMergeNode merge_a;
    sar::ImageTileMergeNode merge_b;
    auto status_a = merge_a.Transfer(
        *host_image_a,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto status_b = merge_b.Transfer(
        host_image_b_mutated,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(status_a.has_value());
    ASSERT_TRUE(status_b.has_value());

    EXPECT_EQ(status_a->sidecar.sequence_id, status_b->sidecar.sequence_id);
    EXPECT_EQ(status_a->sidecar.batch_id, status_b->sidecar.batch_id);
    EXPECT_EQ(status_a->sidecar.aperture_id, status_b->sidecar.aperture_id);
    EXPECT_EQ(status_a->sidecar.pulse_range_start, status_b->sidecar.pulse_range_start);
    EXPECT_EQ(status_a->sidecar.pulse_range_count, status_b->sidecar.pulse_range_count);
    EXPECT_EQ(status_a->sidecar.stream_id, status_b->sidecar.stream_id);
    EXPECT_EQ(status_a->sidecar.tile_id, status_b->sidecar.tile_id);
    EXPECT_EQ(status_a->sidecar.tile_count, status_b->sidecar.tile_count);
    EXPECT_EQ(status_a->sidecar.marker, status_b->sidecar.marker);
    EXPECT_EQ(status_a->sidecar.bytes_h2d, status_b->sidecar.bytes_h2d);
    EXPECT_EQ(status_a->sidecar.bytes_d2h, status_b->sidecar.bytes_d2h);
    EXPECT_EQ(status_a->sidecar.kernel_dispatches, status_b->sidecar.kernel_dispatches);
}
