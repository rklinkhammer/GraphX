// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "gpu/accel/types/AccelTypes.hpp"
#include "gpu/cuda/nodes/H2DAsyncNode.hpp"
#include "graph/AccelTokenContracts.hpp"

#include <cstdint>
#include <type_traits>

namespace {

using graph::gpu::accel::ControlToken;
using graph::gpu::accel::DeviceBufferView;
using graph::gpu::accel::HostPinnedBufferView;

struct SyntheticGpuSidecar {
    std::uint64_t frame_id{};
    std::uint32_t lane_id{};
};

using SyntheticGpuToken = ControlToken<SyntheticGpuSidecar>;

static_assert(graph::gpu::accel::ControlTokenType<SyntheticGpuToken>);
static_assert(graph::gpu::accel::ControlTokenFor<SyntheticGpuToken,
                                                 SyntheticGpuSidecar>);
static_assert(graph::AccelControlTokenType<SyntheticGpuToken>);
static_assert(
    graph::AccelControlTokenFor<SyntheticGpuToken, SyntheticGpuSidecar>);

static_assert(graph::InputPortTypeIs<graph::gpu::cuda::nodes::H2DAsyncNode,
                                     0,
                                     HostPinnedBufferView>);
static_assert(graph::OutputPortTypeIs<graph::gpu::cuda::nodes::H2DAsyncNode,
                                      0,
                                      DeviceBufferView>);

TEST(AccelTokenContractsTest, ControlTokenTypeTraitsRecognizeSpecialization) {
    EXPECT_TRUE((graph::gpu::accel::IsControlTokenV<SyntheticGpuToken>));
    EXPECT_FALSE((graph::gpu::accel::IsControlTokenV<SyntheticGpuSidecar>));
}

TEST(AccelTokenContractsTest, HostPtrAndReadyEventDoNotMutateSidecarIdentity) {
    SyntheticGpuToken token{};
    token.sidecar.frame_id = 77;
    token.sidecar.lane_id = 3;

    token.host_view.host_ptr = reinterpret_cast<void*>(0x1234u);
    token.device_view.ready_event = 42;

    EXPECT_EQ(token.sidecar.frame_id, 77u);
    EXPECT_EQ(token.sidecar.lane_id, 3u);

    token.host_view.host_ptr = reinterpret_cast<void*>(0x5678u);
    token.device_view.ready_event = 999;

    EXPECT_EQ(token.sidecar.frame_id, 77u);
    EXPECT_EQ(token.sidecar.lane_id, 3u);
}

TEST(AccelTokenContractsTest,
     DistinctTransportSentinelsPreserveEquivalentSidecarIdentity) {
    SyntheticGpuToken a{};
    a.sidecar.frame_id = 1001;
    a.sidecar.lane_id = 6;
    a.host_view.host_ptr = reinterpret_cast<void*>(0xAAAAu);
    a.device_view.ready_event = 1;

    SyntheticGpuToken b{};
    b.sidecar.frame_id = 1001;
    b.sidecar.lane_id = 6;
    b.host_view.host_ptr = reinterpret_cast<void*>(0xBBBBu);
    b.device_view.ready_event = 2;

    EXPECT_EQ(a.sidecar.frame_id, b.sidecar.frame_id);
    EXPECT_EQ(a.sidecar.lane_id, b.sidecar.lane_id);
    EXPECT_NE(a.host_view.host_ptr, b.host_view.host_ptr);
    EXPECT_NE(a.device_view.ready_event, b.device_view.ready_event);
}

}  // namespace
