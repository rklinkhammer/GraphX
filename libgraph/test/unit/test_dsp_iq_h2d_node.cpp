// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/DspIqH2DNode.hpp"
#include "dsp/IqPacket.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/Message.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

constexpr std::size_t kPacketSize = 256;

using IqPacketType = dsp::IqPacket<float, kPacketSize>;
using NodeType = dsp::DspIqH2DNode<kPacketSize>;
using TokenType = NodeType::TokenType;

class FakeMetalMemoryPool final
    : public graph::gpu::metal::capabilities::IMetalMemoryPoolCapability {
public:
    bool AllocateDevice(std::uint64_t bytes,
                        std::uint32_t device_id,
                        graph::gpu::accel::BufferLease& out_lease) override {
        if (!allow_allocate || bytes == 0) {
            return false;
        }

        const auto allocation_id = next_allocation_id_++;
        auto [it, inserted] = device_allocations_.emplace(
            allocation_id,
            std::vector<std::byte>(static_cast<std::size_t>(bytes), std::byte{0}));
        if (!inserted) {
            return false;
        }

        out_lease.pool_id = 77;
        out_lease.allocation_id = allocation_id;
        out_lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
        out_lease.device_view.backend = graph::gpu::accel::BackendKind::Metal;
        out_lease.device_view.device_id = device_id;
        out_lease.device_view.device_ptr = it->second.data();
        out_lease.device_view.bytes = bytes;
        out_lease.device_view.dtype = graph::gpu::accel::DataType::Float32;
        out_lease.device_view.layout = dsp::IqPacketDeviceLayout<kPacketSize>();
        ++allocate_device_count;
        last_device_id = device_id;
        last_bytes = bytes;
        return true;
    }

    bool AllocateShared(std::uint64_t, std::uint32_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool AllocateHost(std::uint64_t, graph::gpu::accel::BufferLease&) override {
        return false;
    }

    bool Release(const graph::gpu::accel::BufferLease& lease) override {
        return device_allocations_.erase(lease.allocation_id) != 0;
    }

    [[nodiscard]] MemoryPoolSnapshot Snapshot() const override {
        MemoryPoolSnapshot snapshot{};
        snapshot.allocation_count = allocate_device_count;
        return snapshot;
    }

    bool allow_allocate{true};
    std::size_t allocate_device_count{0};
    std::uint32_t last_device_id{0};
    std::uint64_t last_bytes{0};

private:
    std::uint64_t next_allocation_id_{1};
    std::unordered_map<std::uint64_t, std::vector<std::byte>> device_allocations_{};
};

class FakeMetalTransfer final
    : public graph::gpu::metal::capabilities::IMetalTransferCapability {
public:
    bool EnqueueH2D(const graph::gpu::accel::HostPinnedBufferView& src,
                    graph::gpu::accel::DeviceBufferView& dst,
                    std::uint64_t queue_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (!allow_h2d || queue_id == 0 ||
            !graph::gpu::accel::IsValidView(src) ||
            !graph::gpu::accel::IsValidView(dst)) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.device_ptr, src.host_ptr, static_cast<std::size_t>(copy_bytes));
        dst.ready_event = next_event_id_++;

        out_ticket.backend = graph::gpu::accel::BackendKind::Metal;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = queue_id;
        out_ticket.completion_event = dst.ready_event;
        out_ticket.src_host = src;
        out_ticket.dst_device = dst;

        ++h2d_count;
        last_queue_id = queue_id;
        last_src = src;
        last_dst = dst;
        return true;
    }

    bool EnqueueD2H(const graph::gpu::accel::DeviceBufferView&,
                    graph::gpu::accel::HostPinnedBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool EnqueueD2D(const graph::gpu::accel::DeviceBufferView&,
                    graph::gpu::accel::DeviceBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool allow_h2d{true};
    std::size_t h2d_count{0};
    std::uint64_t last_queue_id{0};
    graph::gpu::accel::HostPinnedBufferView last_src{};
    graph::gpu::accel::DeviceBufferView last_dst{};

private:
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{100};
};

TokenType MakeIqToken() {
    IqPacketType packet;
    packet.packet_number = 42;
    packet.sample_rate_hz = 48000.0;
    packet.samples[0] = {1.0f, -1.0f};
    packet.samples[1] = {0.5f, 0.25f};

    TokenType token{};
    token.token_id = 99;
    token.sidecar = graph::message::Message(packet);
    return token;
}

void RegisterFakeCapabilities(graph::CapabilityBus& bus,
                              const std::shared_ptr<FakeMetalMemoryPool>& memory_pool,
                              const std::shared_ptr<FakeMetalTransfer>& transfer) {
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);
}

}  // namespace

TEST(DspIqH2DNodeTest, PreservesTokenSidecarAndCopiesIqLayout) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto transfer = std::make_shared<FakeMetalTransfer>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", 5}, {"device_id", 2}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    const auto input = MakeIqToken();
    const auto output = node.Transfer(input,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(output.has_value());

    const auto& input_packet = input.sidecar.template get<IqPacketType>();
    const auto& output_packet = output->sidecar.template get<IqPacketType>();
    EXPECT_EQ(output->token_id, input.token_id);
    EXPECT_EQ(output_packet.packet_number, input_packet.packet_number);
    EXPECT_FLOAT_EQ(output_packet.samples[0].real(), 1.0f);
    EXPECT_FLOAT_EQ(output_packet.samples[0].imag(), -1.0f);
    EXPECT_TRUE(output->has_host_view);
    EXPECT_TRUE(output->has_device_view);
    EXPECT_TRUE(output->has_lease);
    EXPECT_TRUE(output->has_transfer_ticket);

    ASSERT_TRUE(graph::gpu::accel::IsValidView(output->host_view));
    ASSERT_TRUE(graph::gpu::accel::IsValidView(output->device_view));
    ASSERT_TRUE(graph::gpu::accel::IsValidLease(output->lease));
    ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(output->transfer_ticket));

    EXPECT_EQ(output->host_view.bytes, dsp::IqPacketDeviceBytes<kPacketSize>());
    EXPECT_EQ(output->host_view.layout.rank, 2);
    EXPECT_EQ(output->host_view.layout.shape[0], kPacketSize);
    EXPECT_EQ(output->host_view.layout.shape[1], 2u);
    EXPECT_EQ(output->device_view.layout.shape[0], kPacketSize);
    EXPECT_EQ(output->device_view.layout.shape[1], 2u);
    EXPECT_EQ(output->device_view.device_id, 2u);
    EXPECT_EQ(output->transfer_ticket.execution_queue_id, 5u);

    auto* host_floats = static_cast<const float*>(output->host_view.host_ptr);
    auto* device_floats = static_cast<const float*>(output->device_view.device_ptr);
    ASSERT_NE(host_floats, nullptr);
    ASSERT_NE(device_floats, nullptr);
    EXPECT_FLOAT_EQ(host_floats[0], 1.0f);
    EXPECT_FLOAT_EQ(host_floats[1], -1.0f);
    EXPECT_FLOAT_EQ(host_floats[2], 0.5f);
    EXPECT_FLOAT_EQ(host_floats[3], 0.25f);
    EXPECT_FLOAT_EQ(device_floats[0], host_floats[0]);
    EXPECT_FLOAT_EQ(device_floats[1], host_floats[1]);

    EXPECT_EQ(memory_pool->allocate_device_count, 1u);
    EXPECT_EQ(memory_pool->last_bytes, dsp::IqPacketDeviceBytes<kPacketSize>());
    EXPECT_EQ(transfer->h2d_count, 1u);
}

TEST(DspIqH2DNodeTest, FailsWithoutRequiredIqPacketSidecar) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto transfer = std::make_shared<FakeMetalTransfer>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", 1}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    TokenType token{};
    token.token_id = 3;
    token.sidecar = graph::message::Message(17);

    auto output = node.Transfer(token,
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(memory_pool->allocate_device_count, 0u);
    EXPECT_EQ(transfer->h2d_count, 0u);
}

TEST(DspIqH2DNodeTest, FailsWhenTransferCapabilityUnavailable) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    graph::CapabilityBus bus;
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", 1}}));
    EXPECT_FALSE(node.BindGpuCapabilities(bus));

    auto output = node.Transfer(MakeIqToken(),
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
}

TEST(DspIqH2DNodeTest, FailsWhenTransferRejectsCopy) {
    auto memory_pool = std::make_shared<FakeMetalMemoryPool>();
    auto transfer = std::make_shared<FakeMetalTransfer>();
    transfer->allow_h2d = false;
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, memory_pool, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", 1}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto output = node.Transfer(MakeIqToken(),
                                std::integral_constant<std::size_t, 0>{},
                                std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(memory_pool->allocate_device_count, 1u);
    EXPECT_EQ(transfer->h2d_count, 0u);
}

TEST(DspIqH2DNodeTest, PluginRegistrationExposesDspIqH2DNode256) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);
    ASSERT_TRUE(provider->IsNodeTypeAvailable("DspIqH2DNode<256>"));

    auto node = provider->CreateNodeExpected("DspIqH2DNode<256>");
    ASSERT_TRUE(node);
    auto wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(
        std::make_shared<graph::NodeFacadeAdapter>(std::move(node).value()));
    auto concrete = wrapper->GetNode<NodeType>();
    ASSERT_NE(concrete, nullptr);
}
