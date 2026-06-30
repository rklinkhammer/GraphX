// SPDX-License-Identifier: MIT

/**
 * @file test_sar_json_pipeline.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarCpuReference.hpp"
#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"
#include "sar_reference_parity_fixture.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {

namespace reference_parity = sar::test::reference_parity;

sar::reference::Image ToImage(const std::vector<float>& pixels) {
    sar::reference::Image image{};
    image.width = static_cast<std::uint32_t>(pixels.size());
    image.height = 1u;
    image.pixels = pixels;
    return image;
}

std::shared_ptr<sar::SarMaterializedImageSinkNode> ResolveMaterializedSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SarMaterializedImageSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarMaterializedImageSinkNode>();
    }

    return nullptr;
}

void AssertEosSidecarIdentity(const sar::SarControlToken& status,
                              std::uint64_t expected_sequence_id,
                              std::uint32_t expected_stream_id,
                              std::uint32_t expected_tile_count,
                              std::uint32_t expected_backend_id) {
    EXPECT_EQ(status.sidecar.sequence_id, expected_sequence_id);
    EXPECT_EQ(status.sidecar.batch_id, expected_stream_id);
    EXPECT_EQ(status.sidecar.aperture_id, expected_sequence_id);
    EXPECT_EQ(status.sidecar.pulse_range_start, expected_sequence_id);
    EXPECT_EQ(status.sidecar.pulse_range_count, 0u);
    EXPECT_EQ(status.sidecar.stream_id, expected_stream_id);
    EXPECT_LT(status.sidecar.tile_id, expected_tile_count);
    EXPECT_EQ(status.sidecar.tile_count, expected_tile_count);
    EXPECT_EQ(status.sidecar.backend_id, expected_backend_id);
    EXPECT_EQ(status.sidecar.marker, sar::SarFrameMarker::EndOfStream);

    EXPECT_TRUE(status.has_host_view);
    EXPECT_TRUE(status.has_transfer_ticket);
    EXPECT_EQ(status.transfer_ticket.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_GT(status.transfer_ticket.execution_queue_id, 0u);

    if (status.has_kernel_ticket) {
        EXPECT_EQ(status.kernel_ticket.backend, graph::gpu::accel::BackendKind::Metal);
        EXPECT_GT(status.kernel_ticket.execution_queue_id, 0u);
    }
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_JSON_CONFIG_PATH
#define SAR_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_simulated.json"
#endif

#ifndef SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH
#define SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_materialized_image.json"
#endif

#ifndef SAR_CRSD_TINY_FULL_PIPELINE_CONFIG_JSON
#define SAR_CRSD_TINY_FULL_PIPELINE_CONFIG_JSON "examples/SAR/config/sar_crsd_tiny_fixture_full_pipeline.json"
#endif

TEST(SarJsonPipelineTest, ExecutesJsonPipelineWithSimulatedBackendPath) {
    const std::filesystem::path config_path{SAR_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, 32768u);
    EXPECT_EQ(diagnostics.bytes_d2h, 32768u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 32u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 28u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);

    const auto& status = sink->last_token();
    AssertEosSidecarIdentity(status, 32u, 0u, 4u, 0u);
}

TEST(SarJsonPipelineTest, MaterializedImagePathCapturesDeterministicSamples) {
    const std::filesystem::path config_path{SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto materialized_sink = ResolveMaterializedSink(executor->GetGraphManager());
    ASSERT_NE(materialized_sink, nullptr);
    EXPECT_GT(materialized_sink->capture_count(), 0u);
    ASSERT_TRUE(materialized_sink->has_materialized_image());

    const auto image = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    ASSERT_EQ(metadata.element_count, image.size());
    ASSERT_GE(image.size(), 4u);

    const auto reference = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id,
        metadata.tile_id,
        metadata.element_count);
    const auto error = sar::reference::CompareVectors(image, reference);

    EXPECT_LE(error.l_inf, 1.0e-7);
    EXPECT_LE(error.rms, 1.0e-7);
    EXPECT_LE(error.relative_l2, 2.0e-7);

    auto diagnostics_sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(diagnostics_sink, nullptr);
    diagnostics_sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());
    EXPECT_EQ(diagnostics_sink->last_diagnostics().sidecar.marker, sar::SarFrameMarker::EndOfStream);

    const auto& status = diagnostics_sink->last_token();
    AssertEosSidecarIdentity(status, 32u, 0u, 4u, 0u);
}

TEST(SarJsonPipelineTest, MaterializedImageParityMetricsMatchReference) {
    const std::filesystem::path config_path{SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto materialized_sink = ResolveMaterializedSink(executor->GetGraphManager());
    ASSERT_NE(materialized_sink, nullptr);
    ASSERT_TRUE(materialized_sink->has_materialized_image());

    const auto graph_pixels = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    ASSERT_EQ(metadata.element_count, graph_pixels.size());

    const auto reference_pixels = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id,
        metadata.tile_id,
        metadata.element_count);
    ASSERT_EQ(reference_pixels.size(), graph_pixels.size());

    const auto graph_image = ToImage(graph_pixels);
    const auto reference_image = ToImage(reference_pixels);
    const auto error = sar::reference::CompareImages(graph_image, reference_image);

    const auto graph_peak = sar::reference::FindPeak(graph_image);
    const auto ref_peak = sar::reference::FindPeak(reference_image);
    const auto peak_location_error_pixels =
        std::sqrt(static_cast<double>((static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) *
                                      (static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) +
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y)) *
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y))));

    const auto graph_metrics = sar::reference::MeasureImageQuality(
        graph_image,
        graph_peak.x,
        graph_peak.y);
    const auto ref_metrics = sar::reference::MeasureImageQuality(
        reference_image,
        ref_peak.x,
        ref_peak.y);
    const auto dynamic_range_delta = std::abs(graph_metrics.dynamic_range_db - ref_metrics.dynamic_range_db);

    std::ostringstream metric_report;
    metric_report << "l_inf=" << error.l_inf
                  << ", rms=" << error.rms
                  << ", relative_l2=" << error.relative_l2
                  << ", peak_location_error_pixels=" << peak_location_error_pixels
                  << ", dynamic_range_delta_db=" << dynamic_range_delta;

    EXPECT_LE(error.l_inf, reference_parity::kMaterializedImageLInfTolerance) << metric_report.str();
    EXPECT_LE(error.rms, reference_parity::kMaterializedImageRmsTolerance) << metric_report.str();
    EXPECT_LE(error.relative_l2, reference_parity::kMaterializedImageRelativeL2Tolerance) << metric_report.str();
    EXPECT_LE(peak_location_error_pixels, reference_parity::kImagePeakLocationErrorTolerancePixels) << metric_report.str();
    EXPECT_LE(dynamic_range_delta, reference_parity::kMaterializedImageDynamicRangeDeltaToleranceDb) << metric_report.str();
}

// The CPU-reference validation gate measures correctness independently from transport diagnostics.
// intentionally independent of transport-side diagnostics assertions.
TEST(SarJsonPipelineTest, CpuReferenceValidationGateIsIndependentFromTransportDiagnostics) {
    const std::filesystem::path config_path{SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto materialized_sink = ResolveMaterializedSink(executor->GetGraphManager());
    ASSERT_NE(materialized_sink, nullptr);
    ASSERT_TRUE(materialized_sink->has_materialized_image());

    const auto graph_pixels = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    ASSERT_EQ(metadata.element_count, graph_pixels.size());

    const auto reference_pixels = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id,
        metadata.tile_id,
        metadata.element_count);
    ASSERT_EQ(reference_pixels.size(), graph_pixels.size());

    const auto graph_image = ToImage(graph_pixels);
    const auto reference_image = ToImage(reference_pixels);
    const auto error = sar::reference::CompareImages(graph_image, reference_image);

    const auto graph_peak = sar::reference::FindPeak(graph_image);
    const auto ref_peak = sar::reference::FindPeak(reference_image);
    const auto peak_location_error_pixels =
        std::sqrt(static_cast<double>((static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) *
                                      (static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) +
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y)) *
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y))));

    const auto graph_metrics = sar::reference::MeasureImageQuality(
        graph_image,
        graph_peak.x,
        graph_peak.y);
    const auto ref_metrics = sar::reference::MeasureImageQuality(
        reference_image,
        ref_peak.x,
        ref_peak.y);
    const auto dynamic_range_delta = std::abs(graph_metrics.dynamic_range_db - ref_metrics.dynamic_range_db);

    std::ostringstream metric_report;
    metric_report << "l_inf=" << error.l_inf
                  << ", rms=" << error.rms
                  << ", relative_l2=" << error.relative_l2
                  << ", peak_location_error_pixels=" << peak_location_error_pixels
                  << ", dynamic_range_delta_db=" << dynamic_range_delta;

    EXPECT_LE(error.l_inf, reference_parity::kMaterializedImageLInfTolerance) << metric_report.str();
    EXPECT_LE(error.rms, reference_parity::kMaterializedImageRmsTolerance) << metric_report.str();
    EXPECT_LE(error.relative_l2, reference_parity::kMaterializedImageRelativeL2Tolerance) << metric_report.str();
    EXPECT_LE(peak_location_error_pixels, reference_parity::kImagePeakLocationErrorTolerancePixels) << metric_report.str();
    EXPECT_LE(dynamic_range_delta, reference_parity::kMaterializedImageDynamicRangeDeltaToleranceDb) << metric_report.str();
}

TEST(SarJsonPipelineTest, CrsdTinyFullPipelineConfigUsesFocusedImageStages) {
    const std::filesystem::path config_path{SAR_CRSD_TINY_FULL_PIPELINE_CONFIG_JSON};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::ifstream in(config_path);
    ASSERT_TRUE(in.is_open());
    nlohmann::json config;
    in >> config;

    ASSERT_TRUE(config.contains("edge_contract"));
    EXPECT_EQ(config.at("edge_contract").get<std::string>(), "accel-token");

    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    std::set<std::string> node_types;
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        node_types.insert(node.at("type").get<std::string>());
    }

    EXPECT_TRUE(node_types.contains("OrderedCrsdSetInputSourceNode"));
    EXPECT_TRUE(node_types.contains("CrsdApertureAssemblyAdapterNode"));
    EXPECT_TRUE(node_types.contains("CrsdFocusedImageTransformNode"));
    EXPECT_TRUE(node_types.contains("CrsdFocusedImageSinkNode"));

}
