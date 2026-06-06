#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceReduceNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceShardNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceTransformNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/HostEgressSinkNodeMetal.hpp"
#include "gpu/metal/nodes/HostIngressPinnedSourceNodeMetal.hpp"
#include "gpu/metal/nodes/LeaseReleaseNodeMetal.hpp"

TEST(MetalNativeRuntimeGraphPipelineTest, VibrationHealthPipelineRunsEndToEnd) {
    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.enable_metal = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    if (!graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable()) {
        GTEST_SKIP() << "Native Metal runtime unavailable: "
                     << graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
    }

    auto context = bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    auto memory_pool = bus.Get<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(memory_pool, nullptr);

    ASSERT_TRUE(context->SelectDevice(0U));
    const auto queue_id = context->CreateCommandQueue();
    ASSERT_NE(queue_id, 0U);

    graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal ingress;
    graph::gpu::metal::nodes::H2DAsyncNodeMetal h2d;
    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;
    graph::gpu::metal::nodes::DeviceTransformNodeMetal transform;
    graph::gpu::metal::nodes::DeviceReduceNodeMetal reduce;
    graph::gpu::metal::nodes::D2HAsyncNodeMetal d2h;
    graph::gpu::metal::nodes::HostEgressSinkNodeMetal egress;
    graph::gpu::metal::nodes::LeaseReleaseNodeMetal lease_release;

    ASSERT_TRUE(ingress.BindGpuCapabilities(bus));
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));
    ASSERT_TRUE(transform.BindGpuCapabilities(bus));
    ASSERT_TRUE(reduce.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));
    ASSERT_TRUE(lease_release.BindGpuCapabilities(bus));

    h2d.SetQueueAndDevice(queue_id, 0U);
    d2h.SetQueue(queue_id);

    // 3-axis accelerometer window encoded as bytes (X,Y,Z interleaved by contiguous blocks).
    constexpr std::uint64_t kFrameBytes = 96;
    graph::gpu::accel::HostPinnedBufferView ingress_view{};
    graph::gpu::accel::BufferLease ingress_lease{};
    ASSERT_TRUE(ingress.ProduceForTest(kFrameBytes, ingress_view, ingress_lease));

    auto* ingress_bytes = static_cast<std::byte*>(ingress_view.host_ptr);
    ASSERT_NE(ingress_bytes, nullptr);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kFrameBytes); ++i) {
        ingress_bytes[i] = std::byte{static_cast<std::uint8_t>((i * 11U) & 0xFFU)};
    }

    auto device_frame = h2d.Transfer(
        ingress_view,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_frame.has_value());
    device_frame->execution_queue_id = queue_id;

    shard.ConfigureShard(1U, 3U);
    auto sharded = shard.Transfer(
        *device_frame,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(sharded.has_value());
    ASSERT_EQ(sharded->bytes, 32U);

    transform.ConfigureKernel(10001U, "graphx_transform_xor_u8_inplace", 0U, queue_id);
    auto transformed = transform.Transfer(
        *sharded,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(transformed.has_value());

    reduce.ConfigureKernel(10002U, "graphx_reduce_health_metrics_u8", 0U, queue_id);
    auto reduced = reduce.Transfer(
        *transformed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(reduced.has_value());
    ASSERT_EQ(reduced->bytes, 8U);

    auto host_out = d2h.Transfer(
        *reduced,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_out.has_value());
    ASSERT_TRUE(egress.ConsumeForTest(*host_out));
    EXPECT_EQ(egress.ConsumeCount(), 1U);

    const auto* metrics = static_cast<const std::uint32_t*>(egress.LastView().host_ptr);
    ASSERT_NE(metrics, nullptr);

    std::uint64_t sumsq = 0;
    std::uint32_t peak = 0;
    for (std::size_t i = 0; i < 32U; ++i) {
        const auto raw = static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(ingress_bytes[32U + i]));
        const auto transformed_value = static_cast<std::uint32_t>(raw ^ 0xA5U);
        sumsq += static_cast<std::uint64_t>(transformed_value * transformed_value);
        peak = std::max(peak, transformed_value);
    }

    const auto expected_rms = static_cast<std::uint32_t>(
        std::sqrt(static_cast<double>(sumsq) / 32.0));
    EXPECT_EQ(metrics[0], expected_rms);
    EXPECT_EQ(metrics[1], peak);

    // Explicitly exercise lease release at the end of the pipeline lifecycle.
    EXPECT_TRUE(lease_release.ConsumeForTest(ingress_lease));

    context->DestroyCommandQueue(queue_id);
}
