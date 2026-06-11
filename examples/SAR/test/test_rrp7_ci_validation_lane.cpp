#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarCpuReference.hpp"
#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"
#include "sar_pr7_parity_fixture.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <nlohmann/json.hpp>

namespace {

namespace pr7 = sar::test::pr7;

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_RRP1_LOCAL_RUNNER_PATH
#define SAR_RRP1_LOCAL_RUNNER_PATH "examples/SAR/tools/rrp1_local_runner.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_RRP7_TINY_GOTCHA_FIXTURE_PATH
#define SAR_RRP7_TINY_GOTCHA_FIXTURE_PATH "examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;

    nlohmann::json value;
    input >> value;
    return value;
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write json file: " << path;
    output << value.dump(2) << '\n';
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

sar::reference::Image ToImage(const std::vector<float>& pixels) {
    sar::reference::Image image{};
    image.width = static_cast<std::uint32_t>(pixels.size());
    image.height = 1u;
    image.pixels = pixels;
    return image;
}

} // namespace

TEST(Rrp7CiValidationLaneTest, CiSafeValidationLaneReplaysScenario001WithoutExternalDownload) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto tiny_fixture_path = std::filesystem::path{SAR_RRP7_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(tiny_fixture_path));

    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_rrp7_ci_validation_lane";
    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string command =
        "python3 " + Quote(std::filesystem::path{SAR_RRP1_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const auto orchestration_plan = LoadJson(output_dir / "reports" / "orchestration_plan.json");
    EXPECT_FALSE(orchestration_plan.at("requires_external_data").get<bool>());
    EXPECT_FALSE(orchestration_plan.at("requires_external_reference_binary").get<bool>());

    auto config = LoadJson(output_dir / "graphx" / "graphx_config.json");
    ASSERT_TRUE(config.contains("nodes"));
    for (auto& node : config["nodes"]) {
        if (node.at("id").get<std::string>() == "src") {
            node["node_config"]["fixture_path"] = tiny_fixture_path.string();
        }
    }

    const auto runtime_config_path = output_dir / "graphx" / "graphx_runtime_config.json";
    WriteJson(runtime_config_path, config);

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(runtime_config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
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

    const auto error = sar::reference::CompareVectors(graph_pixels, reference_pixels);
    EXPECT_LE(error.l_inf, pr7::kMaterializedImageLInfTolerance);
    EXPECT_LE(error.rms, pr7::kMaterializedImageRmsTolerance);
    EXPECT_LE(error.relative_l2, pr7::kMaterializedImageRelativeL2Tolerance);

    const auto graph_image = ToImage(graph_pixels);
    const auto reference_image = ToImage(reference_pixels);
    const auto graph_peak = sar::reference::FindPeak(graph_image);
    const auto ref_peak = sar::reference::FindPeak(reference_image);
    const auto peak_location_error_pixels =
        std::sqrt(static_cast<double>((static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) *
                                      (static_cast<int>(graph_peak.x) - static_cast<int>(ref_peak.x)) +
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y)) *
                                      (static_cast<int>(graph_peak.y) - static_cast<int>(ref_peak.y))));
    EXPECT_LE(peak_location_error_pixels, pr7::kImagePeakLocationErrorTolerancePixels);

    const auto graph_metrics = sar::reference::MeasureImageQuality(graph_image, graph_peak.x, graph_peak.y);
    const auto ref_metrics = sar::reference::MeasureImageQuality(reference_image, ref_peak.x, ref_peak.y);
    const auto dynamic_range_delta = std::abs(graph_metrics.dynamic_range_db - ref_metrics.dynamic_range_db);
    EXPECT_LE(dynamic_range_delta, pr7::kMaterializedImageDynamicRangeDeltaToleranceDb);

    auto diagnostics_sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(diagnostics_sink, nullptr);
    diagnostics_sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());
    EXPECT_EQ(diagnostics_sink->last_diagnostics().sidecar.marker, sar::SarFrameMarker::EndOfStream);
}