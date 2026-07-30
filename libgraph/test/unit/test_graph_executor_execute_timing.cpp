// SPDX-License-Identifier: MIT

/**
 * @file test_graph_executor_execute_timing.cpp
 * @brief GraphExecutor Execute timing contract tests.
 */
#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/ExecutionState.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "test/TestGraphTopologies.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

std::filesystem::path DspSpectrumConfigPath() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
           "libdsp/config/dsp_sine_fft_spectrum_256.json";
}

std::filesystem::path PluginDirectory() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

std::shared_ptr<graph::GraphExecutor> BuildCpuDspExecutor() {
    return graph::GraphExecutorBuilder()
        .WithJsonConfig(DspSpectrumConfigPath().string())
        .WithPluginDirectory(PluginDirectory().string())
        .WithExecutorTimeout(std::chrono::seconds(5))
        .Build();
}

std::unique_ptr<graph::GraphExecutor> BuildManualLifecycleExecutor() {
    auto graph_manager =
        test::TopologyBuilder::BuildTopology(test::TopologyType::MinimalGraph);
    auto capability = std::make_shared<capabilities::GraphCapability>();
    capability->SetGraphManager(graph_manager);
    return std::make_unique<graph::GraphExecutor>(nullptr, capability);
}

void ExpectPhaseNotGreaterThanTotal(const graph::ExecutionResult& result) {
    EXPECT_LE(result.init_elapsed_time_ms, result.elapsed_time_ms);
    EXPECT_LE(result.start_elapsed_time_ms, result.elapsed_time_ms);
    EXPECT_LE(result.run_elapsed_time_ms, result.elapsed_time_ms);
    EXPECT_LE(result.stop_elapsed_time_ms, result.elapsed_time_ms);
    EXPECT_LE(result.join_elapsed_time_ms, result.elapsed_time_ms);
}

}  // namespace

TEST(GraphExecutorTimingContractTest, ExecuteReturnsTotalAndLifecyclePhaseTimings) {
    ASSERT_TRUE(std::filesystem::exists(DspSpectrumConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectory()));

    auto executor = BuildCpuDspExecutor();
    ASSERT_NE(executor, nullptr);

    const auto result = executor->Execute();

    ASSERT_TRUE(result.success) << result.message << " " << result.error_details;
    EXPECT_EQ(result.message, "Execute completed successfully");
    EXPECT_EQ(result.current_state, graph::ExecutionState::STOPPED);
    EXPECT_TRUE(executor->IsCompletionSignaled());
    ExpectPhaseNotGreaterThanTotal(result);
}

TEST(GraphExecutorTimingContractTest, StandaloneLifecycleResultsRemainStandalone) {
    auto executor = BuildManualLifecycleExecutor();
    ASSERT_NE(executor, nullptr);

    const auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message;

    const auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message;
    EXPECT_EQ(start_result.current_state, graph::ExecutionState::RUNNING);
    EXPECT_EQ(start_result.init_elapsed_time_ms, 0u);
    EXPECT_EQ(start_result.start_elapsed_time_ms, 0u);
    EXPECT_EQ(start_result.run_elapsed_time_ms, 0u);
    EXPECT_EQ(start_result.stop_elapsed_time_ms, 0u);
    EXPECT_EQ(start_result.join_elapsed_time_ms, 0u);

    const auto stop_result = executor->Stop();
    ASSERT_TRUE(stop_result.success) << stop_result.message;
    EXPECT_EQ(stop_result.current_state, graph::ExecutionState::STOPPING);
    EXPECT_EQ(stop_result.init_elapsed_time_ms, 0u);
    EXPECT_EQ(stop_result.start_elapsed_time_ms, 0u);
    EXPECT_EQ(stop_result.run_elapsed_time_ms, 0u);
    EXPECT_EQ(stop_result.stop_elapsed_time_ms, 0u);
    EXPECT_EQ(stop_result.join_elapsed_time_ms, 0u);

    const auto join_result = executor->Join();
    ASSERT_TRUE(join_result.success) << join_result.message;
    EXPECT_EQ(join_result.init_elapsed_time_ms, 0u);
    EXPECT_EQ(join_result.start_elapsed_time_ms, 0u);
    EXPECT_EQ(join_result.run_elapsed_time_ms, 0u);
    EXPECT_EQ(join_result.stop_elapsed_time_ms, 0u);
    EXPECT_EQ(join_result.join_elapsed_time_ms, 0u);
}

TEST(GraphExecutorTimingContractTest, ExecuteTimingBaselineFieldsRemainDeterministic) {
    ASSERT_TRUE(std::filesystem::exists(DspSpectrumConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectory()));

    auto executor = BuildCpuDspExecutor();
    ASSERT_NE(executor, nullptr);

    const auto result = executor->Execute();
    ASSERT_TRUE(result.success) << result.message << " " << result.error_details;

    EXPECT_EQ(result.current_state, graph::ExecutionState::STOPPED);
    EXPECT_GE(result.elapsed_time_ms, 0u);
    EXPECT_GE(result.init_elapsed_time_ms, 0u);
    EXPECT_GE(result.start_elapsed_time_ms, 0u);
    EXPECT_GE(result.run_elapsed_time_ms, 0u);
    EXPECT_GE(result.stop_elapsed_time_ms, 0u);
    EXPECT_GE(result.join_elapsed_time_ms, 0u);
    EXPECT_GT(result.run_elapsed_time_ms, 0u);

    ExpectPhaseNotGreaterThanTotal(result);
}
