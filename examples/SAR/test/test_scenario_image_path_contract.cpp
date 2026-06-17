// SPDX-License-Identifier: MIT

/**
 * @file test_scenario_image_path_contract.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_LOCAL_RUNNER_PATH
#define SAR_LOCAL_RUNNER_PATH "examples/SAR/tools/sar_local_runner.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_GOTCHA_REPLAY_FIXTURE_PATH
#define SAR_GOTCHA_REPLAY_FIXTURE_PATH "examples/SAR/test/fixtures/gotcha_replay_fixture.json"
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
    std::ofstream output(path, std::ios::trunc);
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

}  // namespace

TEST(ScenarioImagePathContractTest, ScenarioDrivenConfigCapturesMaterializedImage) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto fixture_path = std::filesystem::path{SAR_GOTCHA_REPLAY_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_scenario_image_path_contract";
    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const auto generated_config_path = output_dir / "graphx" / "graphx_config.json";
    ASSERT_TRUE(std::filesystem::exists(generated_config_path));

    auto config = LoadJson(generated_config_path);
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.contains("edges"));

    bool saw_materialize_node = false;
    bool saw_materialize_edge = false;
    for (auto& node : config["nodes"]) {
        if (node.at("id").get<std::string>() == "src") {
            node["node_config"]["fixture_path"] = fixture_path.string();
        }
        if (node.at("id").get<std::string>() == "materialize") {
            saw_materialize_node = true;
            EXPECT_EQ(node.at("type").get<std::string>(), "SarMaterializedImageSinkNode");
            EXPECT_TRUE(node.at("node_config").at("enabled").get<bool>());
        }
    }
    for (const auto& edge : config["edges"]) {
        if (edge.at("source_node_id").get<std::string>() == "materialize" ||
            edge.at("target_node_id").get<std::string>() == "materialize") {
            saw_materialize_edge = true;
        }
    }
    ASSERT_TRUE(saw_materialize_node);
    ASSERT_TRUE(saw_materialize_edge);

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
    EXPECT_GT(materialized_sink->capture_count(), 0u);
    ASSERT_TRUE(materialized_sink->has_materialized_image());

    const auto image = materialized_sink->last_materialized_image();
    const auto metadata = materialized_sink->last_capture_metadata();
    EXPECT_FALSE(image.empty());
    EXPECT_EQ(metadata.element_count, image.size());
}