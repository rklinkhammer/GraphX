// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "dsp/DspGpuBufferLayout.hpp"
#include "dsp/IqPacket.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "gpu/accel/types/AccelTypes.hpp"
#include "graph/Message.hpp"

namespace {

constexpr std::size_t kPacketSize = 256;

using IqPacketType = dsp::IqPacket<float, kPacketSize>;
using MagnitudePacketType = dsp::MagnitudePacket<float, kPacketSize>;
using IqTokenType = graph::gpu::accel::ControlToken<graph::message::Message>;
using MagnitudeTokenType = graph::gpu::accel::ControlToken<MagnitudePacketType>;

TEST(DspGpuBufferLayoutTest, IqPacketUsesContiguousComplexFloatPairLayout) {
    using Layout = dsp::DspGpuBufferLayout<kPacketSize>;

    static_assert(Layout::kIqComponentCount == 2);
    static_assert(Layout::kIqFloatCount == kPacketSize * 2);
    static_assert(Layout::kIqBytes == kPacketSize * 2 * sizeof(float));
    static_assert(sizeof(IqPacketType::samples[0]) == sizeof(float) * 2);

    const auto layout = dsp::IqPacketDeviceLayout<kPacketSize>();
    EXPECT_EQ(layout.rank, 2);
    EXPECT_EQ(layout.shape[0], kPacketSize);
    EXPECT_EQ(layout.shape[1], 2u);
    EXPECT_EQ(layout.stride[0], 2u);
    EXPECT_EQ(layout.stride[1], 1u);
    EXPECT_EQ(dsp::IqPacketDeviceBytes<kPacketSize>(), kPacketSize * 2 * sizeof(float));
    EXPECT_EQ(Layout::ScalarDataType(), graph::gpu::accel::DataType::Float32);
}

TEST(DspGpuBufferLayoutTest, MagnitudePacketUsesContiguousFloatBinLayout) {
    using Layout = dsp::DspGpuBufferLayout<kPacketSize>;

    static_assert(Layout::kMagnitudeBinCount == kPacketSize / 2);
    static_assert(Layout::kMagnitudeFloatCount == kPacketSize / 2);
    static_assert(Layout::kMagnitudeBytes == (kPacketSize / 2) * sizeof(float));

    const auto layout = dsp::MagnitudePacketDeviceLayout<kPacketSize>();
    EXPECT_EQ(layout.rank, 1);
    EXPECT_EQ(layout.shape[0], kPacketSize / 2);
    EXPECT_EQ(layout.stride[0], 1u);
    EXPECT_EQ(dsp::MagnitudePacketDeviceBytes<kPacketSize>(),
              (kPacketSize / 2) * sizeof(float));
    EXPECT_EQ(Layout::ScalarDataType(), graph::gpu::accel::DataType::Float32);
}

TEST(DspGpuBufferLayoutTest, IqControlTokenCarriesMessageSidecarSeparatelyFromTransport) {
    IqPacketType packet;
    packet.packet_number = 42;
    packet.sample_rate_hz = 48000.0;
    packet.samples[0] = {1.25f, -0.5f};

    IqTokenType token{};
    token.token_id = 99;
    token.sidecar = graph::message::Message(packet);
    token.device_view.device_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000));
    token.device_view.bytes = dsp::IqPacketDeviceBytes<kPacketSize>();
    token.device_view.dtype = graph::gpu::accel::DataType::Float32;
    token.device_view.layout = dsp::IqPacketDeviceLayout<kPacketSize>();
    token.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    token.has_device_view = true;
    token.transfer_ticket.transfer_id = 7;
    token.has_transfer_ticket = true;

    const auto& sidecar_packet = token.sidecar.template get<IqPacketType>();
    EXPECT_EQ(token.token_id, 99u);
    EXPECT_EQ(sidecar_packet.packet_number, 42u);
    EXPECT_DOUBLE_EQ(sidecar_packet.sample_rate_hz, 48000.0);
    EXPECT_FLOAT_EQ(sidecar_packet.samples[0].real(), 1.25f);
    EXPECT_FLOAT_EQ(sidecar_packet.samples[0].imag(), -0.5f);
    EXPECT_TRUE(token.has_device_view);
    EXPECT_EQ(token.device_view.bytes, dsp::IqPacketDeviceBytes<kPacketSize>());
}

TEST(DspGpuBufferLayoutTest, MagnitudeControlTokenCarriesPacketSidecarSeparatelyFromTransport) {
    MagnitudePacketType packet;
    packet.packet_number = 42;
    packet.sample_rate_hz = 48000.0;
    packet.peak_bin = 5;
    packet.peak_magnitude = 12.5f;
    packet.peak_frequency_hz = packet.BinToFrequency(packet.peak_bin);
    packet.magnitudes[packet.peak_bin] = packet.peak_magnitude;

    MagnitudeTokenType token{};
    token.token_id = 123;
    token.sidecar = packet;
    token.host_view.host_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x2000));
    token.host_view.bytes = dsp::MagnitudePacketDeviceBytes<kPacketSize>();
    token.host_view.dtype = graph::gpu::accel::DataType::Float32;
    token.host_view.layout = dsp::MagnitudePacketDeviceLayout<kPacketSize>();
    token.has_host_view = true;
    token.kernel_ticket.kernel_id = 11;
    token.has_kernel_ticket = true;

    EXPECT_EQ(token.token_id, 123u);
    EXPECT_EQ(token.sidecar.packet_number, 42u);
    EXPECT_EQ(token.sidecar.peak_bin, 5u);
    EXPECT_FLOAT_EQ(token.sidecar.peak_magnitude, 12.5f);
    EXPECT_FLOAT_EQ(token.sidecar.magnitudes[5], 12.5f);
    EXPECT_TRUE(token.has_host_view);
    EXPECT_EQ(token.host_view.bytes, dsp::MagnitudePacketDeviceBytes<kPacketSize>());
}

TEST(DspGpuBufferLayoutTest, TransportMetadataDoesNotDefineDspIdentity) {
    MagnitudePacketType packet;
    packet.packet_number = 314;
    packet.peak_bin = 8;
    packet.peak_magnitude = 2.0f;
    packet.magnitudes[8] = 2.0f;

    MagnitudeTokenType first{};
    first.token_id = 1;
    first.sidecar = packet;
    first.device_view.ready_event = 100;
    first.device_view.device_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x3000));

    MagnitudeTokenType second = first;
    second.token_id = 2;
    second.device_view.ready_event = 200;
    second.device_view.device_ptr = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x4000));

    EXPECT_EQ(first.sidecar.packet_number, second.sidecar.packet_number);
    EXPECT_EQ(first.sidecar.peak_bin, second.sidecar.peak_bin);
    EXPECT_FLOAT_EQ(first.sidecar.peak_magnitude, second.sidecar.peak_magnitude);
    EXPECT_NE(first.token_id, second.token_id);
    EXPECT_NE(first.device_view.ready_event, second.device_view.ready_event);
    EXPECT_NE(first.device_view.device_ptr, second.device_view.device_ptr);
}

}  // namespace
