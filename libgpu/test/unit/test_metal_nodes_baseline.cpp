#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "gpu/metal/nodes/CollectiveReduceNodeMetal.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceReduceNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceShardNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceTransformNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/HostEgressSinkNodeMetal.hpp"
#include "gpu/metal/nodes/HostIngressPinnedSourceNodeMetal.hpp"
#include "gpu/metal/nodes/LeaseReleaseNodeMetal.hpp"
#include "gpu/metal/nodes/PeerCopyNodeMetal.hpp"
#include "gpu/metal/nodes/QueueSyncNodeMetal.hpp"
#include "graph/CapabilityBus.hpp"

namespace {

class RecordingMetalMemoryPool final
    : public graph::gpu::metal::capabilities::IMetalMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t, std::uint32_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocateShared(std::uint64_t, std::uint32_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocateHost(std::uint64_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool Release(const graph::gpu::accel::BufferLease& lease) override {
        ++release_count;
        last_allocation_id = lease.allocation_id;
        return should_succeed;
    }

    bool should_succeed{true};
    std::size_t release_count{0};
    std::uint64_t last_allocation_id{0};
};

TEST(GpuMetalNodeBaseline, IngressToH2DToD2HFlowProducesValidTokens) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::metal::nodes::H2DAsyncNodeMetal>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::metal::nodes::D2HAsyncNodeMetal>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::metal::nodes::HostEgressSinkNodeMetal>);

    graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal ingress;
    graph::gpu::metal::nodes::H2DAsyncNodeMetal h2d;
    graph::gpu::metal::nodes::D2HAsyncNodeMetal d2h;
    graph::gpu::metal::nodes::HostEgressSinkNodeMetal sink;

    graph::CapabilityBus bus;
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>());

    ASSERT_TRUE(ingress.BindGpuCapabilities(bus));
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));

    graph::gpu::accel::HostPinnedBufferView host_view{};
    graph::gpu::accel::BufferLease host_lease{};
    constexpr std::uint64_t kBytes = 4096;
    ASSERT_TRUE(ingress.ProduceForTest(kBytes, host_view, host_lease));

    std::vector<std::uint8_t> expected(kBytes);
    for (std::uint64_t index = 0; index < kBytes; ++index) {
        expected[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>((index * 11U) & 0xFFU);
    }
    std::memcpy(host_view.host_ptr, expected.data(), expected.size());

    auto device_out = h2d.Transfer(host_view,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_out.has_value());
    EXPECT_EQ(device_out->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*device_out));

    auto host_roundtrip_out = d2h.Transfer(*device_out,
                                           std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_roundtrip_out.has_value());
    EXPECT_EQ(host_roundtrip_out->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*host_roundtrip_out));

    std::vector<std::uint8_t> actual(kBytes, 0);
    std::memcpy(actual.data(), host_roundtrip_out->host_ptr, actual.size());
    EXPECT_EQ(actual, expected);

    ASSERT_TRUE(sink.ConsumeForTest(*host_roundtrip_out));
    EXPECT_EQ(sink.ConsumeCount(), 1U);
    EXPECT_EQ(sink.LastView().bytes, host_roundtrip_out->bytes);
}

TEST(GpuMetalNodeBaseline, LeaseReleaseNodeReleasesLeaseThroughPoolCapability) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::metal::nodes::LeaseReleaseNodeMetal>);

    auto memory_pool = std::make_shared<RecordingMetalMemoryPool>();
    graph::CapabilityBus bus;
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);

    graph::gpu::metal::nodes::LeaseReleaseNodeMetal release_node;
    ASSERT_TRUE(release_node.BindGpuCapabilities(bus));

    graph::gpu::accel::BufferLease lease{};
    lease.pool_id = 21;
    lease.allocation_id = 99;
    lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
    lease.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    lease.device_view.device_ptr = reinterpret_cast<void*>(0x5000);
    lease.device_view.bytes = 64;
    lease.device_view.dtype = graph::gpu::accel::DataType::UInt8;
    lease.device_view.layout.rank = 1;
    lease.device_view.layout.shape[0] = 64;
    lease.device_view.layout.stride[0] = 1;

    ASSERT_TRUE(release_node.ConsumeForTest(lease));
    EXPECT_EQ(memory_pool->release_count, 1U);
    EXPECT_EQ(memory_pool->last_allocation_id, 99U);
    EXPECT_EQ(release_node.ReleaseCount(), 1U);
    EXPECT_EQ(release_node.LastReleasedLease().allocation_id, 99U);

    graph::gpu::accel::BufferLease invalid_lease{};
    invalid_lease.pool_id = 21;
    EXPECT_FALSE(release_node.ConsumeForTest(invalid_lease));
}

TEST(GpuMetalNodeBaseline, ComputeAndControlNodesAcceptMetalPayloads) {
    graph::CapabilityBus bus;
    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>());
    bus.Register<graph::gpu::metal::capabilities::IMetalCollectiveCapability>(
        std::make_shared<graph::gpu::metal::capabilities::DefaultMetalCollectiveCapability>());

    graph::gpu::metal::nodes::DeviceTransformNodeMetal transform;
    graph::gpu::metal::nodes::DeviceReduceNodeMetal reduce;
    graph::gpu::metal::nodes::QueueSyncNodeMetal sync;
    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;
    graph::gpu::metal::nodes::PeerCopyNodeMetal peer_copy;
    graph::gpu::metal::nodes::CollectiveReduceNodeMetal collective;

    ASSERT_TRUE(transform.BindGpuCapabilities(bus));
    ASSERT_TRUE(reduce.BindGpuCapabilities(bus));
    ASSERT_TRUE(sync.BindGpuCapabilities(bus));
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));
    ASSERT_TRUE(peer_copy.BindGpuCapabilities(bus));
    ASSERT_TRUE(collective.BindGpuCapabilities(bus));

    transform.ConfigureKernel(7U, "metal_transform", 0U, 3U);
    reduce.ConfigureKernel(8U, "metal_reduce", 0U, 4U);
    sync.SetQueue(5U);
    shard.ConfigureShard(0U, 2U);
    collective.ConfigureCollective(9U, 0U, 2U);

    graph::gpu::accel::DeviceBufferView input{};
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.device_ptr = reinterpret_cast<void*>(0x6000);
    input.bytes = 128;
    input.dtype = graph::gpu::accel::DataType::UInt8;
    input.layout.rank = 1;
    input.layout.shape[0] = 128;
    input.layout.stride[0] = 1;
    input.device_id = 0;
    input.execution_queue_id = 3U;
    input.ready_event = 11U;

    auto transformed = transform.Transfer(input,
                                         std::integral_constant<std::size_t, 0>{},
                                         std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(transformed.has_value());
    EXPECT_EQ(transformed->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*transformed));

    auto reduced = reduce.Transfer(*transformed,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(reduced.has_value());
    EXPECT_EQ(reduced->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*reduced));

    auto synced = sync.Transfer(*reduced,
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(synced.has_value());
    EXPECT_EQ(synced->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*synced));
    EXPECT_NE(synced->ready_event, reduced->ready_event);

    auto sharded = shard.Transfer(*synced,
                                  std::integral_constant<std::size_t, 0>{},
                                  std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(sharded.has_value());
    EXPECT_EQ(sharded->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*sharded));

    auto peer_copied = peer_copy.Transfer(*sharded,
                                          std::integral_constant<std::size_t, 0>{},
                                          std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(peer_copied.has_value());
    EXPECT_EQ(peer_copied->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*peer_copied));

    auto collectively_reduced = collective.Transfer(*peer_copied,
                                                     std::integral_constant<std::size_t, 0>{},
                                                     std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(collectively_reduced.has_value());
    EXPECT_EQ(collectively_reduced->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*collectively_reduced));
}

} // namespace