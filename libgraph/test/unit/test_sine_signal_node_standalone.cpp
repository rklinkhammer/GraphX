// SPDX-License-Identifier: MIT

/**
 * @file test_sine_signal_node_standalone.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include "dsp/IqPacket.hpp"
#include "dsp/SineSignalNode.hpp"
#include "graph/Message.hpp"

namespace {

constexpr size_t kPacketSize = 256;
constexpr double kDefaultFrequencyHz = 1000.0;
constexpr double kDefaultSampleRateHz = 48000.0;
constexpr double kPi = 3.14159265358979323846;

TEST(SineSignalNodeStandaloneTest, ProducesExpectedDefaultWaveformPacket) {
    dsp::SineSignalNode<kPacketSize> node;

    const auto message = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(message.has_value());

    const auto& packet = message->template get<dsp::IqPacket<float, kPacketSize>>();

    EXPECT_EQ(packet.packet_number, 0u);
    EXPECT_DOUBLE_EQ(packet.sample_rate_hz, kDefaultSampleRateHz);
    EXPECT_EQ(packet.GetNumSamples(), kPacketSize);
    EXPECT_TRUE(packet.IsValid());

    const double phase_step = 2.0 * kPi * kDefaultFrequencyHz / kDefaultSampleRateHz;

    // Validate the beginning of the generated sequence against the analytic sine/cosine model.
    for (size_t i = 0; i < 8; ++i) {
        const auto& sample = packet.samples[i];
        const double expected_i = std::sin(phase_step * static_cast<double>(i));
        const double expected_q = std::cos(phase_step * static_cast<double>(i));

        EXPECT_NEAR(sample.real(), expected_i, 1e-4) << "I mismatch at sample " << i;
        EXPECT_NEAR(sample.imag(), expected_q, 1e-4) << "Q mismatch at sample " << i;
    }

    // With default amplitude=1.0, each complex sample should be near the unit circle.
    for (size_t i = 0; i < kPacketSize; ++i) {
        const auto& sample = packet.samples[i];
        const double magnitude = std::hypot(static_cast<double>(sample.real()),
                                            static_cast<double>(sample.imag()));
        EXPECT_NEAR(magnitude, 1.0, 1e-4) << "Magnitude drift at sample " << i;
    }
}

}  // namespace
