#include <gtest/gtest.h>

#include "sar/SarCpuReference.hpp"

#include <cstdint>

namespace {

sar::reference::Geometry TinyPointTargetGeometry() {
    sar::reference::Geometry geometry{};
    geometry.pulse_count = 9;
    geometry.range_bin_count = 128;
    geometry.image_width = 9;
    geometry.image_height = 9;
    geometry.platform_x_start_m = -4.0;
    geometry.platform_x_end_m = 4.0;
    geometry.platform_y_m = -20.0;
    geometry.range_origin_m = 15.0;
    geometry.range_spacing_m = 0.25;
    geometry.scene_center_x_m = 0.0;
    geometry.scene_center_y_m = 0.0;
    geometry.pixel_spacing_m = 0.5;
    geometry.wavelength_m = 0.03;
    return geometry;
}

} // namespace

TEST(SarCpuReferenceTest, PointTargetBackprojectionFocusesAtKnownPixel) {
    const auto geometry = TinyPointTargetGeometry();
    const sar::reference::PointTarget target{
        .x_m = 0.0,
        .y_m = 0.0,
        .reflectivity = 1.0,
    };

    const auto phase_history = sar::reference::GeneratePointTargetPhaseHistory(geometry, target);
    const auto image = sar::reference::BackprojectNearestRange(geometry, phase_history);
    const auto peak = sar::reference::FindPeak(image);

    EXPECT_EQ(peak.x, 4u);
    EXPECT_EQ(peak.y, 4u);
    EXPECT_NEAR(peak.value, 1.0f, 1.0e-5f);

    const auto center_index =
        static_cast<std::size_t>(peak.y) * image.width + peak.x;
    ASSERT_LT(center_index, image.pixels.size());
    EXPECT_EQ(image.pixels[center_index], peak.value);

    constexpr std::uint64_t kExpectedHash = 17363341780019616407ull;
    EXPECT_EQ(sar::reference::QuantizedImageHash(image), kExpectedHash);
}

TEST(SarCpuReferenceTest, ImageComparisonReportsParityMetrics) {
    const auto geometry = TinyPointTargetGeometry();
    const sar::reference::PointTarget target{
        .x_m = 0.0,
        .y_m = 0.0,
        .reflectivity = 1.0,
    };

    const auto phase_history = sar::reference::GeneratePointTargetPhaseHistory(geometry, target);
    const auto expected = sar::reference::BackprojectNearestRange(geometry, phase_history);
    auto actual = expected;

    const auto exact = sar::reference::CompareImages(actual, expected);
    EXPECT_DOUBLE_EQ(exact.l_inf, 0.0);
    EXPECT_DOUBLE_EQ(exact.rms, 0.0);
    EXPECT_DOUBLE_EQ(exact.relative_l2, 0.0);

    ASSERT_GT(actual.pixels.size(), 40u);
    actual.pixels[40] += 0.125f;

    const auto perturbed = sar::reference::CompareImages(actual, expected);
    EXPECT_NEAR(perturbed.l_inf, 0.125, 1.0e-12);
    EXPECT_GT(perturbed.rms, 0.0);
    EXPECT_GT(perturbed.relative_l2, 0.0);
}
