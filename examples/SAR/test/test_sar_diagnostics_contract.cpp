#include <gtest/gtest.h>

#include "sar/SarDiagnosticsSinkNode.hpp"

#include <cstddef>

namespace {

sar::SarMergeStatusMessage MakeStatus(
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
    sar::SarMergeStatusMessage msg{};
    msg.envelope.sequence_id = sequence_id;
    msg.envelope.stream_id = 42;
    msg.envelope.tile_id = 0;
    msg.envelope.tile_count = 4;
    msg.envelope.backend_id = 1;
    msg.envelope.backend = sar::SarBackendKind::SimulatedDevice;
    msg.envelope.marker = marker;
    msg.envelope.synthetic = true;

    msg.expected_tiles = 4;
    msg.received_tiles = received_tiles;
    msg.duplicate_tiles = duplicate_tiles;
    msg.missing_tiles = missing_tiles;
    msg.out_of_order_tiles = 0;
    msg.bytes_h2d = bytes_h2d;
    msg.bytes_d2h = bytes_d2h;
    msg.kernel_dispatches = kernel_dispatches;
    msg.watermark_seen = false;
    msg.fanin_wait_ms = fanin_wait_ms;
    msg.complete = complete;
    return msg;
}

TEST(SarDiagnosticsContractTest, EmitsDeterministicMetricsFromMergeStatus) {
    sar::SarDiagnosticsSinkNode sink;

    ASSERT_TRUE(sink.Consume(
        MakeStatus(0, sar::SarFrameMarker::Data, false, 1, 0, 3, 1024, 1024, 1, 0),
        std::integral_constant<std::size_t, 0>{}));

    ASSERT_TRUE(sink.Consume(
        MakeStatus(32, sar::SarFrameMarker::EndOfStream, true, 4, 28, 0, 32768, 32768, 32, 32),
        std::integral_constant<std::size_t, 0>{}));

    const auto& diag = sink.last_diagnostics();
    EXPECT_EQ(diag.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diag.pulses_processed, 32u);
    EXPECT_EQ(diag.tiles_processed, 4u);
    EXPECT_EQ(diag.bytes_h2d, 32768u);
    EXPECT_EQ(diag.bytes_d2h, 32768u);
    EXPECT_EQ(diag.kernel_dispatches, 32u);
    EXPECT_EQ(diag.fanin_wait_ms, 32u);
    EXPECT_EQ(diag.e2e_latency_ms, 32u);
    EXPECT_EQ(diag.duplicate_tile_count, 28u);
    EXPECT_EQ(diag.missing_tile_count, 0u);
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

} // namespace
