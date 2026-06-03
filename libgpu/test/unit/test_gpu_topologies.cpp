#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "capabilities/GraphCapability.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "test/PluginInfrastructure.hpp"

#include "gpu/cuda/capabilities/DefaultCudaCapabilities.hpp"
#include "gpu/cuda/nodes/D2HAsyncNode.hpp"
#include "gpu/cuda/nodes/H2DAsyncNode.hpp"
#include "gpu/cuda/nodes/HostEgressSinkNode.hpp"
#include "gpu/cuda/nodes/HostIngressPinnedSourceNode.hpp"
#include "gpu/sycl/capabilities/DefaultSyclCapabilities.hpp"
#include "gpu/sycl/nodes/D2HAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/H2DAsyncNodeSycl.hpp"
#include "gpu/sycl/nodes/HostEgressSinkNodeSycl.hpp"
#include "gpu/sycl/nodes/HostIngressPinnedSourceNodeSycl.hpp"

namespace {

constexpr std::uint8_t kSourcePattern = 0x5A;
constexpr std::uint8_t kDestinationPattern = 0xC3;

class TopologyTestCudaMemoryPool final : public graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                        graph::gpu::accel::BufferLease& out_lease) override {
        if (bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        auto [it, inserted] = device_allocations_.emplace(allocation_id,
                                                           std::vector<std::byte>(bytes, std::byte{0}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 101;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.device_view.backend = graph::gpu::accel::BackendKind::CUDA;
        out_lease.device_view.device_id = device_id;
        out_lease.device_view.bytes = bytes;
        out_lease.device_view.dtype = graph::gpu::accel::DataType::UInt8;
        out_lease.device_view.layout.rank = 1;
        out_lease.device_view.layout.shape[0] = bytes;
        out_lease.device_view.layout.stride[0] = 1;
        out_lease.device_view.device_ptr = static_cast<void*>(it->second.data());
        return true;
    }

    bool AllocatePinnedHost(std::uint64_t bytes,
                            graph::gpu::accel::BufferLease& out_lease) override {
        if (bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        const auto pattern = (host_allocation_count_++ == 0) ? kSourcePattern : kDestinationPattern;
        auto [it, inserted] = host_allocations_.emplace(
            allocation_id,
            std::vector<std::byte>(bytes, std::byte{pattern}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 102;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.host_view.backend = graph::gpu::accel::BackendKind::CUDA;
        out_lease.host_view.bytes = bytes;
        out_lease.host_view.dtype = graph::gpu::accel::DataType::UInt8;
        out_lease.host_view.layout.rank = 1;
        out_lease.host_view.layout.shape[0] = bytes;
        out_lease.host_view.layout.stride[0] = 1;
        out_lease.host_view.host_ptr = static_cast<void*>(it->second.data());
        out_lease.host_view.allocator_id = out_lease.pool_id;
        return true;
    }

    bool Release(const graph::gpu::accel::BufferLease& lease) override {
        if (lease.allocation_id == 0) {
            return false;
        }
        const auto removed_device = device_allocations_.erase(lease.allocation_id);
        const auto removed_host = host_allocations_.erase(lease.allocation_id);
        return removed_device != 0 || removed_host != 0;
    }

private:
    std::uint64_t next_allocation_id_{1};
    std::size_t host_allocation_count_{0};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_{};
};

class TopologyTestCudaTransfer final : public graph::gpu::cuda::capabilities::ICudaTransferCapability {
public:
    bool EnqueueH2D(const graph::gpu::accel::HostPinnedBufferView& src,
                    graph::gpu::accel::DeviceBufferView& dst,
                    std::uint64_t stream_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (stream_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(copy_bytes));
        ++h2d_count;

        out_ticket.backend = graph::gpu::accel::BackendKind::CUDA;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = stream_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_host = src;
        out_ticket.dst_device = dst;
        dst.ready_event = out_ticket.completion_event;
        return true;
    }

    bool EnqueueD2H(const graph::gpu::accel::DeviceBufferView& src,
                    graph::gpu::accel::HostPinnedBufferView& dst,
                    std::uint64_t stream_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (stream_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));
        ++d2h_count;

        out_ticket.backend = graph::gpu::accel::BackendKind::CUDA;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = stream_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_device = src;
        out_ticket.dst_host = dst;
        return true;
    }

    bool EnqueueD2D(const graph::gpu::accel::DeviceBufferView& src,
                    graph::gpu::accel::DeviceBufferView& dst,
                    std::uint64_t stream_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (stream_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.device_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

        out_ticket.backend = graph::gpu::accel::BackendKind::CUDA;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = stream_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_device = src;
        out_ticket.dst_device = dst;
        dst.ready_event = out_ticket.completion_event;
        return true;
    }

    std::size_t h2d_count{0};
    std::size_t d2h_count{0};

private:
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{1};
};

class TopologyTestSyclMemoryPool final : public graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t bytes, std::uint32_t device_id,
                        graph::gpu::accel::BufferLease& out_lease) override {
        if (bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        auto [it, inserted] = device_allocations_.emplace(allocation_id,
                                                           std::vector<std::byte>(bytes, std::byte{0}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 201;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.device_view.backend = graph::gpu::accel::BackendKind::SYCL;
        out_lease.device_view.device_id = device_id;
        out_lease.device_view.bytes = bytes;
        out_lease.device_view.dtype = graph::gpu::accel::DataType::UInt8;
        out_lease.device_view.layout.rank = 1;
        out_lease.device_view.layout.shape[0] = bytes;
        out_lease.device_view.layout.stride[0] = 1;
        out_lease.device_view.device_ptr = static_cast<void*>(it->second.data());
        return true;
    }

    bool AllocateShared(std::uint64_t bytes, std::uint32_t device_id,
                        graph::gpu::accel::BufferLease& out_lease) override {
        return AllocateDevice(bytes, device_id, out_lease);
    }

    bool AllocateHost(std::uint64_t bytes,
                      graph::gpu::accel::BufferLease& out_lease) override {
        if (bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        const auto pattern = (host_allocation_count_++ == 0) ? kSourcePattern : kDestinationPattern;
        auto [it, inserted] = host_allocations_.emplace(
            allocation_id,
            std::vector<std::byte>(bytes, std::byte{pattern}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 202;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.host_view.backend = graph::gpu::accel::BackendKind::SYCL;
        out_lease.host_view.bytes = bytes;
        out_lease.host_view.dtype = graph::gpu::accel::DataType::UInt8;
        out_lease.host_view.layout.rank = 1;
        out_lease.host_view.layout.shape[0] = bytes;
        out_lease.host_view.layout.stride[0] = 1;
        out_lease.host_view.host_ptr = static_cast<void*>(it->second.data());
        out_lease.host_view.allocator_id = out_lease.pool_id;
        return true;
    }

    bool Release(const graph::gpu::accel::BufferLease& lease) override {
        if (lease.allocation_id == 0) {
            return false;
        }
        const auto removed_device = device_allocations_.erase(lease.allocation_id);
        const auto removed_host = host_allocations_.erase(lease.allocation_id);
        return removed_device != 0 || removed_host != 0;
    }

private:
    std::uint64_t next_allocation_id_{1};
    std::size_t host_allocation_count_{0};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> host_allocations_{};
};

class TopologyTestSyclTransfer final : public graph::gpu::sycl::capabilities::ISyclTransferCapability {
public:
    bool EnqueueH2D(const graph::gpu::accel::HostPinnedBufferView& src,
                    graph::gpu::accel::DeviceBufferView& dst,
                    std::uint64_t queue_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (queue_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(copy_bytes));
        ++h2d_count;

        out_ticket.backend = graph::gpu::accel::BackendKind::SYCL;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = queue_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_host = src;
        out_ticket.dst_device = dst;
        dst.ready_event = out_ticket.completion_event;
        return true;
    }

    bool EnqueueD2H(const graph::gpu::accel::DeviceBufferView& src,
                    graph::gpu::accel::HostPinnedBufferView& dst,
                    std::uint64_t queue_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (queue_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));
        ++d2h_count;

        out_ticket.backend = graph::gpu::accel::BackendKind::SYCL;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = queue_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_device = src;
        out_ticket.dst_host = dst;
        return true;
    }

    bool EnqueueD2D(const graph::gpu::accel::DeviceBufferView& src,
                    graph::gpu::accel::DeviceBufferView& dst,
                    std::uint64_t queue_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (queue_id == 0) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.device_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

        out_ticket.backend = graph::gpu::accel::BackendKind::SYCL;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = queue_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_device = src;
        out_ticket.dst_device = dst;
        dst.ready_event = out_ticket.completion_event;
        return true;
    }

    std::size_t h2d_count{0};
    std::size_t d2h_count{0};

private:
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{1};
};

TEST(GpuTopology, GpuMinimalRoundTripCudaGraphExecutorBuilder) {
    auto graph_manager = std::make_shared<graph::GraphManager>();
    auto factory = test::PluginInfrastructure::GetFactory();

    auto ingress = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostIngressPinnedSourceNode"));
    auto h2d = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "H2DAsyncNode"));
    auto d2h = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "D2HAsyncNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostEgressSinkNode"));

    auto ingress_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(ingress);
    auto h2d_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(h2d);
    auto d2h_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(d2h);
    auto sink_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph_manager->AddNode(ingress_wrapper);
    graph_manager->AddNode(h2d_wrapper);
    graph_manager->AddNode(d2h_wrapper);
    graph_manager->AddNode(sink_wrapper);

    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode, 0,
                                                     graph::gpu::cuda::nodes::H2DAsyncNode, 0>(
        graph_manager,
        ingress_wrapper,
        h2d_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::cuda::nodes::H2DAsyncNode, 0,
                                                     graph::gpu::cuda::nodes::D2HAsyncNode, 0>(
        graph_manager,
        h2d_wrapper,
        d2h_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::cuda::nodes::D2HAsyncNode, 0,
                                                     graph::gpu::cuda::nodes::HostEgressSinkNode, 0>(
        graph_manager,
        d2h_wrapper,
        sink_wrapper)));

    auto ingress_node = ingress_wrapper->GetNode<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode>();
    auto sink_node = sink_wrapper->GetNode<graph::gpu::cuda::nodes::HostEgressSinkNode>();
    ASSERT_NE(ingress_node, nullptr);
    ASSERT_NE(sink_node, nullptr);

    ingress_node->StageNextBufferBytes(4096);

    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph_manager)
                        .Build();

    auto memory_pool = std::make_shared<TopologyTestCudaMemoryPool>();
    auto transfer = std::make_shared<TopologyTestCudaTransfer>();
    executor->Register<graph::gpu::cuda::capabilities::ICudaMemoryPoolCapability>(memory_pool);
    executor->Register<graph::gpu::cuda::capabilities::ICudaTransferCapability>(transfer);

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message << " " << init_result.error_details;

    auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message << " " << start_result.error_details;

    auto run_result = executor->Run();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto stop_result = executor->Stop();
    EXPECT_TRUE(stop_result.success) << stop_result.message << " " << stop_result.error_details;

    auto join_result = executor->Join();
    EXPECT_TRUE(join_result.success) << join_result.message << " " << join_result.error_details;

    EXPECT_EQ(sink_node->ConsumeCount(), 1U);
    EXPECT_EQ(sink_node->LastView().backend, graph::gpu::accel::BackendKind::CUDA);
    EXPECT_EQ(sink_node->LastView().bytes, 4096U);
    EXPECT_EQ(transfer->h2d_count, 1U);
    EXPECT_EQ(transfer->d2h_count, 1U);

    const auto* sink_data = static_cast<const std::byte*>(sink_node->LastView().host_ptr);
    ASSERT_NE(sink_data, nullptr);
    for (std::uint64_t index = 0; index < sink_node->LastView().bytes; ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(sink_data[index]), kSourcePattern);
    }
}

TEST(GpuTopology, GpuMinimalRoundTripSyclGraphExecutorBuilder) {
    auto graph_manager = std::make_shared<graph::GraphManager>();
    auto factory = test::PluginInfrastructure::GetFactory();

    auto ingress = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostIngressPinnedSourceNodeSycl"));
    auto h2d = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "H2DAsyncNodeSycl"));
    auto d2h = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "D2HAsyncNodeSycl"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "HostEgressSinkNodeSycl"));

    auto ingress_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(ingress);
    auto h2d_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(h2d);
    auto d2h_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(d2h);
    auto sink_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph_manager->AddNode(ingress_wrapper);
    graph_manager->AddNode(h2d_wrapper);
    graph_manager->AddNode(d2h_wrapper);
    graph_manager->AddNode(sink_wrapper);

    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::H2DAsyncNodeSycl, 0>(
        graph_manager,
        ingress_wrapper,
        h2d_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::H2DAsyncNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::D2HAsyncNodeSycl, 0>(
        graph_manager,
        h2d_wrapper,
        d2h_wrapper)));
    ASSERT_TRUE((test::PluginInfrastructure::AddEdge<graph::gpu::sycl::nodes::D2HAsyncNodeSycl, 0,
                                                     graph::gpu::sycl::nodes::HostEgressSinkNodeSycl, 0>(
        graph_manager,
        d2h_wrapper,
        sink_wrapper)));

    auto ingress_node = ingress_wrapper->GetNode<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl>();
    auto sink_node = sink_wrapper->GetNode<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl>();
    ASSERT_NE(ingress_node, nullptr);
    ASSERT_NE(sink_node, nullptr);

    ingress_node->StageNextBufferBytes(2048);

    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph_manager)
                        .Build();

    auto memory_pool = std::make_shared<TopologyTestSyclMemoryPool>();
    auto transfer = std::make_shared<TopologyTestSyclTransfer>();
    executor->Register<graph::gpu::sycl::capabilities::ISyclMemoryPoolCapability>(memory_pool);
    executor->Register<graph::gpu::sycl::capabilities::ISyclTransferCapability>(transfer);

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message << " " << init_result.error_details;

    auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message << " " << start_result.error_details;

    auto run_result = executor->Run();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto stop_result = executor->Stop();
    EXPECT_TRUE(stop_result.success) << stop_result.message << " " << stop_result.error_details;

    auto join_result = executor->Join();
    EXPECT_TRUE(join_result.success) << join_result.message << " " << join_result.error_details;

    EXPECT_EQ(sink_node->ConsumeCount(), 1U);
    EXPECT_EQ(sink_node->LastView().backend, graph::gpu::accel::BackendKind::SYCL);
    EXPECT_EQ(sink_node->LastView().bytes, 2048U);
    EXPECT_EQ(transfer->h2d_count, 1U);
    EXPECT_EQ(transfer->d2h_count, 1U);

    const auto* sink_data = static_cast<const std::byte*>(sink_node->LastView().host_ptr);
    ASSERT_NE(sink_data, nullptr);
    for (std::uint64_t index = 0; index < sink_node->LastView().bytes; ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(sink_data[index]), kSourcePattern);
    }
}

TEST(GpuTopology, PluginLoadedNodeFailsCleanlyWithoutRequiredCapabilities) {
    auto factory = test::PluginInfrastructure::GetFactory();
    auto h2d = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "H2DAsyncNode"));
    auto h2d_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(h2d);

    auto h2d_node = h2d_wrapper->GetNode<graph::gpu::cuda::nodes::H2DAsyncNode>();
    ASSERT_NE(h2d_node, nullptr);

    graph::CapabilityBus empty_bus;
    EXPECT_FALSE(h2d_node->BindGpuCapabilities(empty_bus));
}

TEST(GpuTopology, ExecutorInitFailsWhenGpuBootstrapDisabledAndCapabilitiesMissing) {
    auto graph_manager = std::make_shared<graph::GraphManager>();
    auto factory = test::PluginInfrastructure::GetFactory();

    auto h2d = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateDynamicNodeOrThrow(factory, "H2DAsyncNode"));
    auto h2d_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(h2d);
    graph_manager->AddNode(h2d_wrapper);

    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph_manager)
                        .Build();

    auto graph_capability = executor->GetCapability<capabilities::GraphCapability>();
    ASSERT_NE(graph_capability, nullptr);
    graph_capability->SetGpuBootstrapEnabled(false);

    auto init_result = executor->Init();
    EXPECT_FALSE(init_result.success);
    EXPECT_NE(init_result.message.find("ExecutionPolicyChain::OnInit() failed"), std::string::npos);
}

} // namespace
