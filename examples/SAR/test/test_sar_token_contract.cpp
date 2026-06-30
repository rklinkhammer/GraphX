// SPDX-License-Identifier: MIT

/**
 * @file test_sar_token_contract.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "dsp/DspIqH2DNode.hpp"
#include "dsp/fhss/CPSMBranchMetricNode.hpp"
#include "gpu/cuda/nodes/H2DAsyncNode.hpp"
#include "graph/AccelTokenContracts.hpp"
#include "sar/GotchaReplaySourceNode.hpp"
#include "sar/D2HAsyncAccelNode.hpp"
#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/OrderedCrsdSetInputSourceNode.hpp"
#include "sar/SarBackprojectionTransformAccelNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarMessages.hpp"

#include <type_traits>

namespace {

TEST(SarTokenContractTest, SarPacketCarriesCanonicalIdentityFields) {
    sar::SarPacket sidecar{};
    sidecar.sequence_id = 101;
    sidecar.batch_id = 17;
    sidecar.aperture_id = 9;
    sidecar.pulse_range_start = 400;
    sidecar.pulse_range_count = 32;
    sidecar.stream_id = 3;
    sidecar.tile_id = 2;
    sidecar.tile_count = 8;
    sidecar.backend_id = 1;
    sidecar.backend = sar::SarBackendKind::NativeDevice;
    sidecar.marker = sar::SarFrameMarker::Watermark;
    sidecar.synthetic = false;
    sidecar.payload_byte_count = 4096;
    sidecar.h2d_queue_id = 11;
    sidecar.kernel_queue_id = 12;
    sidecar.d2h_queue_id = 13;

    EXPECT_EQ(sidecar.sequence_id, 101u);
    EXPECT_EQ(sidecar.batch_id, 17u);
    EXPECT_EQ(sidecar.aperture_id, 9u);
    EXPECT_EQ(sidecar.pulse_range_start, 400u);
    EXPECT_EQ(sidecar.pulse_range_count, 32u);
    EXPECT_EQ(sidecar.stream_id, 3u);
    EXPECT_EQ(sidecar.tile_id, 2u);
    EXPECT_EQ(sidecar.tile_count, 8u);
    EXPECT_EQ(sidecar.backend_id, 1u);
    EXPECT_EQ(sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(sidecar.marker, sar::SarFrameMarker::Watermark);
    EXPECT_FALSE(sidecar.synthetic);
    EXPECT_EQ(sidecar.payload_byte_count, 4096u);
    EXPECT_EQ(sidecar.h2d_queue_id, 11u);
    EXPECT_EQ(sidecar.kernel_queue_id, 12u);
    EXPECT_EQ(sidecar.d2h_queue_id, 13u);
}

TEST(SarTokenContractTest, CanonicalTokenCarriesSidecarAndAccelViews) {
    sar::SarControlToken token{};
    token.token_id = 55;
    token.sidecar.sequence_id = 8;
    token.sidecar.marker = sar::SarFrameMarker::Data;
    token.has_host_view = true;
    token.host_view.bytes = 512;

    EXPECT_EQ(token.token_id, 55u);
    EXPECT_EQ(token.sidecar.sequence_id, 8u);
    EXPECT_EQ(token.sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_TRUE(token.has_host_view);
    EXPECT_EQ(token.host_view.bytes, 512u);
    EXPECT_FALSE(token.has_device_view);
    EXPECT_FALSE(token.has_transfer_ticket);
    EXPECT_FALSE(token.has_kernel_ticket);
    EXPECT_FALSE(token.has_lease);
}

TEST(SarTokenContractTest, SarAccelTokenUsesSarPacket) {
    EXPECT_TRUE((graph::AccelControlTokenFor<sar::SarControlToken,
                                             sar::SarPacket>));
}

TEST(SarTokenContractTest, CompileTimePortContractsAreTokenHardenedAcrossDomains) {
    static_assert(graph::InputPortUsesAccelControlTokenFor<
                  dsp::DspIqH2DNode<256>,
                  0,
                  graph::message::Message>);
    static_assert(graph::OutputPortUsesAccelControlTokenFor<
                  dsp::DspIqH2DNode<256>,
                  0,
                  graph::message::Message>);

    static_assert(graph::InputPortUsesAccelControlToken<
                  dsp::fhss::CPSMBranchMetricNode,
                  0>);
    static_assert(graph::OutputPortUsesAccelControlToken<
                  dsp::fhss::CPSMBranchMetricNode,
                  0>);

    static_assert(graph::InputPortUsesAccelControlTokenFor<
                  sar::H2DAsyncAccelNode,
                  0,
                  sar::SarPacket>);
    static_assert(graph::OutputPortUsesAccelControlTokenFor<
                  sar::H2DAsyncAccelNode,
                  0,
                  sar::SarPacket>);

    static_assert(graph::OutputPortUsesAccelControlTokenFor<
                  sar::OrderedCrsdSetInputSourceNode,
                  0,
                  sar::SarPacket>);
    static_assert(graph::OutputPortUsesAccelControlTokenFor<
                  sar::GotchaReplaySourceNode,
                  0,
                  sar::SarPacket>);
    static_assert(graph::InputPortUsesAccelControlTokenFor<
                  sar::SarDiagnosticsSinkNode,
                  0,
                  sar::SarPacket>);

    static_assert(graph::InputPortTypeIs<
                  graph::gpu::cuda::nodes::H2DAsyncNode,
                  0,
                  graph::gpu::accel::HostPinnedBufferView>);
    static_assert(graph::OutputPortTypeIs<
                  graph::gpu::cuda::nodes::H2DAsyncNode,
                  0,
                  graph::gpu::accel::DeviceBufferView>);

    SUCCEED();
}

TEST(SarTokenContractTest, CanonicalSarGpuStagesUseExplicitAccelTypeNames) {
    EXPECT_EQ(sar::H2DAsyncAccelNode::TypeName(), "H2DAsyncAccelNode");
    EXPECT_EQ(sar::D2HAsyncAccelNode::TypeName(), "D2HAsyncAccelNode");
    EXPECT_EQ(sar::SarBackprojectionTransformAccelNode::TypeName(),
              "SarBackprojectionTransformAccelNode");
}

} // namespace
