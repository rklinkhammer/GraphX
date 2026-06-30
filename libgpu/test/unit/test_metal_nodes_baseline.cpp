// SPDX-License-Identifier: MIT

/**
 * @file test_metal_nodes_baseline.cpp
 * @brief Test Metal Nodes Baseline GPU acceleration support.
 *
 * @details Provides GPU test coverage for accelerator contracts and runtime behavior. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "gpu/metal/nodes/DeviceKernelNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceReduceNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceShardNodeMetal.hpp"
#include "gpu/metal/nodes/DeviceTransformNodeMetal.hpp"
#include "gpu/metal/nodes/LeaseReleaseNodeMetal.hpp"
#include "gpu/metal/nodes/PeerCopyNodeMetal.hpp"
#include "gpu/metal/nodes/QueueSyncNodeMetal.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/INode.hpp"

namespace {

/**
 * @class RecordingMetalMemoryPool
 * @brief Recording metal memory pool implementation for GraphX.
 */
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

    [[nodiscard]] MemoryPoolSnapshot Snapshot() const override {
        return {};
    }

    bool should_succeed{true};
    std::size_t release_count{0};
    std::uint64_t last_allocation_id{0};
};

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
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto transfer = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>();
    auto kernel = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>();
    auto telemetry = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>();

    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);

    graph::gpu::metal::nodes::DeviceTransformNodeMetal transform;
    graph::gpu::metal::nodes::DeviceReduceNodeMetal reduce;
    graph::gpu::metal::nodes::QueueSyncNodeMetal sync;
    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;
    graph::gpu::metal::nodes::PeerCopyNodeMetal peer_copy;

    ASSERT_TRUE(transform.BindGpuCapabilities(bus));
    ASSERT_TRUE(reduce.BindGpuCapabilities(bus));
    ASSERT_TRUE(sync.BindGpuCapabilities(bus));
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));
    ASSERT_TRUE(peer_copy.BindGpuCapabilities(bus));

    transform.ConfigureKernel(7U, "metal_transform", 0U, 3U);
    reduce.ConfigureKernel(8U, "metal_reduce", 0U, 4U);
    sync.SetQueue(5U);
    shard.ConfigureShard(0U, 2U);

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(128, 0, input_lease));
    auto* input_bytes = static_cast<std::byte*>(input_lease.device_view.device_ptr);
    ASSERT_NE(input_bytes, nullptr);
    for (std::size_t index = 0; index < 128; ++index) {
        input_bytes[index] = std::byte{static_cast<std::uint8_t>(index)};
    }

    graph::gpu::accel::DeviceBufferView input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
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
    auto* transformed_bytes = static_cast<const std::byte*>(transformed->device_ptr);
    ASSERT_NE(transformed_bytes, nullptr);
    for (std::size_t index = 0; index < 128; ++index) {
        const auto expected = static_cast<std::uint8_t>(index);
        EXPECT_EQ(static_cast<std::uint8_t>(transformed_bytes[index]), expected);
    }

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
    EXPECT_NE(peer_copied->device_ptr, sharded->device_ptr);

}

TEST(GpuMetalNodeBaseline, DeviceKernelNodeAllocatesConfiguredOutputAndLaunchesDescriptor) {
    graph::CapabilityBus bus;
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto kernel = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>();
    auto telemetry = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>();

    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);

    graph::gpu::metal::nodes::DeviceKernelNodeMetal device_kernel;
    ASSERT_TRUE(device_kernel.BindGpuCapabilities(bus));

    graph::gpu::metal::capabilities::MetalKernelDescriptor descriptor{};
    descriptor.kernel_id = 81U;
    descriptor.function_name = "graphx_test_device_kernel";
    descriptor.source_kind = graph::gpu::metal::capabilities::MetalKernelSourceKind::Builtin;
    descriptor.arg_layout = {
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::ReadOnly},
        graph::gpu::metal::capabilities::MetalKernelArgDescriptor{
            graph::gpu::metal::capabilities::MetalKernelArgKind::DeviceBuffer,
            graph::gpu::metal::capabilities::MetalKernelArgAccess::WriteOnly},
    };
    descriptor.dispatch.default_grid_x = 16U;
    descriptor.dispatch.default_block_x = 1U;
    device_kernel.ConfigureKernelDescriptor(descriptor, 0U, 3U);
    device_kernel.SetOutputBytes(64U);

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(128, 0, input_lease));
    auto input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.dtype = graph::gpu::accel::DataType::UInt8;
    input.layout.rank = 1;
    input.layout.shape[0] = 128;
    input.layout.stride[0] = 1;
    input.device_id = 0;
    input.execution_queue_id = 3U;
    input.ready_event = 11U;

    auto output = device_kernel.Transfer(input,
                                         std::integral_constant<std::size_t, 0>{},
                                         std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(output->bytes, 64U);
    EXPECT_EQ(output->execution_queue_id, 3U);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(*output));
    EXPECT_TRUE(graph::gpu::accel::IsValidLease(device_kernel.last_output_lease()));
    EXPECT_TRUE(graph::gpu::accel::IsValidKernelTicket(device_kernel.last_kernel_ticket()));
    EXPECT_EQ(device_kernel.last_kernel_ticket().kernel_id, 81U);
    EXPECT_EQ(device_kernel.last_kernel_ticket().arg_count, 2U);
    EXPECT_EQ(device_kernel.last_kernel_ticket().launch.grid_x, 16U);
    EXPECT_EQ(telemetry->KernelSamples(), 1U);
}

TEST(GpuMetalNodeBaseline, DeviceShardNodeCopiesSelectedShardSliceAndUpdatesShape) {
    graph::CapabilityBus bus;
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto transfer = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>();
    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);

    graph::gpu::metal::nodes::DeviceShardNodeMetal shard;
    ASSERT_TRUE(shard.BindGpuCapabilities(bus));
    shard.ConfigureShard(2U, 4U);

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(16, 0, input_lease));
    auto* input_bytes = static_cast<std::byte*>(input_lease.device_view.device_ptr);
    ASSERT_NE(input_bytes, nullptr);
    for (std::size_t index = 0; index < 16; ++index) {
        input_bytes[index] = std::byte{static_cast<std::uint8_t>(index)};
    }

    auto input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.dtype = graph::gpu::accel::DataType::UInt8;
    input.layout.rank = 1;
    input.layout.shape[0] = 16;
    input.layout.stride[0] = 1;
    input.execution_queue_id = 7U;

    auto sharded = shard.Transfer(input,
                                  std::integral_constant<std::size_t, 0>{},
                                  std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(sharded.has_value());
    EXPECT_EQ(sharded->bytes, 4U);
    EXPECT_EQ(sharded->layout.shape[0], 4U);

    auto* shard_bytes = static_cast<const std::byte*>(sharded->device_ptr);
    ASSERT_NE(shard_bytes, nullptr);
    EXPECT_EQ(static_cast<std::uint8_t>(shard_bytes[0]), 8U);
    EXPECT_EQ(static_cast<std::uint8_t>(shard_bytes[1]), 9U);
    EXPECT_EQ(static_cast<std::uint8_t>(shard_bytes[2]), 10U);
    EXPECT_EQ(static_cast<std::uint8_t>(shard_bytes[3]), 11U);
}

TEST(GpuMetalNodeBaseline, PeerCopyNodeAllocatesDestinationAndCopiesBytes) {
    graph::CapabilityBus bus;
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto transfer = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>();
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);

    graph::gpu::metal::nodes::PeerCopyNodeMetal peer_copy;
    peer_copy.SetQueue(6U);
    ASSERT_TRUE(peer_copy.BindGpuCapabilities(bus));

    graph::gpu::accel::BufferLease input_lease{};
    ASSERT_TRUE(memory_pool->AllocateDevice(8, 0, input_lease));
    auto* input_bytes = static_cast<std::byte*>(input_lease.device_view.device_ptr);
    ASSERT_NE(input_bytes, nullptr);
    for (std::size_t index = 0; index < 8; ++index) {
        input_bytes[index] = std::byte{static_cast<std::uint8_t>(0x10U + index)};
    }

    auto input = input_lease.device_view;
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.dtype = graph::gpu::accel::DataType::UInt8;
    input.layout.rank = 1;
    input.layout.shape[0] = 8;
    input.layout.stride[0] = 1;

    auto copied = peer_copy.Transfer(input,
                                     std::integral_constant<std::size_t, 0>{},
                                     std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(copied.has_value());
    EXPECT_NE(copied->device_ptr, input.device_ptr);
    EXPECT_EQ(copied->bytes, input.bytes);

    auto* copied_bytes = static_cast<const std::byte*>(copied->device_ptr);
    ASSERT_NE(copied_bytes, nullptr);
    for (std::size_t index = 0; index < 8; ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(copied_bytes[index]),
                  static_cast<std::uint8_t>(0x10U + index));
    }
}

} // namespace
