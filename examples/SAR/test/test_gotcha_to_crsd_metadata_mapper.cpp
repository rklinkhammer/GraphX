#include <gtest/gtest.h>

#include "sar/io/GotchaToCrsdMetadataMapper.hpp"

#include <nlohmann/json.hpp>

TEST(GotchaToCrsdMetadataMapperTest, MapsFrequencyAxisCarrierBandwidthAndSampleCount) {
    const nlohmann::json sidecar{
        {"K", 4},
        {"deltaF", 2.0e6},
        {"minF", 9.590e9},
        {"AntX", 10.0},
        {"AntY", 20.0},
        {"AntZ", 30.0},
        {"R0", 1250.5},
    };

    const auto mapped = graphx::sar::GotchaToCrsdMetadataMapper::Map(sidecar);

    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->sample_count, 4u);
    ASSERT_EQ(mapped->frequency_axis_hz.size(), 4u);
    EXPECT_DOUBLE_EQ(mapped->frequency_axis_hz[0], 9.590e9);
    EXPECT_DOUBLE_EQ(mapped->frequency_axis_hz[1], 9.592e9);
    EXPECT_DOUBLE_EQ(mapped->frequency_axis_hz[2], 9.594e9);
    EXPECT_DOUBLE_EQ(mapped->frequency_axis_hz[3], 9.596e9);
    EXPECT_DOUBLE_EQ(mapped->carrier_hz, 9.593e9);
    EXPECT_DOUBLE_EQ(mapped->bandwidth_hz, 8.0e6);
}

TEST(GotchaToCrsdMetadataMapperTest, MapsAntennaPhaseCenterReferenceRangeAndLocalFrame) {
    const nlohmann::json sidecar{
        {"K", 2},
        {"deltaF", 1.0e6},
        {"minF", 9.599e9},
        {"AntX", -1.25},
        {"AntY", 2.5},
        {"AntZ", 345.75},
        {"R0", 998.125},
    };

    const auto mapped = graphx::sar::GotchaToCrsdMetadataMapper::Map(sidecar);

    ASSERT_TRUE(mapped.has_value());
    EXPECT_DOUBLE_EQ(mapped->antenna_xyz_m[0], -1.25);
    EXPECT_DOUBLE_EQ(mapped->antenna_xyz_m[1], 2.5);
    EXPECT_DOUBLE_EQ(mapped->antenna_xyz_m[2], 345.75);
    EXPECT_DOUBLE_EQ(mapped->reference_range_m, 998.125);
    EXPECT_EQ(mapped->coordinate_frame, graphx::sar::GotchaToCrsdMetadataMapper::kLocalCartesianFrame);

    graphx::sar::NormalizedSarProduct product{};
    graphx::sar::ChannelSignal channel{};
    graphx::sar::PulseVector pulse{};
    graphx::sar::GotchaToCrsdMetadataMapper::ApplyToProductCollection(*mapped, product.collection);
    graphx::sar::GotchaToCrsdMetadataMapper::ApplyToWaveform(*mapped, channel.waveform);
    graphx::sar::GotchaToCrsdMetadataMapper::ApplyToPulse(*mapped, pulse.parameters);

    EXPECT_EQ(product.collection.coordinate_frame, "gotcha_local_cartesian");
    EXPECT_DOUBLE_EQ(channel.waveform.carrier_hz, 9.5995e9);
    EXPECT_DOUBLE_EQ(channel.waveform.bandwidth_hz, 2.0e6);
    EXPECT_DOUBLE_EQ(channel.waveform.sample_rate_hz, 2.0e6);
    EXPECT_DOUBLE_EQ(pulse.parameters.platform.position_m[2], 345.75);
    ASSERT_TRUE(pulse.parameters.reference_range_m.has_value());
    EXPECT_DOUBLE_EQ(*pulse.parameters.reference_range_m, 998.125);
}

TEST(GotchaToCrsdMetadataMapperTest, MissingRawGotchaFieldsDoesNotProduceMapping) {
    const nlohmann::json sidecar{
        {"K", 2},
        {"deltaF", 1.0e6},
        {"minF", 9.599e9},
        {"AntX", 1.0},
        {"AntY", 2.0},
        {"AntZ", 3.0},
    };

    EXPECT_FALSE(graphx::sar::GotchaToCrsdMetadataMapper::Map(sidecar).has_value());
}
