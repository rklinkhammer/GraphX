#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

#include "gpu/cuda/capabilities/DefaultCudaCapabilities.hpp"
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#include "graph/IGpuCapabilityBinding.hpp"
#include "graph/INode.hpp"

#include "graph/CapabilityBus.hpp"

#include "gpu/cuda/nodes/D2HAsyncNode.hpp"
#include "gpu/cuda/nodes/H2DAsyncNode.hpp"
#include "gpu/cuda/nodes/HostEgressSinkNode.hpp"
#include "gpu/cuda/nodes/HostIngressPinnedSourceNode.hpp"
#include "gpu/cuda/nodes/LeaseReleaseNode.hpp"
#include "gpu/sycl/nodes/D2HAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/H2DAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/HostEgressSinkNodeSycl.hpp"
#include "gpu/sycl/nodes/HostIngressPinnedSourceNodeSycl.hpp"
#include "gpu/sycl/nodes/LeaseReleaseNodeSycl.hpp"

namespace {

class RecordingCudaMemoryPool final : public graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t, std::uint32_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocatePinnedHost(std::uint64_t, graph::gpu::accel::BufferLease&) override {
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

class RecordingSyclMemoryPool final : public graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability {
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

TEST(GpuNodeBaseline, CudaIngressToH2DToD2HFlowProducesValidTokens) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::cuda::nodes::HostIngressPinnedSourceNode>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::cuda::nodes::H2DAsyncNode>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::cuda::nodes::D2HAsyncNode>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::cuda::nodes::HostEgressSinkNode>);

    graph::gpu::cuda::nodes::HostIngressPinnedSourceNode ingress;
    graph::gpu::cuda::nodes::H2DAsyncNode h2d;
    graph::gpu::cuda::nodes::D2HAsyncNode d2h;
    graph::gpu::cuda::nodes::HostEgressSinkNode sink;

    graph::CapabilityBus bus;
    bus.Register<graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability>(
        std::make_shared<graph::gpu::cuda::capabilities::DefaultCudaMemoryPoolCapability>());
    bus.Register<graph::gpu::cuda::capabilities::ICudaTransferCapability>(
        std::make_shared<graph::gpu::cuda::capabilities::DefaultCudaTransferCapability>());

    ASSERT_TRUE(ingress.BindGpuCapabilities(bus));
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));

    graph::gpu::accel::HostPinnedBufferView host_view{};
    graph::gpu::accel::BufferLease host_lease{};
    ASSERT_TRUE(ingress.ProduceForTest(4096, host_view, host_lease));

    graph::gpu::accel::DeviceBufferView device_view{};
    graph::gpu::accel::BufferLease device_lease{};
    graph::gpu::accel::TransferTicket ticket{};

    ASSERT_TRUE(h2d.TransferForTest(host_view, /*stream_id=*/1, /*device_id=*/0,
                                    device_view, device_lease, ticket));
    EXPECT_EQ(ticket.backend, graph::gpu::accel::BackendKind::CUDA);
    EXPECT_EQ(ticket.execution_queue_id, 1U);
    EXPECT_EQ(device_view.ready_event, ticket.completion_event);
    EXPECT_EQ(device_view.bytes, host_view.bytes);

    graph::gpu::accel::HostPinnedBufferView host_roundtrip_view{};
    graph::gpu::accel::BufferLease host_roundtrip_lease{};
    graph::gpu::accel::TransferTicket d2h_ticket{};

    ASSERT_TRUE(d2h.TransferForTest(device_view, /*stream_id=*/1,
                                    host_roundtrip_view, host_roundtrip_lease, d2h_ticket));
    EXPECT_EQ(d2h_ticket.backend, graph::gpu::accel::BackendKind::CUDA);
    EXPECT_EQ(d2h_ticket.execution_queue_id, 1U);
    EXPECT_EQ(host_roundtrip_view.bytes, device_view.bytes);
    EXPECT_EQ(d2h_ticket.dst_host.bytes, host_roundtrip_view.bytes);
    ASSERT_TRUE(sink.ConsumeForTest(host_roundtrip_view));
    EXPECT_EQ(sink.ConsumeCount(), 1U);
    EXPECT_EQ(sink.LastView().bytes, host_roundtrip_view.bytes);
}

TEST(GpuNodeBaseline, SyclIngressToH2DToD2HFlowProducesValidTokens) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::sycl::nodes::H2DAsyncNodeSycl>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::sycl::nodes::D2HAsyncNodeSycl>);
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::sycl::nodes::HostEgressSinkNodeSycl>);

    graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl ingress;
    graph::gpu::sycl::nodes::H2DAsyncNodeSycl h2d;
    graph::gpu::sycl::nodes::D2HAsyncNodeSycl d2h;
    graph::gpu::sycl::nodes::HostEgressSinkNodeSycl sink;

    graph::CapabilityBus bus;
    bus.Register<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>(
        std::make_shared<graph::gpu::sycl::capabilities::DefaultSyclMemoryPoolCapability>());
    bus.Register<graph::gpu::sycl::capabilities::ISyclTransferCapability>(
        std::make_shared<graph::gpu::sycl::capabilities::DefaultSyclTransferCapability>());

    ASSERT_TRUE(ingress.BindGpuCapabilities(bus));
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));

    graph::gpu::accel::HostPinnedBufferView host_view{};
    graph::gpu::accel::BufferLease host_lease{};
    ASSERT_TRUE(ingress.ProduceForTest(2048, host_view, host_lease));

    graph::gpu::accel::DeviceBufferView device_view{};
    graph::gpu::accel::BufferLease device_lease{};
    graph::gpu::accel::TransferTicket ticket{};

    ASSERT_TRUE(h2d.TransferForTest(host_view, /*queue_id=*/7, /*device_id=*/0,
                                    device_view, device_lease, ticket));
    EXPECT_EQ(ticket.backend, graph::gpu::accel::BackendKind::SYCL);
    EXPECT_EQ(ticket.execution_queue_id, 7U);
    EXPECT_EQ(device_view.ready_event, ticket.completion_event);
    EXPECT_EQ(device_view.bytes, host_view.bytes);

    graph::gpu::accel::HostPinnedBufferView host_roundtrip_view{};
    graph::gpu::accel::BufferLease host_roundtrip_lease{};
    graph::gpu::accel::TransferTicket d2h_ticket{};

    ASSERT_TRUE(d2h.TransferForTest(device_view, /*queue_id=*/7,
                                    host_roundtrip_view, host_roundtrip_lease, d2h_ticket));
    EXPECT_EQ(d2h_ticket.backend, graph::gpu::accel::BackendKind::SYCL);
    EXPECT_EQ(d2h_ticket.execution_queue_id, 7U);
    EXPECT_EQ(host_roundtrip_view.bytes, device_view.bytes);
    EXPECT_EQ(d2h_ticket.dst_host.bytes, host_roundtrip_view.bytes);
    ASSERT_TRUE(sink.ConsumeForTest(host_roundtrip_view));
    EXPECT_EQ(sink.ConsumeCount(), 1U);
    EXPECT_EQ(sink.LastView().bytes, host_roundtrip_view.bytes);
}

TEST(GpuNodeBaseline, CudaLeaseReleaseNodeReleasesLeaseThroughPoolCapability) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::cuda::nodes::LeaseReleaseNode>);

    auto memory_pool = std::make_shared<RecordingCudaMemoryPool>();
    graph::CapabilityBus bus;
    bus.Register<graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability>(memory_pool);
    graph::gpu::cuda::nodes::LeaseReleaseNode release_node;
    ASSERT_TRUE(release_node.BindGpuCapabilities(bus));

    graph::gpu::accel::BufferLease lease{};
    lease.pool_id = 1;
    lease.allocation_id = 42;
    lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;

    ASSERT_TRUE(release_node.ConsumeForTest(lease));
    EXPECT_EQ(memory_pool->release_count, 1U);
    EXPECT_EQ(memory_pool->last_allocation_id, 42U);
    EXPECT_EQ(release_node.ReleaseCount(), 1U);
    EXPECT_EQ(release_node.LastReleasedLease().allocation_id, 42U);
}

TEST(GpuNodeBaseline, SyclLeaseReleaseNodeReleasesLeaseThroughPoolCapability) {
    static_assert(std::is_base_of_v<graph::INode, graph::gpu::sycl::nodes::LeaseReleaseNodeSycl>);

    auto memory_pool = std::make_shared<RecordingSyclMemoryPool>();
    graph::CapabilityBus bus;
    bus.Register<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>(memory_pool);
    graph::gpu::sycl::nodes::LeaseReleaseNodeSycl release_node;
    ASSERT_TRUE(release_node.BindGpuCapabilities(bus));

    graph::gpu::accel::BufferLease lease{};
    lease.pool_id = 11;
    lease.allocation_id = 84;
    lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;

    ASSERT_TRUE(release_node.ConsumeForTest(lease));
    EXPECT_EQ(memory_pool->release_count, 1U);
    EXPECT_EQ(memory_pool->last_allocation_id, 84U);
    EXPECT_EQ(release_node.ReleaseCount(), 1U);
    EXPECT_EQ(release_node.LastReleasedLease().allocation_id, 84U);
}

} // namespace
