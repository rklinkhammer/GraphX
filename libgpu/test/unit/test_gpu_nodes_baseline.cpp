// SPDX-License-Identifier: MIT

/**
 * @file test_gpu_nodes_baseline.cpp
 * @brief Test GPU Nodes Baseline GPU acceleration support.
 *
 * @details Provides GPU test coverage for accelerator contracts and runtime behavior. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <type_traits>

#include "graph/INode.hpp"

#include "graph/CapabilityBus.hpp"

#include "gpu/cuda/nodes/LeaseReleaseNode.hpp"

namespace {

/**
 * @class RecordingCudaMemoryPool
 * @brief Recording cuda memory pool implementation for GraphX.
 */
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

} // namespace
