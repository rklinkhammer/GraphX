#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

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
#include "gpu/accel/types/AccelValidation.hpp"

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
    auto device_out = h2d.Transfer(host_view,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_out.has_value());
    device_view = *device_out;
    EXPECT_EQ(device_view.backend, graph::gpu::accel::BackendKind::CUDA);
    EXPECT_NE(device_view.ready_event, 0U);
    EXPECT_EQ(device_view.bytes, host_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_view));

    graph::gpu::accel::HostPinnedBufferView host_roundtrip_view{};
    auto host_roundtrip_out = d2h.Transfer(device_view,
                                           std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_roundtrip_out.has_value());
    host_roundtrip_view = *host_roundtrip_out;
    EXPECT_EQ(host_roundtrip_view.bytes, device_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_roundtrip_view));
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
    auto device_out = h2d.Transfer(host_view,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_out.has_value());
    device_view = *device_out;
    EXPECT_EQ(device_view.backend, graph::gpu::accel::BackendKind::SYCL);
    EXPECT_NE(device_view.ready_event, 0U);
    EXPECT_EQ(device_view.bytes, host_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(device_view));

    graph::gpu::accel::HostPinnedBufferView host_roundtrip_view{};
    auto host_roundtrip_out = d2h.Transfer(device_view,
                                           std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_roundtrip_out.has_value());
    host_roundtrip_view = *host_roundtrip_out;
    EXPECT_EQ(host_roundtrip_view.bytes, device_view.bytes);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(host_roundtrip_view));
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
    lease.device_view.backend = graph::gpu::accel::BackendKind::CUDA;
    lease.device_view.device_ptr = reinterpret_cast<void*>(0x3000);
    lease.device_view.bytes = 64;
    lease.device_view.dtype = graph::gpu::accel::DataType::UInt8;
    lease.device_view.layout.rank = 1;
    lease.device_view.layout.shape[0] = 64;
    lease.device_view.layout.stride[0] = 1;

    ASSERT_TRUE(release_node.ConsumeForTest(lease));
    EXPECT_EQ(memory_pool->release_count, 1U);
    EXPECT_EQ(memory_pool->last_allocation_id, 42U);
    EXPECT_EQ(release_node.ReleaseCount(), 1U);
    EXPECT_EQ(release_node.LastReleasedLease().allocation_id, 42U);

    graph::gpu::accel::BufferLease invalid_lease{};
    invalid_lease.pool_id = 1;
    EXPECT_FALSE(release_node.ConsumeForTest(invalid_lease));
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
    lease.host_view.host_ptr = reinterpret_cast<void*>(0x4000);
    lease.host_view.bytes = 64;
    lease.host_view.dtype = graph::gpu::accel::DataType::UInt8;
    lease.host_view.layout.rank = 1;
    lease.host_view.layout.shape[0] = 64;
    lease.host_view.layout.stride[0] = 1;

    ASSERT_TRUE(release_node.ConsumeForTest(lease));
    EXPECT_EQ(memory_pool->release_count, 1U);
    EXPECT_EQ(memory_pool->last_allocation_id, 84U);
    EXPECT_EQ(release_node.ReleaseCount(), 1U);
    EXPECT_EQ(release_node.LastReleasedLease().allocation_id, 84U);

    graph::gpu::accel::BufferLease invalid_lease{};
    invalid_lease.pool_id = 11;
    EXPECT_FALSE(release_node.ConsumeForTest(invalid_lease));
}

TEST(GpuNodeBaseline, CudaStubTransfersPreservePayloadBytes) {
    graph::gpu::cuda::nodes::HostIngressPinnedSourceNode ingress;
    graph::gpu::cuda::nodes::H2DAsyncNode h2d;
    graph::gpu::cuda::nodes::D2HAsyncNode d2h;

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
    constexpr std::size_t kBytes = 256;
    ASSERT_TRUE(ingress.ProduceForTest(kBytes, host_view, host_lease));

    std::vector<std::uint8_t> expected(kBytes);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::uint8_t>((index * 7U) & 0xFFU);
    }
    std::memcpy(host_view.host_ptr, expected.data(), expected.size());

    graph::gpu::accel::DeviceBufferView device_view{};
    auto device_out = h2d.Transfer(host_view,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_out.has_value());
    device_view = *device_out;

    graph::gpu::accel::HostPinnedBufferView roundtrip_view{};
    auto roundtrip_out = d2h.Transfer(device_view,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(roundtrip_out.has_value());
    roundtrip_view = *roundtrip_out;

    std::vector<std::uint8_t> actual(kBytes, 0);
    std::memcpy(actual.data(), roundtrip_view.host_ptr, actual.size());
    EXPECT_EQ(actual, expected);
}

TEST(GpuNodeBaseline, SyclStubTransfersPreservePayloadBytes) {
    graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl ingress;
    graph::gpu::sycl::nodes::H2DAsyncNodeSycl h2d;
    graph::gpu::sycl::nodes::D2HAsyncNodeSycl d2h;

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
    constexpr std::size_t kBytes = 320;
    ASSERT_TRUE(ingress.ProduceForTest(kBytes, host_view, host_lease));

    std::vector<std::uint8_t> expected(kBytes);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expected[index] = static_cast<std::uint8_t>((index * 13U) & 0xFFU);
    }
    std::memcpy(host_view.host_ptr, expected.data(), expected.size());

    graph::gpu::accel::DeviceBufferView device_view{};
    auto device_out = h2d.Transfer(host_view,
                                   std::integral_constant<std::size_t, 0>{},
                                   std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_out.has_value());
    device_view = *device_out;

    graph::gpu::accel::HostPinnedBufferView roundtrip_view{};
    auto roundtrip_out = d2h.Transfer(device_view,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(roundtrip_out.has_value());
    roundtrip_view = *roundtrip_out;

    std::vector<std::uint8_t> actual(kBytes, 0);
    std::memcpy(actual.data(), roundtrip_view.host_ptr, actual.size());
    EXPECT_EQ(actual, expected);
}

} // namespace
