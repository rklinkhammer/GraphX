#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_FROZEN_SCENARIO_REPLAY_GUIDE_PATH
#define SAR_FROZEN_SCENARIO_REPLAY_GUIDE_PATH "examples/SAR/tools/frozen_scenario_replay.md"
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

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
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
    for (const float v : pixels) {
        output.write(reinterpret_cast<const char*>(&v), sizeof(v));
    }
}

std::shared_ptr<sar::SarMaterializedImageSinkNode> ResolveMaterializedSinkPr5(
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

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open guide file: " << path;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(Rrp5FrozenScenarioReplayTest, GuideDescribesExactLocalSetupAndArtifactLayout) {
    const auto guide_path = std::filesystem::path{SAR_FROZEN_SCENARIO_REPLAY_GUIDE_PATH};
    ASSERT_TRUE(std::filesystem::exists(guide_path));

    const auto guide = ReadText(guide_path);

    EXPECT_NE(guide.find("# Frozen Scenario Replay Guide"), std::string::npos);
    EXPECT_NE(guide.find("export GOTCHA_DIR=/path/to/unpacked/GOTCHA"), std::string::npos);
    EXPECT_NE(guide.find("export GOTCHA_BACK_BIN=/path/to/gotcha-back/sarbp"), std::string::npos);
    EXPECT_NE(guide.find("python3 examples/SAR/tools/sar_local_runner.py"), std::string::npos);
    EXPECT_NE(guide.find("/tmp/graphx_sar_scenario_001/graphx/run_graphx.sh"), std::string::npos);
    EXPECT_NE(guide.find("/tmp/graphx_sar_scenario_001/reference/run_gotcha_back.sh"), std::string::npos);
    EXPECT_NE(guide.find("examples/SAR/tools/gotcha_back_adapter.py"), std::string::npos);
    EXPECT_NE(guide.find("examples/SAR/tools/sar_image_comparator.py"), std::string::npos);
    EXPECT_NE(guide.find("<output-dir>/\n  manifest/\n    scenario_001.json"), std::string::npos);
    EXPECT_NE(guide.find("reports/image_comparison_report.json"), std::string::npos);
    // PR5: CI-safe command path must also be documented
    EXPECT_NE(guide.find("CI-Safe Local Replay Command Path"), std::string::npos);
    EXPECT_NE(guide.find("graphx_output_contract.json"), std::string::npos);
    EXPECT_NE(guide.find("deterministic_reference_contract.json"), std::string::npos);
    EXPECT_NE(guide.find("ci_safe_comparison_report.json"), std::string::npos);
}

TEST(Rrp5FrozenScenarioReplayTest, GuideStatesReplayExpectationsAndScopeBoundaries) {
    const auto guide_path = std::filesystem::path{SAR_FROZEN_SCENARIO_REPLAY_GUIDE_PATH};
    ASSERT_TRUE(std::filesystem::exists(guide_path));

    const auto guide = ReadText(guide_path);

    EXPECT_NE(guide.find("scenario_001.json is immutable"), std::string::npos);
    EXPECT_NE(guide.find("matching artifacts produce `pass`; mismatched pixels produce `fail`"), std::string::npos);
    EXPECT_NE(guide.find("does not download external data"), std::string::npos);
    EXPECT_NE(guide.find("does not clone gotcha-back"), std::string::npos);
    EXPECT_NE(guide.find("does not change SAR math"), std::string::npos);
    EXPECT_NE(guide.find("does not alter accel-token architecture"), std::string::npos);
    EXPECT_NE(guide.find("does not introduce a CI dependency on GOTCHA data"), std::string::npos);
}

// PR5: Local Runner-to-Comparator Integration
// Validates the full CI-safe chain:
//   sar_local_runner → GraphX in-process → write contracts → sar_image_comparator → structured pass/fail
// Uses the tiny fixture and deterministic reference; no external data required.
TEST(Rrp5FrozenScenarioReplayTest, CiSafeLocalReplayChainProducesArtifactsAndPassesComparator) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto tiny_fixture_path = std::filesystem::path{SAR_CI_TINY_GOTCHA_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(tiny_fixture_path));

    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    const auto comparator_path = std::filesystem::path{SAR_IMAGE_COMPARATOR_PATH};
    ASSERT_TRUE(std::filesystem::exists(comparator_path));

    // Step 1: Scaffold layout with sar_local_runner
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_rrp5_ci_safe_replay";
    {
        std::error_code ec;
        std::filesystem::remove_all(output_dir, ec);
    }

    const std::string scaffold_command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";
    ASSERT_EQ(std::system(scaffold_command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_dir / "graphx" / "graphx_config.json"));

    // Step 2: Inject CI-safe tiny fixture path into scaffolded config
    auto config = LoadJson(output_dir / "graphx" / "graphx_config.json");
    ASSERT_TRUE(config.contains("nodes"));
    for (auto& node : config["nodes"]) {
        if (node.at("id").get<std::string>() == "src") {
            node["node_config"]["fixture_path"] = tiny_fixture_path.string();
        }
    }
    const auto runtime_config_path = output_dir / "graphx" / "graphx_runtime_config.json";
    WriteJson(runtime_config_path, config);

    // Step 3: Run GraphX using C++ executor (no external data download)
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

    // Step 4: Extract materialized image from sink
    auto materialized_sink = ResolveMaterializedSinkPr5(executor->GetGraphManager());
    ASSERT_NE(materialized_sink, nullptr);
    ASSERT_TRUE(materialized_sink->has_materialized_image());

    const auto graph_pixels = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    ASSERT_FALSE(graph_pixels.empty());
    ASSERT_EQ(metadata.element_count, graph_pixels.size());

    const auto element_count = metadata.element_count;

    // Step 5: Write GraphX artifact binary and contract
    const auto graphx_bin_path = output_dir / "graphx" / "graphx_output.bin";
    WriteFloat32Raster(graphx_bin_path, graph_pixels);

    const auto graphx_contract_path = output_dir / "graphx" / "graphx_output_contract.json";
    WriteJson(graphx_contract_path, nlohmann::json{
        {"source_tool", "graphx"},
        {"provenance_class", "graphx_runtime"},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", static_cast<std::uint64_t>(element_count)},
        {"height", 1u},
        {"byte_count", static_cast<std::uint64_t>(element_count) * 4u},
        {"raw_path", graphx_bin_path.string()},
    });

    // Step 6: Build deterministic reference and write reference binary and contract
    const auto reference_pixels = sar::SarMaterializedImageSinkNode::BuildDeterministicReferenceImage(
        metadata.sequence_id, metadata.tile_id, element_count);
    ASSERT_EQ(reference_pixels.size(), graph_pixels.size());

    const auto reference_bin_path = output_dir / "reference" / "deterministic_reference.bin";
    WriteFloat32Raster(reference_bin_path, reference_pixels);

    const auto reference_contract_path = output_dir / "reference" / "deterministic_reference_contract.json";
    WriteJson(reference_contract_path, nlohmann::json{
        {"source_tool", "deterministic-reference"},
        {"provenance_class", "deterministic_internal_reference"},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", static_cast<std::uint64_t>(element_count)},
        {"height", 1u},
        {"byte_count", static_cast<std::uint64_t>(element_count) * 4u},
        {"raw_path", reference_bin_path.string()},
    });

    // Step 7: Run comparator and assert structured pass/fail output
    const auto report_path = output_dir / "reports" / "ci_safe_comparison_report.json";
    const std::string compare_command =
        "PYTHONDONTWRITEBYTECODE=1 python3 -B " + Quote(comparator_path) +
        " compare" +
        " --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    const int compare_exit_code = std::system(compare_command.c_str());

    ASSERT_TRUE(std::filesystem::exists(report_path));
    const auto report = LoadJson(report_path);

    EXPECT_EQ(report.at("schema_version").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass")
        << "Comparator reasons: " << report.at("reasons").dump();
    EXPECT_TRUE(report.at("passed").get<bool>());
    EXPECT_EQ(compare_exit_code, 0);

    const auto& metrics = report.at("metrics");
    EXPECT_DOUBLE_EQ(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("rms").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("relative_l2").get<double>(), 0.0);

    // Confirm CI does not require external data (orchestration plan check)
    const auto plan = LoadJson(output_dir / "reports" / "orchestration_plan.json");
    EXPECT_FALSE(plan.at("requires_external_data").get<bool>());
    EXPECT_FALSE(plan.at("requires_external_reference_binary").get<bool>());
}
