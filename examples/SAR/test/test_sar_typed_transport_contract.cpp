// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "sar/SarMessages.hpp"

#include <array>
#include <cstddef>
#include <type_traits>

namespace {

struct TypedFakeTransport {
    std::array<std::byte, 32> host_storage{};
    std::array<std::byte, 32> device_storage{};
    std::uint64_t event_id{};

    graph::gpu::accel::HostPinnedBufferView HostView() {
        graph::gpu::accel::HostPinnedBufferView view{};
        view.backend = graph::gpu::accel::BackendKind::Metal;
        view.host_ptr = host_storage.data();
        view.bytes = host_storage.size();
        return view;
    }

    graph::gpu::accel::DeviceBufferView DeviceView() {
        graph::gpu::accel::DeviceBufferView view{};
        view.backend = graph::gpu::accel::BackendKind::Metal;
        view.device_ptr = device_storage.data();
        view.bytes = device_storage.size();
        view.ready_event = event_id;
        return view;
    }

    graph::gpu::accel::TransferTicket TransferTicket() const {
        graph::gpu::accel::TransferTicket ticket{};
        ticket.backend = graph::gpu::accel::BackendKind::Metal;
        ticket.completion_event = event_id;
        return ticket;
    }
};

sar::SarControlToken MakeSemanticToken(std::uint64_t sequence_id) {
    sar::SarControlToken token{};
    token.sidecar.sequence_id = sequence_id;
    token.sidecar.batch_id = 10u;
    token.sidecar.aperture_id = 5u;
    token.sidecar.tile_id = 2u;
    return token;
}

TEST(SarTypedTransportContractTest,
     IdenticalDomainPacketsRemainEquivalentAcrossTypedFakeTransports) {
    TypedFakeTransport first{.event_id = 100u};
    TypedFakeTransport second{.event_id = 200u};
    auto token_a = MakeSemanticToken(1234u);
    auto token_b = MakeSemanticToken(1234u);

    token_a.host_view = first.HostView();
    token_a.device_view = first.DeviceView();
    token_a.transfer_ticket = first.TransferTicket();
    token_b.host_view = second.HostView();
    token_b.device_view = second.DeviceView();
    token_b.transfer_ticket = second.TransferTicket();

    EXPECT_EQ(token_a.sidecar.sequence_id, token_b.sidecar.sequence_id);
    EXPECT_EQ(token_a.sidecar.batch_id, token_b.sidecar.batch_id);
    EXPECT_EQ(token_a.sidecar.aperture_id, token_b.sidecar.aperture_id);
    EXPECT_EQ(token_a.sidecar.tile_id, token_b.sidecar.tile_id);
    EXPECT_NE(token_a.host_view.host_ptr, token_b.host_view.host_ptr);
    EXPECT_NE(token_a.device_view.ready_event, token_b.device_view.ready_event);
}

TEST(SarTypedTransportContractTest, CanonicalTokenUsesDomainPacket) {
    static_assert(std::is_same_v<
                  sar::SarControlToken,
                  graph::gpu::accel::ControlToken<sar::SarPacket>>);
    SUCCEED();
}

} // namespace
