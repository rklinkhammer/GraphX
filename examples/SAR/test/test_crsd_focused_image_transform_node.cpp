// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_focused_image_transform_node.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/CrsdFocusedImageTransformNode.hpp"
#include "sar/CrsdApertureAssemblyAdapterNode.hpp"
#include "sar/OrderedCrsdSetInputSourceNode.hpp"
#include "sar/SarCpuReference.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_CRSD_TINY_FIXTURE_BASE_DIR
#define SAR_CRSD_TINY_FIXTURE_BASE_DIR \
    "examples/SAR/test/fixtures/crsd_binary_tiny_multisegment"
#endif

constexpr double kSpeedOfLight = 299792458.0;
constexpr double kDefaultCarrierHz = 9.6e9;
constexpr double kDefaultSampleRateHz = 1.0e9;

std::vector<std::string> TinyFixturePaths() {
    const std::filesystem::path base{SAR_CRSD_TINY_FIXTURE_BASE_DIR};
    return {
        (base / "segment_000" / "product.crsd").string(),
        (base / "segment_001" / "product.crsd").string(),
        (base / "segment_002" / "product.crsd").string(),
    };
}

std::string FocusedImagePluginFilename() {
    return std::string("libcrsd_focused_image_transform_node") + kSharedLibraryExtension;
}

// ---------------------------------------------------------------------------
// FakeReader helpers for unit test injection without binary fixtures.
// ---------------------------------------------------------------------------

class FakeAdapterReader final : public graphx::sar::ICrsdReader {
public:
    explicit FakeAdapterReader(graphx::sar::CrsdReadResult result)
        : result_(std::move(result)) {}
    [[nodiscard]] graphx::sar::CrsdReadResult ReadOrderedSet(
        const graphx::sar::CrsdReadOptions&) const override {
        return result_;
    }
private:
    graphx::sar::CrsdReadResult result_{};
};

// Build a synthetic phase history result with a coherent point target at
// the specified range bin (pulse_target_range_bin) across all pulses.
graphx::sar::CrsdReadResult MakeCoherentPointTargetResult(
    std::uint64_t segments,
    std::uint64_t vectors_per_segment,
    std::uint64_t samples_per_vector,
    std::uint64_t point_target_range_bin = 4u,
    double carrier_hz = kDefaultCarrierHz,
    double sample_rate_hz = kDefaultSampleRateHz) {

    constexpr double kPi = 3.141592653589793238462643383279502884;
    const double wavelength_m = kSpeedOfLight / carrier_hz;
    const double range_spacing_m = kSpeedOfLight / (2.0 * sample_rate_hz);
    const auto clamped_range_bin = std::min(
        point_target_range_bin,
        samples_per_vector > 0u ? (samples_per_vector - 1u) : 0u);
    const double target_range_m = range_spacing_m * static_cast<double>(clamped_range_bin);
    const double platform_y_m = -target_range_m;
    const double total_vectors = static_cast<double>(segments * vectors_per_segment);

    graphx::sar::CrsdReadResult result{};
    result.success = true;
    std::uint64_t global_start = 0u;

    for (std::uint64_t seg_idx = 0u; seg_idx < segments; ++seg_idx) {
        graphx::sar::CrsdSegmentRecord seg{};
        seg.segment_index = seg_idx;
        seg.channel_id = 0u;
        seg.global_vector_start = global_start;
        seg.vector_count = vectors_per_segment;
        seg.samples_per_vector = samples_per_vector;
        seg.carrier_hz = carrier_hz;
        seg.sample_rate_hz = sample_rate_hz;

        for (std::uint64_t v = 0u; v < vectors_per_segment; ++v) {
            const std::uint64_t global_v = global_start + v;

            graphx::sar::CrsdVectorRecord vec{};
            vec.vector_index = global_v;
            vec.channel_id = 0u;

            // Spread platform uniformly along X.
            const double t = static_cast<double>(global_v) / std::max(total_vectors - 1.0, 1.0);
            const double platform_x = -50.0 + t * 100.0;  // -50 m .. +50 m
            vec.platform_position_m = {platform_x, platform_y_m, 0.0};
            vec.platform_velocity_mps = {1.0, 0.0, 0.0};
            vec.rcv_time_s = static_cast<double>(global_v) * 1.0e-4;

            // Coherent phase for a point target at the origin.
            const double dx = 0.0 - platform_x;
            const double dy = 0.0 - vec.platform_position_m[1];
            const double range_to_origin = std::sqrt(dx*dx + dy*dy);
            vec.signal.resize(samples_per_vector, {0.0f, 0.0f});

            const std::size_t bin = static_cast<std::size_t>(
                std::llround(range_to_origin / range_spacing_m));
            if (bin < samples_per_vector) {
                const double phase = -4.0 * kPi * range_to_origin / wavelength_m;
                vec.signal[bin] = {static_cast<float>(std::cos(phase)),
                                   static_cast<float>(std::sin(phase))};
            }

            seg.vectors.push_back(vec);
        }

        if (!seg.vectors.empty()) {
            seg.first_vector = seg.vectors.front();
            seg.last_vector  = seg.vectors.back();
        }
        seg.payload_hash = seg_idx + 1u;
        result.value.segments.push_back(seg);
        global_start += vectors_per_segment;
    }

    result.value.total_vector_count = global_start;
    result.value.ordered_set_payload_hash = 0xCAFEBABEu;
    return result;
}

// Build all-zero signal result (no point target energy).
graphx::sar::CrsdReadResult MakeAllZeroResult(
    std::uint64_t segments = 3u,
    std::uint64_t vectors_per_segment = 3u,
    std::uint64_t samples_per_vector = 8u) {

    graphx::sar::CrsdReadResult result{};
    result.success = true;
    std::uint64_t global_start = 0u;
    for (std::uint64_t seg_idx = 0u; seg_idx < segments; ++seg_idx) {
        graphx::sar::CrsdSegmentRecord seg{};
        seg.segment_index = seg_idx;
        seg.channel_id = 0u;
        seg.global_vector_start = global_start;
        seg.vector_count = vectors_per_segment;
        seg.samples_per_vector = samples_per_vector;
        seg.carrier_hz = kDefaultCarrierHz;
        seg.sample_rate_hz = kDefaultSampleRateHz;
        const double total_v = static_cast<double>(segments * vectors_per_segment);
        for (std::uint64_t v = 0u; v < vectors_per_segment; ++v) {
            graphx::sar::CrsdVectorRecord vec{};
            vec.vector_index = global_start + v;
            vec.channel_id = 0u;
            const double t = static_cast<double>(global_start + v) / std::max(total_v - 1.0, 1.0);
            vec.platform_position_m = {-50.0 + t * 100.0, -200.0, 0.0};
            vec.platform_velocity_mps = {1.0, 0.0, 0.0};
            vec.rcv_time_s = static_cast<double>(global_start + v) * 1.0e-4;
            vec.signal.assign(samples_per_vector, {0.0f, 0.0f});
            seg.vectors.push_back(vec);
        }
        if (!seg.vectors.empty()) {
            seg.first_vector = seg.vectors.front();
            seg.last_vector  = seg.vectors.back();
        }
        seg.payload_hash = seg_idx + 1u;
        result.value.segments.push_back(seg);
        global_start += vectors_per_segment;
    }
    result.value.total_vector_count = global_start;
    result.value.ordered_set_payload_hash = 0xDEADBEEFu;
    return result;
}

// Run adapter + transform directly from a CrsdReadResult.
std::optional<sar::FocusedImageResult> RunAdapterAndTransform(
    graphx::sar::CrsdReadResult reader_result,
    std::uint32_t image_width = 16u,
    std::uint32_t image_height = 16u) {

    auto fake_reader = std::make_shared<FakeAdapterReader>(std::move(reader_result));
    sar::CrsdApertureAssemblyAdapterNode adapter(
        sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);

    const nlohmann::json adapter_cfg{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}};
    adapter.Configure(graph::JsonView(adapter_cfg));

    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{image_width, image_height});

    // Drive data tokens through adapter.
    std::optional<sar::SarPhaseHistoryControlMessage> assembled;
    const auto num_segments = fake_reader->ReadOrderedSet({}).value.segments.size();
    for (std::uint64_t i = 0u; i < num_segments; ++i) {
        sar::SarControlToken data_tok{};
        data_tok.sidecar.marker = sar::SarFrameMarker::Data;
        data_tok.sidecar.sequence_id = i;
        adapter.Transfer(data_tok,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
    }
    sar::SarControlToken eos{};
    eos.sidecar.marker = sar::SarFrameMarker::EndOfStream;
    assembled = adapter.Transfer(eos,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    if (!assembled.has_value()) {
        return std::nullopt;
    }
    return transform.Transfer(*assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
}

} // namespace

// ===========================================================================
// PR4 Tests
// ===========================================================================

// --- Basic construction and configuration ---

TEST(CrsdFocusedImageTransformNodeTest, DefaultConstructionAndConfigureAccepted) {
    sar::CrsdFocusedImageTransformNode transform{};
    EXPECT_EQ(transform.GetLastDiagnostic(), "unconfigured");

    const nlohmann::json cfg{{"image_width", 8}, {"image_height", 8}};
    ASSERT_NO_THROW(transform.Configure(graph::JsonView(cfg)));
    EXPECT_EQ(transform.GetConfig().image_width, 8u);
    EXPECT_EQ(transform.GetConfig().image_height, 8u);
}

TEST(CrsdFocusedImageTransformNodeTest, ZeroWidthOrHeightRejectedAtConfigure) {
    sar::CrsdFocusedImageTransformNode t1, t2;
    EXPECT_THROW(t1.Configure(graph::JsonView(nlohmann::json{{"image_width", 0}, {"image_height", 8}})),
                 graph::ConfigError);
    EXPECT_THROW(t2.Configure(graph::JsonView(nlohmann::json{{"image_width", 8}, {"image_height", 0}})),
                 graph::ConfigError);
}

// --- All-zero input produces near-zero output (diagnostics-only guardrail) ---

TEST(CrsdFocusedImageTransformNodeTest, AllZeroInputProducesNearZeroImageNotFakeOutput) {
    const auto result = RunAdapterAndTransform(MakeAllZeroResult(3u, 3u, 8u));
    ASSERT_TRUE(result.has_value());
    // All-zero input must not produce a nonzero synthetic image (diagnostics-only path).
    float max_pixel = 0.0f;
    for (float p : result->pixels) {
        max_pixel = std::max(max_pixel, std::abs(p));
    }
    EXPECT_NEAR(max_pixel, 0.0f, 1.0e-6f)
        << "All-zero CRSD signal must produce a near-zero focused image";
}

// --- Coherent multi-segment peak ---

TEST(CrsdFocusedImageTransformNodeTest, CoherentMultiSegmentProducesFiniteNonzeroPeak) {
    const auto result = RunAdapterAndTransform(
        MakeCoherentPointTargetResult(3u, 4u, 64u), 16u, 16u);
    ASSERT_TRUE(result.has_value());

    const auto peak = sar::reference::FindPeak(
        sar::reference::Image{result->grid.width, result->grid.height, result->pixels});
    EXPECT_GT(peak.value, 0.0f) << "Coherent point target must produce positive peak";
    EXPECT_TRUE(std::isfinite(peak.value));
}

// --- Output shape, dtype, layout are explicit ---

TEST(CrsdFocusedImageTransformNodeTest, OutputGridAndPayloadMetadataAreExplicit) {
    const auto result = RunAdapterAndTransform(
        MakeCoherentPointTargetResult(3u, 3u, 16u), 12u, 8u);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->grid.width, 12u);
    EXPECT_EQ(result->grid.height, 8u);
    EXPECT_EQ(result->pixels.size(),
              static_cast<std::size_t>(12u) * 8u)
        << "pixel count must equal width * height (float32 row-major)";

    EXPECT_GT(result->grid.range_spacing_m, 0.0);
    EXPECT_GT(result->grid.pixel_spacing_m, 0.0);
    EXPECT_GT(result->grid.wavelength_m, 0.0);
    EXPECT_NE(result->output_hash, 0u);
    EXPECT_NE(result->input_ordered_set_hash, 0u);
    EXPECT_GT(result->total_pulses, 0u);
    EXPECT_GT(result->samples_per_pulse, 0u);
}

// --- Deterministic repeatability ---

TEST(CrsdFocusedImageTransformNodeTest, IdenticalInputsProduceDeterministicOutputHash) {
    const auto input = MakeCoherentPointTargetResult(3u, 3u, 16u);

    const auto result1 = RunAdapterAndTransform(input);
    const auto result2 = RunAdapterAndTransform(input);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result1->output_hash, result2->output_hash)
        << "Identical inputs must produce identical output hash on repeated runs";
    EXPECT_EQ(result1->pixels, result2->pixels);
}

// --- One-sample perturbation changes output ---

TEST(CrsdFocusedImageTransformNodeTest, OneSamplePerturbationChangesOutputHash) {
    auto input_baseline = MakeCoherentPointTargetResult(3u, 3u, 16u);
    auto input_perturbed = input_baseline;

    // Replace all signals in segment 1 with large constant values.
    // This guarantees the change is visible at all range bins the backprojector visits.
    for (auto& vec : input_perturbed.value.segments[1].vectors) {
        vec.signal.assign(vec.signal.size(), {10.0f, 0.0f});
    }

    const auto baseline = RunAdapterAndTransform(input_baseline);
    const auto perturbed = RunAdapterAndTransform(input_perturbed);

    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(perturbed.has_value());
    EXPECT_NE(baseline->output_hash, perturbed->output_hash)
        << "One-sample perturbation must change the focused image output hash";
}

// --- PVP/geometry perturbation changes output ---

TEST(CrsdFocusedImageTransformNodeTest, PlatformPositionPerturbationChangesOutput) {
    auto input_baseline = MakeCoherentPointTargetResult(3u, 3u, 16u);
    auto input_perturbed = input_baseline;

    // Shift platform X start for segment 0, vector 0.
    input_perturbed.value.segments[0].vectors[0].platform_position_m[0] += 5.0;

    const auto baseline = RunAdapterAndTransform(input_baseline);
    const auto perturbed = RunAdapterAndTransform(input_perturbed);

    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(perturbed.has_value());
    EXPECT_NE(baseline->output_hash, perturbed->output_hash)
        << "Platform position perturbation must change the focused image output hash";
}

// --- SarControlToken is preserved ---

TEST(CrsdFocusedImageTransformNodeTest, SarControlTokenPreservedInOutput) {
    auto reader_result = MakeCoherentPointTargetResult(3u, 3u, 16u);
    auto fake_reader = std::make_shared<FakeAdapterReader>(reader_result);
    sar::CrsdApertureAssemblyAdapterNode adapter(
        sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}}));

    for (std::uint64_t i = 0u; i < 3u; ++i) {
        sar::SarControlToken t{};
        t.sidecar.marker = sar::SarFrameMarker::Data;
        t.sidecar.sequence_id = i;
        adapter.Transfer(t, std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
    }

    sar::SarControlToken eos{};
    eos.sidecar.marker = sar::SarFrameMarker::EndOfStream;
    eos.sidecar.stream_id = 42u;
    eos.sidecar.backend_id = 7u;
    eos.sidecar.aperture_id = 99u;

    auto assembled = adapter.Transfer(eos,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});
    auto result = transform.Transfer(*assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->control.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(result->control.sidecar.stream_id, 42u);
    EXPECT_EQ(result->control.sidecar.backend_id, 7u);
    EXPECT_EQ(result->control.sidecar.aperture_id, 99u);
}

// --- Diagnostics-only forwarding failure guardrail ---
// The transform must return nullopt (not a fake image) for non-EOS data frames.

TEST(CrsdFocusedImageTransformNodeTest, NonEosDataMarkerProducesNullopt) {
    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});

    sar::SarPhaseHistoryControlMessage msg{};
    msg.frame.control_marker = sar::SarPhaseHistoryControlMarker::Data;

    auto result = transform.Transfer(msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(result.has_value())
        << "Data-marker input must not produce a focused image (diagnostics-only guardrail)";
}

// --- Payload-ignored failure guardrail ---
// Empty segments/zero vector count must return nullopt, not a fake result.

TEST(CrsdFocusedImageTransformNodeTest, EmptyPayloadFrameProducesNullopt) {
    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});

    sar::SarPhaseHistoryControlMessage msg{};
    msg.frame.control_marker = sar::SarPhaseHistoryControlMarker::EndOfStream;
    msg.frame.total_vector_count = 0u;
    msg.frame.samples_per_vector = 0u;

    auto result = transform.Transfer(msg,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(result.has_value())
        << "Empty payload must not produce a focused image";
    EXPECT_EQ(transform.GetLastDiagnostic(), "focused_image_transform:empty_payload_rejected");
}

// --- Quick-look rejection guardrail ---
// Output is NOT CRSD signal magnitude; it has data dependence on geometry (PVP/platform pos).

TEST(CrsdFocusedImageTransformNodeTest, OutputDependsOnPvpGeometryNotJustMagnitude) {
    // Two inputs: same signal magnitude, different platform trajectory.
    auto input_a = MakeCoherentPointTargetResult(3u, 3u, 16u);
    auto input_b = input_a;
    for (auto& seg : input_b.value.segments) {
        for (auto& vec : seg.vectors) {
            // Shift platform Y drastically — same signal, different geometry.
            vec.platform_position_m[1] = -500.0;
        }
    }

    const auto result_a = RunAdapterAndTransform(input_a);
    const auto result_b = RunAdapterAndTransform(input_b);

    ASSERT_TRUE(result_a.has_value());
    ASSERT_TRUE(result_b.has_value());
    EXPECT_NE(result_a->output_hash, result_b->output_hash)
        << "Different platform geometry must produce different focused image output — "
           "not a simple CRSD signal magnitude quick-look";
}

// --- One-image-per-segment rejection ---
// The transform must emit exactly one output for a full-aperture assembled frame,
// not one output per data token.

TEST(CrsdFocusedImageTransformNodeTest, OneImageProducedFromFullApertureNotPerSegment) {
    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});

    // Attempt to pass individual partial phase-history messages (each with only some vectors).
    // The transform should return nullopt for non-EOS partial inputs.
    sar::SarPhaseHistoryControlMessage partial{};
    partial.frame.control_marker = sar::SarPhaseHistoryControlMarker::Data;
    partial.frame.total_vector_count = 3u;
    partial.frame.samples_per_vector = 8u;
    sar::SarPhaseHistorySegment seg{};
    seg.vector_count = 3u;
    seg.samples_per_vector = 8u;
    partial.frame.segments.push_back(seg);

    const auto out = transform.Transfer(partial,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(out.has_value())
        << "Per-segment data-marker input must not produce focused image output";
}

// --- Split/merge partition determinism ---
// The partition scheme in the assembled frame routes correctly through transform.

TEST(CrsdFocusedImageTransformNodeTest, PartitionSchemeHashSurvivesToTransformOutput) {
    const auto input = MakeCoherentPointTargetResult(3u, 3u, 16u);
    const auto result = RunAdapterAndTransform(input);

    ASSERT_TRUE(result.has_value());
    // The input_ordered_set_hash in the result must match the reader's ordered_set_payload_hash.
    EXPECT_EQ(result->input_ordered_set_hash, input.value.ordered_set_payload_hash)
        << "Partition/split boundary input hash must be preserved in transform output";
}

// --- Binary fixture integration: source -> adapter -> transform pipeline ---

TEST(CrsdFocusedImageTransformNodeTest, TinyFixturePipelineProducesFiniteNonzeroPeak) {
    sar::OrderedCrsdSetInputSourceNode source;
    sar::CrsdApertureAssemblyAdapterNode adapter;

    const nlohmann::json source_cfg{
        {"crsd_paths", TinyFixturePaths()},
        {"stream_id", 10},
        {"backend_id", 0},
        {"backend", 0},
    };
    const nlohmann::json adapter_cfg{{"crsd_paths", TinyFixturePaths()}};

    ASSERT_NO_THROW(source.Configure(graph::JsonView(source_cfg)));
    ASSERT_NO_THROW(adapter.Configure(graph::JsonView(adapter_cfg)));

    sar::CrsdFocusedImageTransformNode transform(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});

    std::optional<sar::SarPhaseHistoryControlMessage> assembled;
    while (true) {
        auto token = source.Produce(std::integral_constant<std::size_t, 0>{});
        if (!token.has_value()) { break; }
        assembled = adapter.Transfer(*token,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
    }

    ASSERT_TRUE(assembled.has_value());
    auto result = transform.Transfer(*assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(transform.GetLastDiagnostic(), "ok:focused_image_produced");
    EXPECT_EQ(result->pixels.size(), static_cast<std::size_t>(16u * 16u));
    EXPECT_NE(result->output_hash, 0u);

    for (float p : result->pixels) {
        EXPECT_TRUE(std::isfinite(p))
            << "Every focused image pixel must be finite";
    }
}

// --- Dynamic plugin load and instantiation ---

TEST(CrsdFocusedImageTransformNodeTest, DynamicPluginLoadAndInstantiationSmoke) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(FocusedImagePluginFilename()));

    auto created = registry->CreateNodeExpected("CrsdFocusedImageTransformNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::CrsdFocusedImageTransformNode>();
    ASSERT_TRUE(node);
}
