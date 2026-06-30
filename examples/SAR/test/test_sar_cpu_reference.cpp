// SPDX-License-Identifier: MIT

/**
 * @file test_sar_cpu_reference.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar_reference_parity_fixture.hpp"
#include "sar/SarCpuReference.hpp"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/accel/types/AccelTypes.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#include "graph/CapabilityBus.hpp"
#include "sar/SarBackprojectionTransformAccelNode.hpp"
#endif

namespace {

namespace reference_parity = sar::test::reference_parity;

} // namespace

TEST(SarCpuReferenceTest, PointTargetBackprojectionFocusesAtKnownPixel) {
    const auto geometry = reference_parity::TinyPointTargetGeometry();
    const sar::reference::PointTarget target{
        .x_m = 0.0,
        .y_m = 0.0,
        .reflectivity = 1.0,
    };

    const auto phase_history = sar::reference::GeneratePointTargetPhaseHistory(geometry, target);
    const auto image = sar::reference::BackprojectNearestRange(geometry, phase_history);
    const auto peak = sar::reference::FindPeak(image);

    EXPECT_EQ(peak.x, reference_parity::kTinyPointPeakX);
    EXPECT_EQ(peak.y, reference_parity::kTinyPointPeakY);
    EXPECT_NEAR(peak.value, reference_parity::kTinyPointPeakValue, reference_parity::kTinyPointPeakValueTolerance);

    const auto center_index =
        static_cast<std::size_t>(peak.y) * image.width + peak.x;
    ASSERT_LT(center_index, image.pixels.size());
    EXPECT_EQ(image.pixels[center_index], peak.value);

    EXPECT_EQ(sar::reference::QuantizedImageHash(image), reference_parity::kTinyPointImageHash);
}

TEST(SarCpuReferenceTest, ImageComparisonReportsParityMetrics) {
    const auto geometry = reference_parity::TinyPointTargetGeometry();
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

TEST(SarCpuReferenceTest, MatchedFilterKnownVectorFindsDelayedEcho) {
    sar::reference::ChirpReferenceConfig cfg{};
    cfg.sample_count = reference_parity::kMatchedFilterVectorLength;
    cfg.sample_rate_hz = 16.0e6;
    cfg.bandwidth_hz = 4.0e6;
    cfg.chirp_duration_s = 1.0e-6;
    cfg.range_origin_m = 0.0;
    cfg.range_spacing_m = 0.25;

    const auto chirp = sar::reference::GenerateLinearFmChirp(cfg);
    const auto echo = sar::reference::GenerateDelayedEcho(chirp, 3u, 0.75);
    const auto compressed = sar::reference::MatchedFilterRangeCompress(echo, chirp);
    const auto image = sar::reference::MagnitudeImage(reference_parity::kMatchedFilterVectorLength, 1u, compressed);
    const auto metrics = sar::reference::MeasureImageQuality(image, reference_parity::kMatchedFilterPeakBin, 0u);

    EXPECT_EQ(metrics.peak.x, reference_parity::kMatchedFilterPeakBin);
    EXPECT_EQ(metrics.peak.y, 0u);
    EXPECT_NEAR(metrics.peak.value, static_cast<float>(reference_parity::kMatchedFilterPeakValue), reference_parity::kTinyPointPeakValueTolerance);
    EXPECT_DOUBLE_EQ(metrics.peak_location_error_pixels, reference_parity::kImagePeakLocationErrorTolerancePixels);
    EXPECT_GE(metrics.dynamic_range_db, reference_parity::kImageDynamicRangeMinDb);
    EXPECT_LT(metrics.peak_sidelobe_ratio_db, 0.0);
    EXPECT_NE(metrics.image_hash, 0u);
}

TEST(SarCpuReferenceTest, ImageQualityMetricsTrackOffGridPointTarget) {
    const auto geometry = reference_parity::TinyPointTargetGeometry();
    const sar::reference::PointTarget target{
        .x_m = 0.25,
        .y_m = 0.0,
        .reflectivity = 1.0,
    };

    const auto phase_history = sar::reference::GeneratePointTargetPhaseHistory(geometry, target);
    const auto image = sar::reference::BackprojectNearestRange(geometry, phase_history);
    const auto metrics = sar::reference::MeasureImageQuality(image, 4u, 4u);

    EXPECT_LE(metrics.peak_location_error_pixels, 1.0);
    EXPECT_GT(metrics.peak.value, 0.25f);
    EXPECT_GE(metrics.impulse_response_width_pixels, 1.0);
    EXPECT_NE(metrics.image_hash, 0u);
}

TEST(SarCpuReferenceTest, TwoPointTargetFixtureHasStableRelativePeaks) {
    const auto geometry = reference_parity::TinyPointTargetGeometry();
    const std::vector<sar::reference::PointTarget> targets{
        sar::reference::PointTarget{.x_m = 0.0, .y_m = 0.0, .reflectivity = 1.0},
        sar::reference::PointTarget{.x_m = 1.0, .y_m = 0.0, .reflectivity = 0.5},
    };

    std::vector<std::complex<double>> phase_history(
        static_cast<std::size_t>(geometry.pulse_count) * geometry.range_bin_count,
        {0.0, 0.0});
    for (const auto& target : targets) {
        const auto target_history =
            sar::reference::GeneratePointTargetPhaseHistory(geometry, target);
        ASSERT_EQ(target_history.size(), phase_history.size());
        for (std::size_t i = 0; i < phase_history.size(); ++i) {
            phase_history[i] += target_history[i];
        }
    }

    const auto image = sar::reference::BackprojectNearestRange(geometry, phase_history);
    const auto metrics = sar::reference::MeasureImageQuality(image, 4u, 4u);

    EXPECT_EQ(metrics.peak.x, 4u);
    EXPECT_EQ(metrics.peak.y, 4u);
    EXPECT_GT(metrics.peak.value, 1.0f);
    EXPECT_LT(metrics.peak_sidelobe_ratio_db, 0.0);
    EXPECT_NE(metrics.image_hash, 0u);
}

TEST(SarCpuReferenceTest, BackprojectionAdapterReferenceMatchesNativeMetalWhenAvailable) {
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    namespace accel = graph::gpu::accel;
    namespace metal_cap = graph::gpu::metal::capabilities;
    namespace metal_nodes = graph::gpu::metal::nodes;

    if (!metal_cap::NativeMetalRuntimeAvailable()) {
#if GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME
        FAIL() << "GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON but native Metal is unavailable: "
               << metal_cap::NativeMetalRuntimeDiagnostics();
#else
        GTEST_SKIP() << "Native Metal unavailable: "
                     << metal_cap::NativeMetalRuntimeDiagnostics();
#endif
    }

    auto runtime = metal_cap::CreateNativeMetalRuntimeContext();
    auto context = std::make_shared<metal_cap::NativeMetalContextCapability>(runtime);
    auto shared_queue = std::make_shared<metal_cap::MetalSharedQueueCapability>(context);
    auto memory_pool = std::make_shared<metal_cap::NativeMetalMemoryPoolCapability>(runtime);
    auto transfer = std::make_shared<metal_cap::NativeMetalTransferCapability>(runtime);
    auto kernel = std::make_shared<metal_cap::NativeMetalKernelCapability>(runtime);
    auto telemetry = std::make_shared<metal_cap::NativeMetalTelemetryCapability>(runtime);

    graph::CapabilityBus bus;
    bus.Register<metal_cap::IMetalContextCapability>(context);
    bus.Register<metal_cap::IMetalSharedQueueCapability>(shared_queue);
    bus.Register<metal_cap::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<metal_cap::IMetalTransferCapability>(transfer);
    bus.Register<metal_cap::IMetalKernelCapability>(kernel);
    bus.Register<metal_cap::IMetalTelemetryCapability>(telemetry);

    const std::vector<float> range_tile{
        0.25f, 1.0f, 0.5f, 2.0f, 1.25f, 0.75f, 1.5f, 0.125f,
        0.375f, 0.625f, 1.875f, 0.875f, 1.125f, 0.25f, 0.5f, 1.0f,
    };

    accel::HostPinnedBufferView host_input{};
    host_input.backend = accel::BackendKind::Metal;
    host_input.host_ptr = const_cast<float*>(range_tile.data());
    host_input.bytes = range_tile.size() * sizeof(float);
    host_input.dtype = accel::DataType::Float32;
    host_input.layout.rank = 1;
    host_input.layout.shape[0] = range_tile.size();
    host_input.layout.stride[0] = 1;
    host_input.allocator_id = 1;
    ASSERT_TRUE(accel::IsValidView(host_input));

    metal_nodes::H2DAsyncNodeMetal h2d;
    ASSERT_TRUE(h2d.BindGpuCapabilities(bus));
    auto device_input = h2d.Transfer(
        host_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_input.has_value());

    sar::reference::BackprojectionAdapterConfig ref_cfg{};
    ref_cfg.tap_count = 8;
    ref_cfg.delay_step = 0.5;
    ref_cfg.phase_tap_scale = 0.35;
    ref_cfg.phase_aperture_scale = 0.2;

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
    bp_cfg.image_width = static_cast<std::uint32_t>(range_tile.size());
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 0;
    bp_cfg.kernel_id = 99001;
    bp_cfg.tap_count = ref_cfg.tap_count;
    bp_cfg.delay_step = static_cast<float>(ref_cfg.delay_step);
    bp_cfg.phase_tap_scale = static_cast<float>(ref_cfg.phase_tap_scale);
    bp_cfg.phase_aperture_scale = static_cast<float>(ref_cfg.phase_aperture_scale);
    bp_cfg.backend = sar::SarBackendKind::NativeDevice;

    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);
    ASSERT_TRUE(bp.BindGpuCapabilities(bus));
    ASSERT_TRUE(bp.native_kernel_bound());

    sar::SarControlToken bp_input{};
    bp_input.token_id = 1u;
    bp_input.sidecar.sequence_id = 1u;
    bp_input.sidecar.tile_id = 0u;
    bp_input.sidecar.marker = sar::SarFrameMarker::Data;
    bp_input.device_view = *device_input;
    bp_input.has_device_view = true;

    auto device_output = bp.Transfer(
        bp_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(device_output.has_value());
    ASSERT_TRUE(device_output->has_device_view);

    metal_nodes::D2HAsyncNodeMetal d2h;
    ASSERT_TRUE(d2h.BindGpuCapabilities(bus));
    auto host_output = d2h.Transfer(
        device_output->device_view,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(host_output.has_value());
    ASSERT_TRUE(accel::IsValidView(*host_output));
    ASSERT_EQ(host_output->bytes, range_tile.size() * sizeof(float));

    const auto* output_ptr = static_cast<const float*>(host_output->host_ptr);
    ASSERT_NE(output_ptr, nullptr);
    std::vector<float> metal_output(output_ptr, output_ptr + range_tile.size());
    const auto cpu_output = sar::reference::RunBackprojectionAdapterReference(range_tile, ref_cfg);
    const auto error = sar::reference::CompareVectors(metal_output, cpu_output);

    EXPECT_LT(error.l_inf, 2.0e-5);
    EXPECT_LT(error.rms, 1.0e-5);
    EXPECT_LT(error.relative_l2, 1.0e-5);
#else
    GTEST_SKIP() << "Native Metal runtime support is not enabled in this build";
#endif
}
