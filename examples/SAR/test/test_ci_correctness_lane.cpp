// SPDX-License-Identifier: MIT

/**
 * @file test_ci_correctness_lane.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_LOCAL_RUNNER_PATH
#define SAR_LOCAL_RUNNER_PATH "examples/SAR/tools/sar_local_runner.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_CI_TINY_GOTCHA_FIXTURE_PATH
#define SAR_CI_TINY_GOTCHA_FIXTURE_PATH "examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json"
#endif

#ifndef SAR_IMAGE_COMPARATOR_PATH
#define SAR_IMAGE_COMPARATOR_PATH "examples/SAR/tools/sar_image_comparator.py"
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

void WriteFloat32Raster(const std::filesystem::path& path, const std::vector<float>& pixels) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write raster: " << path;
    for (const float value : pixels) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

std::shared_ptr<sar::SarMaterializedImageSinkNode> ResolveMaterializedSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper || wrapper->GetType() != "SarMaterializedImageSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarMaterializedImageSinkNode>();
    }

    return nullptr;
}

struct CiLaneResult {
    std::filesystem::path output_dir{};
    std::filesystem::path graphx_contract_path{};
    std::filesystem::path reference_contract_path{};
    std::filesystem::path report_path{};
    nlohmann::json report{};
};

CiLaneResult RunCiSafeCorrectnessLane(const std::filesystem::path& output_dir) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto tiny_fixture_path = std::filesystem::path{SAR_CI_TINY_GOTCHA_FIXTURE_PATH};
    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    const auto comparator_path = std::filesystem::path{SAR_IMAGE_COMPARATOR_PATH};

    if (!std::filesystem::exists(scenario_path)) {
        throw std::invalid_argument("missing scenario file");
    }
    if (!std::filesystem::exists(tiny_fixture_path)) {
        throw std::invalid_argument("missing CI-safe tiny fixture");
    }
    if (!std::filesystem::exists(plugin_dir)) {
        throw std::invalid_argument("missing plugin directory");
    }
    if (!std::filesystem::exists(comparator_path)) {
        throw std::invalid_argument("missing comparator script");
    }

    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string scaffold_command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";
    if (std::system(scaffold_command.c_str()) != 0) {
        throw std::invalid_argument("sar_local_runner failed to scaffold CI-safe lane layout");
    }

    const auto orchestration_plan = LoadJson(output_dir / "reports" / "orchestration_plan.json");
    if (orchestration_plan.at("requires_external_data").get<bool>()) {
        throw std::invalid_argument("CI-safe lane unexpectedly requires external data");
    }
    if (orchestration_plan.at("requires_external_reference_binary").get<bool>()) {
        throw std::invalid_argument("CI-safe lane unexpectedly requires an external reference binary");
    }

    auto config = LoadJson(output_dir / "graphx" / "graphx_config.json");
    if (!config.contains("nodes")) {
        throw std::invalid_argument("graphx_config.json is missing nodes");
    }
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
    if (!executor) {
        throw std::invalid_argument("unable to construct graph executor");
    }
    if (!executor->GetGraphManager()) {
        throw std::invalid_argument("graph executor missing graph manager");
    }

    const auto run_result = executor->Execute();
    if (!run_result.success || !executor->IsCompletionSignaled()) {
        throw std::invalid_argument(run_result.message + " " + run_result.error_details);
    }

    auto materialized_sink = ResolveMaterializedSink(executor->GetGraphManager());
    if (!materialized_sink || !materialized_sink->has_materialized_image()) {
        throw std::invalid_argument("materialized image sink did not capture output");
    }

    const auto graph_pixels = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    if (graph_pixels.empty()) {
        throw std::invalid_argument("graph lane produced empty image payload");
    }
    if (metadata.element_count != graph_pixels.size()) {
        throw std::invalid_argument("graph lane metadata element_count does not match payload size");
    }

    const auto reference_pixels = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id,
        metadata.tile_id,
        metadata.element_count);
    if (reference_pixels.size() != graph_pixels.size()) {
        throw std::invalid_argument("deterministic reference size does not match graph output size");
    }

    const auto graphx_bin_path = output_dir / "graphx" / "graphx_output.bin";
    const auto reference_bin_path = output_dir / "reference" / "deterministic_reference.bin";
    WriteFloat32Raster(graphx_bin_path, graph_pixels);
    WriteFloat32Raster(reference_bin_path, reference_pixels);

    const auto graphx_contract_path = output_dir / "graphx" / "graphx_output_contract.json";
    const auto reference_contract_path = output_dir / "reference" / "deterministic_reference_contract.json";

    WriteJson(graphx_contract_path, nlohmann::json{
        {"source_tool", "graphx"},
        {"provenance_class", "graphx_runtime"},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", static_cast<std::uint64_t>(metadata.element_count)},
        {"height", 1u},
        {"byte_count", static_cast<std::uint64_t>(metadata.element_count) * 4u},
        {"raw_path", graphx_bin_path.string()},
    });

    WriteJson(reference_contract_path, nlohmann::json{
        {"source_tool", "deterministic-reference"},
        {"provenance_class", "deterministic_internal_reference"},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", static_cast<std::uint64_t>(metadata.element_count)},
        {"height", 1u},
        {"byte_count", static_cast<std::uint64_t>(metadata.element_count) * 4u},
        {"raw_path", reference_bin_path.string()},
    });

    const auto report_path = output_dir / "reports" / "ci_safe_correctness_report.json";
    const std::string compare_command =
        "PYTHONDONTWRITEBYTECODE=1 python3 -B " + Quote(comparator_path) +
        " compare" +
        " --strict" +
        " --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";

    if (std::system(compare_command.c_str()) != 0) {
        throw std::invalid_argument("strict comparator returned non-zero for the CI-safe lane");
    }
    if (!std::filesystem::exists(report_path)) {
        throw std::invalid_argument("CI-safe correctness report was not emitted");
    }

    CiLaneResult result{};
    result.output_dir = output_dir;
    result.graphx_contract_path = graphx_contract_path;
    result.reference_contract_path = reference_contract_path;
    result.report_path = report_path;
    result.report = LoadJson(report_path);
    return result;
}

} // namespace

TEST(CiCorrectnessLaneTest, EndToEndLaneEmitsArtifactsAndStrictPassReport) {
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_ci_correctness_lane";
    const auto result = RunCiSafeCorrectnessLane(output_dir);

    ASSERT_TRUE(std::filesystem::exists(result.graphx_contract_path));
    ASSERT_TRUE(std::filesystem::exists(result.reference_contract_path));
    ASSERT_TRUE(std::filesystem::exists(result.report_path));

    const auto& report = result.report;
    EXPECT_EQ(report.at("schema_version").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    EXPECT_EQ(report.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report.at("passed").get<bool>());
    EXPECT_EQ(report.at("thresholds").at("mode").get<std::string>(), "strict");
    EXPECT_TRUE(report.at("reasons").empty());

    const auto& metrics = report.at("metrics");
    EXPECT_DOUBLE_EQ(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("rms").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("relative_l2").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("max_abs_error").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("peak_coordinate_delta_pixels").get<double>(), 0.0);
}

TEST(CiCorrectnessLaneTest, LaneIsDeterministicAcrossRepeatedRunsAndReportPathIsStable) {
    const auto output_dir_1 = std::filesystem::temp_directory_path() / "graphx_ci_correctness_lane_run1";
    const auto output_dir_2 = std::filesystem::temp_directory_path() / "graphx_ci_correctness_lane_run2";

    const auto first = RunCiSafeCorrectnessLane(output_dir_1);
    const auto second = RunCiSafeCorrectnessLane(output_dir_2);

    EXPECT_EQ(first.report_path.filename(), second.report_path.filename());
    EXPECT_EQ(first.report.at("schema_version"), second.report.at("schema_version"));
    EXPECT_EQ(first.report.at("scenario_id"), second.report.at("scenario_id"));
    EXPECT_EQ(first.report.at("verdict"), second.report.at("verdict"));
    EXPECT_EQ(first.report.at("passed"), second.report.at("passed"));
    EXPECT_EQ(first.report.at("metrics").dump(), second.report.at("metrics").dump());
    EXPECT_EQ(first.report.at("thresholds").dump(), second.report.at("thresholds").dump());
    EXPECT_EQ(first.report.at("checks").dump(), second.report.at("checks").dump());
    EXPECT_EQ(first.report.at("reasons").dump(), second.report.at("reasons").dump());

    const auto first_graphx = LoadJson(first.graphx_contract_path);
    const auto second_graphx = LoadJson(second.graphx_contract_path);
    EXPECT_EQ(first_graphx.at("width"), second_graphx.at("width"));
    EXPECT_EQ(first_graphx.at("height"), second_graphx.at("height"));
    EXPECT_EQ(first_graphx.at("byte_count"), second_graphx.at("byte_count"));
    EXPECT_EQ(first_graphx.at("source_tool"), second_graphx.at("source_tool"));
    EXPECT_EQ(first_graphx.at("provenance_class"), second_graphx.at("provenance_class"));
}