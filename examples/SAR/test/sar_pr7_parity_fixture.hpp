#pragma once

#include "sar/SarCpuReference.hpp"

#include <cstdint>

namespace sar::test::pr7 {

inline constexpr std::uint32_t kDeterministicTotalPulses = 32u;
inline constexpr std::uint32_t kDeterministicSamplesPerPulse = 256u;
inline constexpr std::uint32_t kDeterministicTileCount = 4u;
inline constexpr std::uint32_t kDeterministicImageWidth = 16u;
inline constexpr std::uint32_t kDeterministicKernelId = 3301u;

inline constexpr std::uint32_t kMatchedFilterVectorLength = 16u;
inline constexpr std::uint32_t kMatchedFilterPeakBin = 3u;
inline constexpr double kMatchedFilterPeakValue = 9.75;
inline constexpr double kMatchedFilterReferenceLInfTolerance = 1.0e-4;
inline constexpr double kMatchedFilterReferenceRmsTolerance = 1.0e-5;

inline constexpr double kImagePeakLocationErrorTolerancePixels = 0.0;
inline constexpr double kImageDynamicRangeMinDb = 15.0;
inline constexpr double kMaterializedImageLInfTolerance = 1.0e-7;
inline constexpr double kMaterializedImageRmsTolerance = 1.0e-7;
inline constexpr double kMaterializedImageRelativeL2Tolerance = 2.0e-7;
inline constexpr double kMaterializedImageDynamicRangeDeltaToleranceDb = 1.0e-5;

inline constexpr std::uint32_t kTinyPointPeakX = 4u;
inline constexpr std::uint32_t kTinyPointPeakY = 4u;
inline constexpr float kTinyPointPeakValue = 1.0f;
inline constexpr float kTinyPointPeakValueTolerance = 1.0e-5f;
inline constexpr std::uint64_t kTinyPointImageHash = 17363341780019616407ull;

inline sar::reference::Geometry TinyPointTargetGeometry() {
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

} // namespace sar::test::pr7