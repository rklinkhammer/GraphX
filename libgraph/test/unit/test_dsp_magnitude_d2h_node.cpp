// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/DspMagnitudeD2HNode.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

constexpr std::size_t kPacketSize = 256;
constexpr std::uint64_t kQueueId = 5;
constexpr std::uint32_t kDeviceId = 2;

using NodeType = dsp::DspMagnitudeD2HNode<kPacketSize>;
using MagnitudePacketType = NodeType::MagnitudePacketType;
using TokenType = NodeType::TokenType;

class FakeMetalTransfer final
    : public graph::gpu::metal::capabilities::IMetalTransferCapability {
public:
    bool EnqueueH2D(const graph::gpu::accel::HostPinnedBufferView&,
                    graph::gpu::accel::DeviceBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool EnqueueD2H(const graph::gpu::accel::DeviceBufferView& src,
                    graph::gpu::accel::HostPinnedBufferView& dst,
                    std::uint64_t queue_id,
                    graph::gpu::accel::TransferTicket& out_ticket) override {
        if (!allow_d2h || queue_id == 0 ||
            !graph::gpu::accel::IsValidView(src) ||
            !graph::gpu::accel::IsValidView(dst)) {
            return false;
        }

        const auto copy_bytes = std::min(src.bytes, dst.bytes);
        std::memcpy(dst.host_ptr, src.device_ptr, static_cast<std::size_t>(copy_bytes));

        out_ticket.backend = graph::gpu::accel::BackendKind::Metal;
        out_ticket.transfer_id = next_transfer_id_++;
        out_ticket.execution_queue_id = queue_id;
        out_ticket.completion_event = next_event_id_++;
        out_ticket.src_device = src;
        out_ticket.dst_host = dst;

        ++d2h_count;
        last_queue_id = queue_id;
        last_src = src;
        last_dst = dst;
        return true;
    }

    bool EnqueueD2D(const graph::gpu::accel::DeviceBufferView&,
                    graph::gpu::accel::DeviceBufferView&,
                    std::uint64_t,
                    graph::gpu::accel::TransferTicket&) override {
        return false;
    }

    bool allow_d2h{true};
    std::size_t d2h_count{0};
    std::uint64_t last_queue_id{0};
    graph::gpu::accel::DeviceBufferView last_src{};
    graph::gpu::accel::HostPinnedBufferView last_dst{};

private:
    std::uint64_t next_transfer_id_{1};
    std::uint64_t next_event_id_{100};
};

struct DeviceMagnitudeStorage {
    std::vector<float> magnitudes;
    TokenType token;
};

DeviceMagnitudeStorage MakeDeviceMagnitudeToken() {
    DeviceMagnitudeStorage storage{};
    storage.magnitudes.resize(dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount, 0.25f);
    storage.magnitudes[5] = 10.0f;
    storage.magnitudes[17] = 3.0f;

    MagnitudePacketType packet{};
    packet.packet_number = 42;
    packet.num_accumulated_packets = 3;
    packet.sample_rate_hz = 48000.0;
    packet.window_type = 2;
    packet.valid = false;

    storage.token.token_id = 99;
    storage.token.sidecar = packet;
    storage.token.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    storage.token.device_view.device_ptr = storage.magnitudes.data();
    storage.token.device_view.bytes = dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBytes;
    storage.token.device_view.dtype = graph::gpu::accel::DataType::Float32;
    storage.token.device_view.layout = dsp::DspGpuBufferLayout<kPacketSize>::MagnitudeTensorLayout();
    storage.token.device_view.device_id = kDeviceId;
    storage.token.device_view.execution_queue_id = kQueueId;
    storage.token.device_view.ready_event = 11;
    storage.token.has_device_view = true;
    storage.token.kernel_ticket.backend = graph::gpu::accel::BackendKind::Metal;
    storage.token.kernel_ticket.kernel_id = 123;
    storage.token.kernel_ticket.execution_queue_id = kQueueId;
    storage.token.kernel_ticket.completion_event = 11;
    storage.token.has_kernel_ticket = true;
    return storage;
}

void RegisterFakeCapabilities(graph::CapabilityBus& bus,
                              const std::shared_ptr<FakeMetalTransfer>& transfer) {
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);
}

}  // namespace

TEST(DspMagnitudeD2HNodeTest, PreservesTokenMetadataAndReconstructsMagnitudePacket) {
    auto transfer = std::make_shared<FakeMetalTransfer>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceMagnitudeToken();
    const auto output = node.Transfer(storage.token,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(output.has_value());

    EXPECT_EQ(output->token_id, storage.token.token_id);
    EXPECT_EQ(output->sidecar.packet_number, storage.token.sidecar.packet_number);
    EXPECT_EQ(output->sidecar.num_accumulated_packets,
              storage.token.sidecar.num_accumulated_packets);
    EXPECT_DOUBLE_EQ(output->sidecar.sample_rate_hz, storage.token.sidecar.sample_rate_hz);
    EXPECT_EQ(output->sidecar.window_type, storage.token.sidecar.window_type);
    EXPECT_TRUE(output->has_device_view);
    EXPECT_TRUE(output->has_host_view);
    EXPECT_TRUE(output->has_transfer_ticket);
    EXPECT_TRUE(output->has_kernel_ticket);
    EXPECT_EQ(output->kernel_ticket.kernel_id, storage.token.kernel_ticket.kernel_id);

    ASSERT_TRUE(graph::gpu::accel::IsValidView(output->host_view));
    ASSERT_TRUE(graph::gpu::accel::IsValidTransferTicket(output->transfer_ticket));
    EXPECT_EQ(output->host_view.bytes, dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBytes);
    EXPECT_EQ(output->host_view.layout.rank, 1u);
    EXPECT_EQ(output->host_view.layout.shape[0],
              dsp::DspGpuBufferLayout<kPacketSize>::kMagnitudeBinCount);

    EXPECT_TRUE(output->sidecar.valid);
    EXPECT_TRUE(output->sidecar.IsValid());
    EXPECT_FLOAT_EQ(output->sidecar.magnitudes[0], 0.25f);
    EXPECT_FLOAT_EQ(output->sidecar.magnitudes[5], 10.0f);
    EXPECT_FLOAT_EQ(output->sidecar.magnitudes[17], 3.0f);
    EXPECT_EQ(output->sidecar.peak_bin, 5u);
    EXPECT_FLOAT_EQ(output->sidecar.peak_magnitude, 10.0f);
    EXPECT_FLOAT_EQ(output->sidecar.peak_frequency_hz, 937.5f);

    EXPECT_EQ(transfer->d2h_count, 1u);
    EXPECT_EQ(transfer->last_queue_id, kQueueId);
    EXPECT_TRUE(graph::gpu::accel::IsValidView(node.last_host_view()));
    EXPECT_TRUE(graph::gpu::accel::IsValidView(node.last_device_view()));
    EXPECT_TRUE(graph::gpu::accel::IsValidTransferTicket(node.last_transfer_ticket()));
    EXPECT_FLOAT_EQ(node.last_packet().peak_magnitude, 10.0f);
}

TEST(DspMagnitudeD2HNodeTest, FailsWithoutDeviceView) {
    auto transfer = std::make_shared<FakeMetalTransfer>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    TokenType token{};
    token.token_id = 3;
    token.sidecar = MagnitudePacketType{};
    const auto output = node.Transfer(token,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(transfer->d2h_count, 0u);
}

TEST(DspMagnitudeD2HNodeTest, FailsWhenTransferCapabilityUnavailable) {
    graph::CapabilityBus bus;

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    EXPECT_FALSE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceMagnitudeToken();
    const auto output = node.Transfer(storage.token,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
}

TEST(DspMagnitudeD2HNodeTest, FailsWhenTransferRejectsCopy) {
    auto transfer = std::make_shared<FakeMetalTransfer>();
    transfer->allow_d2h = false;
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceMagnitudeToken();
    const auto output = node.Transfer(storage.token,
                                      std::integral_constant<std::size_t, 0>{},
                                      std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(output.has_value());
    EXPECT_EQ(transfer->d2h_count, 0u);
}

TEST(DspMagnitudeD2HNodeTest, DiagnosticsExposeCopyAndPeakEvidence) {
    auto transfer = std::make_shared<FakeMetalTransfer>();
    graph::CapabilityBus bus;
    RegisterFakeCapabilities(bus, transfer);

    NodeType node;
    node.Configure(graph::JsonView(nlohmann::json{{"queue_id", kQueueId}}));
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    auto storage = MakeDeviceMagnitudeToken();
    ASSERT_TRUE(node.Transfer(storage.token,
                              std::integral_constant<std::size_t, 0>{},
                              std::integral_constant<std::size_t, 0>{}));

    const auto diagnostics = node.GetDiagnostics().Raw();
    EXPECT_TRUE(diagnostics.at("has_host_view").get<bool>());
    EXPECT_TRUE(diagnostics.at("has_device_view").get<bool>());
    EXPECT_TRUE(diagnostics.at("has_transfer_ticket").get<bool>());
    EXPECT_EQ(diagnostics.at("backend").get<std::string>(), "Metal");
    EXPECT_EQ(diagnostics.at("peak_bin").get<std::size_t>(), 5u);
    EXPECT_FLOAT_EQ(diagnostics.at("peak_frequency_hz").get<float>(), 937.5f);
    EXPECT_FLOAT_EQ(diagnostics.at("peak_magnitude").get<float>(), 10.0f);
}

TEST(DspMagnitudeD2HNodeTest, PluginRegistrationExposesDspMagnitudeD2HNode256) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);
    ASSERT_TRUE(provider->IsNodeTypeAvailable("DspMagnitudeD2HNode<256>"));

    auto node = provider->CreateNodeExpected("DspMagnitudeD2HNode<256>");
    ASSERT_TRUE(node);
    auto wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(
        std::make_shared<graph::NodeFacadeAdapter>(std::move(node).value()));
    auto concrete = wrapper->GetNode<NodeType>();
    ASSERT_NE(concrete, nullptr);
}
