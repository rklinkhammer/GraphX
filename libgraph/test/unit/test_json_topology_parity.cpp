// SPDX-License-Identifier: MIT

/**
 * @file test_json_topology_parity.cpp
 * @brief Test JSON Topology Parity Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphBuilder.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "test/PluginInfrastructure.hpp"
#include "test/TestGraphTopologies.hpp"

namespace {

struct TopologyCase {
    const char* file_name;
    test::TopologyType topology_type;
    size_t expected_nodes;
    size_t expected_edges;
};

struct ExecutionSummary {
    bool completion_signaled = false;
};

constexpr std::array<TopologyCase, 14> kSupportedTopologyCases = {{
    {"source_only.json", test::TopologyType::SourceOnly, 1u, 0u},
    {"minimal_graph.json", test::TopologyType::MinimalGraph, 2u, 1u},
    {"linear_sequential.json", test::TopologyType::LinearSequential, 3u, 2u},
    {"merge_simple.json", test::TopologyType::MergeSimple, 4u, 3u},
    {"split_simple.json", test::TopologyType::SplitSimple, 4u, 3u},
    {"diamond_complex.json", test::TopologyType::DiamondComplex, 6u, 6u},
    {"multi_path_sequential.json", test::TopologyType::MultiPathSequential, 5u, 4u},
    {"interior_to_merge.json", test::TopologyType::InteriorToMerge, 5u, 4u},
    {"parallel_merge_with_interior.json", test::TopologyType::ParallelMergeWithInterior, 5u, 4u},
    {"complex_network.json", test::TopologyType::ComplexNetwork, 9u, 9u},
    {"minimal_int_producer.json", test::TopologyType::MinimalIntProducer, 3u, 2u},
    {"linear_sequential_int_producer.json", test::TopologyType::LinearSequentialIntProducer, 4u, 3u},
    {"minimal_double_producer.json", test::TopologyType::MinimalDoubleProducer, 3u, 2u},
    {"linear_sequential_double_producer.json", test::TopologyType::LinearSequentialDoubleProducer, 4u, 3u},
}};

constexpr std::array<const char*, 0> kUnsupportedDynamicBuildCases = {{}};

/**
 * @brief Get checked in topology config path.
 * @param file_name Parameter for get checked in topology config path.
 */
std::filesystem::path GetCheckedInTopologyConfigPath(const char* file_name) {
    const auto test_root = std::filesystem::path(__FILE__).parent_path().parent_path();
    return test_root / "config" / "topologies" / file_name;
}

/**
 * @brief Build from json config.
 * @param config_path Parameter for build from json config.
 */
app::BuildResult BuildFromJsonConfig(const std::filesystem::path& config_path) {
    auto capability = std::make_shared<capabilities::GraphCapability>();
    capability->SetNodeProvider(test::PluginInfrastructure::GetProvider());
    capability->SetJsonConfigPath(config_path.string());

    app::GraphBuilder builder(capability);
    return builder.Build();
}

/**
 * @brief Execute graph.
 * @param graph Parameter for execute graph.
 * @param timeout Parameter for execute graph.
 */
ExecutionSummary ExecuteGraph(const std::shared_ptr<graph::GraphManager>& graph, std::chrono::seconds timeout) {
    auto executor = graph::GraphExecutorBuilder()
                        .WithGraphManager(graph)
                        .WithExecutorTimeout(timeout)
                        .Build();

    EXPECT_NE(executor, nullptr);
    if (!executor) {
        return {};
    }

    const auto init_result = executor->Init();
    EXPECT_TRUE(init_result.success) << "Init failed: " << init_result.message;

    const auto start_result = executor->Start();
    EXPECT_TRUE(start_result.success) << "Start failed: " << start_result.message;

    const auto run_result = executor->Run();
    EXPECT_TRUE(run_result.success) << "Run failed: " << run_result.message;

    const auto stop_result = executor->Stop();
    EXPECT_TRUE(stop_result.success) << "Stop failed: " << stop_result.message;

    const auto join_result = executor->Join();
    EXPECT_TRUE(join_result.success) << "Join failed: " << join_result.message;

    return ExecutionSummary{.completion_signaled = executor->IsCompletionSignaled()};
}

}  // namespace

TEST(JsonTopologyParityTest, SupportedCheckedInJsonConfigsMatchFluentTopologyParity) {
    for (const auto& test_case : kSupportedTopologyCases) {
        SCOPED_TRACE(test_case.file_name);

        const auto config_path = GetCheckedInTopologyConfigPath(test_case.file_name);
        ASSERT_TRUE(std::filesystem::exists(config_path))
            << "Missing topology config: " << config_path.string();

        const auto build_result = BuildFromJsonConfig(config_path);
        ASSERT_TRUE(build_result.success) << "GraphBuilder error: " << build_result.error_message;
        ASSERT_NE(build_result.graph, nullptr);
        auto json_graph = build_result.graph;

        EXPECT_EQ(json_graph->GetNodes().size(), test_case.expected_nodes);
        EXPECT_EQ(json_graph->GetEdges().size(), test_case.expected_edges);

        auto fluent_graph = test::TopologyBuilder::BuildTopology(test_case.topology_type);
        ASSERT_NE(fluent_graph, nullptr);

        EXPECT_EQ(fluent_graph->GetNodes().size(), test_case.expected_nodes);
        EXPECT_EQ(fluent_graph->GetEdges().size(), test_case.expected_edges);

        const auto json_summary = ExecuteGraph(json_graph, std::chrono::seconds(5));
        const auto fluent_summary = ExecuteGraph(fluent_graph, std::chrono::seconds(5));

        EXPECT_EQ(json_summary.completion_signaled, fluent_summary.completion_signaled)
            << "Completion semantics differ for " << test_case.file_name;
    }
}

TEST(JsonTopologyParityTest, UnsupportedTopologiesReportExpectedDynamicBuildLimitations) {
    for (const auto* file_name : kUnsupportedDynamicBuildCases) {
        SCOPED_TRACE(file_name);

        const auto config_path = GetCheckedInTopologyConfigPath(file_name);
        ASSERT_TRUE(std::filesystem::exists(config_path))
            << "Missing topology config: " << config_path.string();

        const auto build_result = BuildFromJsonConfig(config_path);
        ASSERT_FALSE(build_result.success)
            << "Expected dynamic build to fail for unsupported topology: " << file_name;
        const bool is_known_failure =
            (build_result.error_message.find("AddDynamicEdgeExpected rejected incompatible runtime port handles") != std::string::npos) ||
            (build_result.error_message.find("reason=PortNotFound") != std::string::npos) ||
            (build_result.error_message.find("reason=MetadataUnavailable") != std::string::npos);
        EXPECT_TRUE(is_known_failure)
            << "Unexpected failure reason: " << build_result.error_message;
    }
}
