// SPDX-License-Identifier: MIT

/**
 * @file test_sar_runtime_helpers.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SarMessages.hpp"

#include <cstdint>

namespace {

TEST(SarRuntimeHelpersTest, ElapsedUsReturnsAtLeastOneMicrosecond) {
    const auto start = sar::runtime::SteadyClock::now();
    const auto elapsed_us = sar::runtime::ElapsedUs(start);
    EXPECT_GE(elapsed_us, 1u);
}

TEST(SarRuntimeHelpersTest, ResolveDiagnosticsSinkReturnsNullForNullGraphManager) {
    const std::shared_ptr<graph::GraphManager> graph_manager;
    auto sink = sar::runtime::ResolveDiagnosticsSink(graph_manager);
    EXPECT_EQ(sink, nullptr);
}

TEST(SarRuntimeHelpersTest, OpaqueHostPointerReturnsTransportSentinel) {
    EXPECT_EQ(
        sar::runtime::OpaqueHostPointer(),
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1u)));
}

TEST(SarRuntimeHelpersTest, OpaqueReadyEventNotSignaledReturnsTransportSentinel) {
    EXPECT_EQ(sar::runtime::OpaqueReadyEventNotSignaled(), 0u);
}

TEST(SarRuntimeHelpersTest, NextOpaqueEventIdReturnsMonotonicOpaqueIds) {
    const auto first = sar::runtime::NextOpaqueEventId();
    const auto second = sar::runtime::NextOpaqueEventId();

    EXPECT_GT(first, 0u);
    EXPECT_GT(second, first);
}

TEST(SarRuntimeHelpersTest, SyntheticDevicePointerUsesOpaqueByteAndSequenceToken) {
    constexpr std::uint64_t bytes = 1024u;
    constexpr std::uint64_t sequence = 7u;
    const auto expected =
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(((bytes + 1u) << 8u) |
                                                            ((sequence + 1u) & 0xFFu)));

    EXPECT_EQ(sar::runtime::SyntheticDevicePointer(bytes, sequence), expected);
}

TEST(SarRuntimeHelpersTest, SyntheticDevicePointerOverloadsUseViewByteCountOnly) {
    graph::gpu::accel::HostPinnedBufferView host_view{};
    host_view.bytes = 4096u;

    graph::gpu::accel::DeviceBufferView device_view{};
    device_view.bytes = host_view.bytes;
    device_view.device_id = 99u;

    constexpr std::uint64_t sequence = 3u;
    const auto expected = sar::runtime::SyntheticDevicePointer(host_view.bytes, sequence);

    EXPECT_EQ(sar::runtime::SyntheticDevicePointer(host_view, sequence), expected);
    EXPECT_EQ(sar::runtime::SyntheticDevicePointer(device_view, sequence), expected);
}

TEST(SarRuntimeHelpersTest, TransportHelpersDoNotMutateSidecarIdentity) {
    sar::SarAccelControlToken token{};
    token.sidecar.sequence_id = 77u;
    token.sidecar.batch_id = 9u;
    token.sidecar.tile_id = 3u;

    const auto original_sidecar = token.sidecar;
    token.host_view.host_ptr = sar::runtime::OpaqueHostPointer();
    token.device_view.device_ptr = sar::runtime::SyntheticDevicePointer(2048u, 5u);
    token.device_view.ready_event = sar::runtime::OpaqueReadyEventNotSignaled();
    token.transfer_ticket.completion_event = sar::runtime::NextOpaqueEventId();

    EXPECT_EQ(token.sidecar.sequence_id, original_sidecar.sequence_id);
    EXPECT_EQ(token.sidecar.batch_id, original_sidecar.batch_id);
    EXPECT_EQ(token.sidecar.tile_id, original_sidecar.tile_id);
}

} // namespace
