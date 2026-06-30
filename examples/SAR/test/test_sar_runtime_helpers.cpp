// SPDX-License-Identifier: MIT

/**
 * @file test_sar_runtime_helpers.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SarMessages.hpp"
#include "gpu/accel/types/AccelValidation.hpp"

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

TEST(SarRuntimeHelpersTest, SyntheticHostFactoryReturnsTypedValidView) {
    graph::gpu::accel::HostPinnedBufferView input{};
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.bytes = 4096u;
    input.dtype = graph::gpu::accel::DataType::Float32;
    input.layout = sar::MakeAccelVectorLayout(1024u);
    input.allocator_id = 17u;

    const auto view = sar::runtime::MakeSyntheticHostView(input);

    EXPECT_TRUE(graph::gpu::accel::IsValidView(view));
    EXPECT_EQ(view.bytes, input.bytes);
    EXPECT_EQ(view.allocator_id, input.allocator_id);
}

TEST(SarRuntimeHelpersTest, SyntheticDeviceFactoryReturnsTypedUnsignaledView) {
    graph::gpu::accel::DeviceBufferView input{};
    input.backend = graph::gpu::accel::BackendKind::Metal;
    input.bytes = 4096u;
    input.dtype = graph::gpu::accel::DataType::Float32;
    input.layout = sar::MakeAccelVectorLayout(1024u);
    input.device_id = 2u;
    input.execution_queue_id = 7u;

    const auto view = sar::runtime::MakeSyntheticDeviceView(input, 3u);

    EXPECT_TRUE(graph::gpu::accel::IsValidView(view));
    EXPECT_EQ(view.bytes, input.bytes);
    EXPECT_EQ(view.device_id, input.device_id);
    EXPECT_EQ(view.ready_event, 0u);
}

TEST(SarRuntimeHelpersTest, SyntheticTicketFactoriesReturnTypedCompletions) {
    graph::gpu::accel::TransferTicket transfer{};
    transfer.backend = graph::gpu::accel::BackendKind::Metal;
    transfer.transfer_id = 3u;
    transfer.execution_queue_id = 7u;
    transfer = sar::runtime::MakeSyntheticTransferTicket(transfer);

    graph::gpu::accel::KernelTicket kernel{};
    kernel.backend = graph::gpu::accel::BackendKind::Metal;
    kernel.kernel_id = 4u;
    kernel.arg_count = 1u;
    kernel.execution_queue_id = 7u;
    kernel = sar::runtime::MakeSyntheticKernelTicket(kernel);

    EXPECT_GT(transfer.completion_event, 0u);
    EXPECT_GT(kernel.completion_event, transfer.completion_event);
}

TEST(SarRuntimeHelpersTest, TransportHelpersDoNotMutateSidecarIdentity) {
    sar::SarControlToken token{};
    token.sidecar.sequence_id = 77u;
    token.sidecar.batch_id = 9u;
    token.sidecar.tile_id = 3u;

    const auto original_sidecar = token.sidecar;
    token.host_view = sar::runtime::MakeSyntheticHostView(token.host_view);
    token.device_view.bytes = 2048u;
    token.device_view = sar::runtime::MakeSyntheticDeviceView(token.device_view, 5u);
    token.transfer_ticket =
        sar::runtime::MakeSyntheticTransferTicket(token.transfer_ticket);

    EXPECT_EQ(token.sidecar.sequence_id, original_sidecar.sequence_id);
    EXPECT_EQ(token.sidecar.batch_id, original_sidecar.batch_id);
    EXPECT_EQ(token.sidecar.tile_id, original_sidecar.tile_id);
}

} // namespace
