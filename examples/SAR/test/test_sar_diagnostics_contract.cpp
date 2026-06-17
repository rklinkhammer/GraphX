// SPDX-License-Identifier: MIT

/**
 * @file test_sar_diagnostics_contract.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/SarDiagnosticsSinkNode.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

sar::SarAccelControlToken MakeStatus(
    std::uint64_t sequence_id,
    sar::SarFrameMarker marker,
    bool complete,
    std::uint32_t received_tiles,
    std::uint32_t duplicate_tiles,
    std::uint32_t missing_tiles,
    std::uint64_t bytes_h2d,
    std::uint64_t bytes_d2h,
    std::uint64_t kernel_dispatches,
    std::uint64_t fanin_wait_ms) {
    sar::SarAccelControlToken msg{};
    msg.sidecar.sequence_id = sequence_id;
    msg.sidecar.stream_id = 42;
    msg.sidecar.tile_id = 0;
    msg.sidecar.tile_count = 4;
    msg.sidecar.backend_id = 1;
    msg.sidecar.backend = sar::SarBackendKind::SimulatedDevice;
    msg.sidecar.marker = marker;
    msg.sidecar.synthetic = true;

    msg.sidecar.expected_tiles = 4;
    msg.sidecar.received_tiles = received_tiles;
    msg.sidecar.duplicate_tiles = duplicate_tiles;
    msg.sidecar.missing_tiles = missing_tiles;
    msg.sidecar.out_of_order_tiles = 2;
    msg.sidecar.bytes_h2d = bytes_h2d;
    msg.sidecar.bytes_d2h = bytes_d2h;
    msg.sidecar.kernel_dispatches = kernel_dispatches;
    msg.sidecar.transfer_h2d_time_us = 11;
    msg.sidecar.kernel_exec_time_us = 22;
    msg.sidecar.transfer_d2h_time_us = 33;
    msg.sidecar.watermark_seen = false;
    msg.sidecar.fanin_wait_ms = fanin_wait_ms;
    msg.sidecar.merge_complete = complete;
    return msg;
}

TEST(SarDiagnosticsContractTest, EmitsDeterministicMetricsFromTokenizedMergeBoundary) {
    sar::SarDiagnosticsSinkNode sink;

    graph::GraphMetrics graph_metrics{};
    graph_metrics.backpressure_events.store(7u, std::memory_order_relaxed);
    graph_metrics.peak_queue_depth.store(3u, std::memory_order_relaxed);
    sink.UpdateFromGraphMetrics(graph_metrics);

    ASSERT_TRUE(sink.Consume(
        MakeStatus(0, sar::SarFrameMarker::Data, false, 1, 0, 3, 1024, 1024, 1, 0),
        std::integral_constant<std::size_t, 0>{}));

    ASSERT_TRUE(sink.Consume(
        MakeStatus(32, sar::SarFrameMarker::EndOfStream, true, 4, 28, 0, 32768, 32768, 32, 32),
        std::integral_constant<std::size_t, 0>{}));

    const auto& diag = sink.last_diagnostics();
    EXPECT_EQ(diag.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diag.pulses_processed, 32u);
    EXPECT_EQ(diag.tiles_processed, 4u);
    EXPECT_EQ(diag.bytes_h2d, 32768u);
    EXPECT_EQ(diag.bytes_d2h, 32768u);
    EXPECT_EQ(diag.kernel_dispatches, 32u);
    EXPECT_EQ(diag.transfer_h2d_time_us, 11u);
    EXPECT_EQ(diag.kernel_exec_time_us, 22u);
    EXPECT_EQ(diag.transfer_d2h_time_us, 33u);
    EXPECT_EQ(diag.fanin_wait_ms, 32u);
    EXPECT_EQ(diag.e2e_latency_ms, 32u);
    EXPECT_EQ(diag.duplicate_tile_count, 28u);
    EXPECT_EQ(diag.missing_tile_count, 0u);
    EXPECT_EQ(diag.out_of_order_completion_count, 2u);
    EXPECT_EQ(diag.queue_backpressure_events, 7u);
    EXPECT_EQ(diag.peak_queue_depth, 3u);
}

TEST(SarDiagnosticsContractTest, SignalsCompletionOnCompleteEndOfStream) {
    sar::SarDiagnosticsSinkNode sink;
    bool completed = false;

    graph::CompletionCallbackProvider::CompletionNodeCallback callback;
    callback.SetOnComplete([&completed]() {
        completed = true;
    });
    ASSERT_TRUE(sink.SetCallbackProvider(&callback));

    ASSERT_TRUE(sink.Consume(
        MakeStatus(8, sar::SarFrameMarker::EndOfStream, true, 4, 0, 0, 8192, 8192, 8, 8),
        std::integral_constant<std::size_t, 0>{}));

    EXPECT_TRUE(completed);
}

TEST(SarDiagnosticsContractTest, DiagnosticsBoundaryConsumesTokenContract) {
    static_assert(std::is_same_v<
                  decltype(std::declval<sar::SarDiagnosticsSinkNode>().Consume(
                      std::declval<const sar::SarAccelControlToken&>(),
                      std::integral_constant<std::size_t, 0>{})),
                  bool>);
    SUCCEED();
}

TEST(SarDiagnosticsContractTest, DiagnosticsIdentityIsInvariantToTransportFieldMutation) {
    auto base = MakeStatus(12, sar::SarFrameMarker::Data, false, 1, 0, 3, 1024, 1024, 1, 0);
    base.sidecar.batch_id = 77u;
    base.sidecar.aperture_id = 12u;
    base.sidecar.pulse_range_start = 12u;
    base.sidecar.pulse_range_count = 1u;

    auto mutated = base;
    mutated.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0xBEEFu));
    mutated.transfer_ticket.completion_event = 99999u;
    mutated.kernel_ticket.completion_event = 88888u;

    sar::SarDiagnosticsSinkNode sink_a;
    sar::SarDiagnosticsSinkNode sink_b;

    ASSERT_TRUE(sink_a.Consume(base, std::integral_constant<std::size_t, 0>{}));
    ASSERT_TRUE(sink_b.Consume(mutated, std::integral_constant<std::size_t, 0>{}));

    const auto& diag_a = sink_a.last_diagnostics();
    const auto& diag_b = sink_b.last_diagnostics();

    EXPECT_EQ(diag_a.sidecar.sequence_id, diag_b.sidecar.sequence_id);
    EXPECT_EQ(diag_a.sidecar.batch_id, diag_b.sidecar.batch_id);
    EXPECT_EQ(diag_a.sidecar.aperture_id, diag_b.sidecar.aperture_id);
    EXPECT_EQ(diag_a.sidecar.pulse_range_start, diag_b.sidecar.pulse_range_start);
    EXPECT_EQ(diag_a.sidecar.pulse_range_count, diag_b.sidecar.pulse_range_count);
    EXPECT_EQ(diag_a.sidecar.stream_id, diag_b.sidecar.stream_id);
    EXPECT_EQ(diag_a.sidecar.tile_id, diag_b.sidecar.tile_id);
    EXPECT_EQ(diag_a.sidecar.tile_count, diag_b.sidecar.tile_count);
    EXPECT_EQ(diag_a.sidecar.backend_id, diag_b.sidecar.backend_id);
    EXPECT_EQ(diag_a.sidecar.backend, diag_b.sidecar.backend);
    EXPECT_EQ(diag_a.sidecar.marker, diag_b.sidecar.marker);
}

} // namespace
