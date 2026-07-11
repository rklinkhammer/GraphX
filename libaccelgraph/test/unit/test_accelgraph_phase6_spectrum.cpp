// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "AccelGraphTopologyTestUtils.hpp"
#include "graph/NodeProviderBootstrap.hpp"

namespace {

constexpr std::size_t kSampleCount = 256;
constexpr double kSampleRateHz = 48000.0;
constexpr std::size_t kExpectedPeakBin = 8;
constexpr double kBinWidthHz = kSampleRateHz / static_cast<double>(kSampleCount);
constexpr float kMagnitudeTolerance = 1.0e-4F;
constexpr double kFrequencyToleranceHz = 1.0e-6;

std::filesystem::path SpectrumCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6_spectrum_cpu_topology.json");
}

std::filesystem::path SpectrumMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6_spectrum_metal_topology.json");
}

std::filesystem::path SpectrumMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6_spectrum_metal_allow_fallback_topology.json");
}

}  // namespace

TEST(AccelGraphSpectrumBackendMatrixTest, CpuSpectrumCorrectnessRunsViaGraphExecutorAndPlugins) {
    const auto config_path = SpectrumCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto source = accelgraph::test::ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = accelgraph::test::ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(analysis, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    const auto output = sink->LastSpectrum();
    ASSERT_TRUE(output.has_value());
    EXPECT_GE(sink->FrameCount(), 1u);

    EXPECT_EQ(output->requested_backend, accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(output->selected_backend, accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(output->used_fallback);
    EXPECT_EQ(output->peak_bin, kExpectedPeakBin);
    EXPECT_NEAR(output->peak_frequency_hz,
                static_cast<double>(kExpectedPeakBin) * kBinWidthHz,
                kFrequencyToleranceHz);

    ASSERT_GT(output->magnitudes.size(), kExpectedPeakBin + 1);
    EXPECT_GT(output->magnitudes[kExpectedPeakBin], 0.9F);
    EXPECT_NEAR(output->magnitudes[kExpectedPeakBin - 1], 0.0F, kMagnitudeTolerance);
    EXPECT_NEAR(output->magnitudes[kExpectedPeakBin + 1], 0.0F, kMagnitudeTolerance);
}

TEST(AccelGraphSpectrumBackendMatrixTest, MetalSpectrumCorrectnessRunsViaGraphExecutorOrSkipsWithExactDiagnostic) {
    const auto config_path = SpectrumMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto source = accelgraph::test::ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = accelgraph::test::ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(analysis, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    const auto output = sink->LastSpectrum();
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->requested_backend, accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(output->selected_backend, accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(output->used_fallback);
    EXPECT_TRUE(output->fallback_diagnostic.empty());
    EXPECT_EQ(output->peak_bin, kExpectedPeakBin);
}

TEST(AccelGraphSpectrumBackendMatrixTest, CpuMetalParityChecksPeakAndSelectedBinsWithinTolerance) {
    const auto cpu_config_path = SpectrumCpuTopologyConfigPath();
    const auto metal_config_path = SpectrumMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(cpu_config_path));
    ASSERT_TRUE(std::filesystem::exists(metal_config_path));

    auto cpu_executor = accelgraph::test::BuildExecutor(cpu_config_path, std::chrono::seconds(10));
    std::shared_ptr<graph::GraphExecutor> metal_executor;
    try {
        metal_executor = accelgraph::test::BuildExecutor(metal_config_path, std::chrono::seconds(10));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
    ASSERT_NE(cpu_executor, nullptr);
    ASSERT_NE(metal_executor, nullptr);

    auto cpu_graph = cpu_executor->GetGraphManager();
    auto metal_graph = metal_executor->GetGraphManager();
    ASSERT_NE(cpu_graph, nullptr);
    ASSERT_NE(metal_graph, nullptr);

    auto cpu_source = accelgraph::test::ResolveNode<accelgraph::SineWaveSourceNode>(cpu_graph);
    auto cpu_analysis = accelgraph::test::ResolveNode<accelgraph::SpectrumAnalysisNode>(cpu_graph);
    auto cpu_sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(cpu_graph);
    auto metal_source = accelgraph::test::ResolveNode<accelgraph::SineWaveSourceNode>(metal_graph);
    auto metal_analysis = accelgraph::test::ResolveNode<accelgraph::SpectrumAnalysisNode>(metal_graph);
    auto metal_sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(metal_graph);
    ASSERT_NE(cpu_source, nullptr);
    ASSERT_NE(cpu_analysis, nullptr);
    ASSERT_NE(cpu_sink, nullptr);
    ASSERT_NE(metal_source, nullptr);
    ASSERT_NE(metal_analysis, nullptr);
    ASSERT_NE(metal_sink, nullptr);

    const auto cpu_run = cpu_executor->Execute();
    const auto metal_run = metal_executor->Execute();
    ASSERT_TRUE(cpu_run.success) << cpu_run.message << " " << cpu_run.error_details;
    ASSERT_TRUE(metal_run.success) << metal_run.message << " " << metal_run.error_details;

    const auto cpu = cpu_sink->LastSpectrum();
    const auto metal = metal_sink->LastSpectrum();
    ASSERT_TRUE(cpu.has_value());
    ASSERT_TRUE(metal.has_value());

    EXPECT_EQ(cpu->peak_bin, metal->peak_bin);
    EXPECT_NEAR(cpu->peak_frequency_hz, metal->peak_frequency_hz, kFrequencyToleranceHz);

    ASSERT_GT(cpu->magnitudes.size(), kExpectedPeakBin + 1);
    ASSERT_GT(metal->magnitudes.size(), kExpectedPeakBin + 1);
    // Representative bins around the expected tone use the same tolerance.
    EXPECT_NEAR(cpu->magnitudes[kExpectedPeakBin - 1],
                metal->magnitudes[kExpectedPeakBin - 1],
                kMagnitudeTolerance);
    EXPECT_NEAR(cpu->magnitudes[kExpectedPeakBin],
                metal->magnitudes[kExpectedPeakBin],
                kMagnitudeTolerance);
    EXPECT_NEAR(cpu->magnitudes[kExpectedPeakBin + 1],
                metal->magnitudes[kExpectedPeakBin + 1],
                kMagnitudeTolerance);
}

TEST(AccelGraphSpectrumBackendMatrixTest, StrictFallbackPolicyIsEnforcedForMetalSelection) {
    const auto strict_config_path = SpectrumMetalTopologyConfigPath();
    const auto allow_config_path = SpectrumMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(strict_config_path));
    ASSERT_TRUE(std::filesystem::exists(allow_config_path));

    bool strict_rejected = false;
    std::string strict_diagnostic;
    std::shared_ptr<graph::GraphExecutor> strict_executor;
    try {
        strict_executor = accelgraph::test::BuildExecutor(strict_config_path, std::chrono::seconds(10));
    } catch (const std::exception& ex) {
        strict_diagnostic = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(strict_diagnostic) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(strict_diagnostic)) {
            strict_rejected = true;
        } else {
            throw;
        }
    }

    if (strict_rejected) {
        auto allow_executor = accelgraph::test::BuildExecutor(allow_config_path, std::chrono::seconds(10));
        ASSERT_NE(allow_executor, nullptr);

        auto allow_graph = allow_executor->GetGraphManager();
        ASSERT_NE(allow_graph, nullptr);
        auto allow_sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(allow_graph);
        ASSERT_NE(allow_sink, nullptr);

        const auto run_result = allow_executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

        const auto output = allow_sink->LastSpectrum();
        ASSERT_TRUE(output.has_value());
        EXPECT_EQ(output->requested_backend, accelgraph::AcceleratorBackend::Metal);
        EXPECT_EQ(output->selected_backend, accelgraph::AcceleratorBackend::Cpu);
        EXPECT_TRUE(output->used_fallback);
        EXPECT_FALSE(output->fallback_diagnostic.empty());
        EXPECT_TRUE(accelgraph::test::IsExpectedMetalDiagnostic(output->fallback_diagnostic));
    } else {
        ASSERT_NE(strict_executor, nullptr);
        auto strict_graph = strict_executor->GetGraphManager();
        ASSERT_NE(strict_graph, nullptr);
        auto strict_sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(strict_graph);
        ASSERT_NE(strict_sink, nullptr);

        const auto run_result = strict_executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

        const auto output = strict_sink->LastSpectrum();
        ASSERT_TRUE(output.has_value());
        EXPECT_EQ(output->requested_backend, accelgraph::AcceleratorBackend::Metal);
        EXPECT_EQ(output->selected_backend, accelgraph::AcceleratorBackend::Metal);
        EXPECT_FALSE(output->used_fallback);
    }
}

TEST(AccelGraphSpectrumBackendMatrixTest, GraphPluginSurfaceDoesNotExposeMetalSpecificSpectrumNodeType) {
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(accelgraph::test::PluginDirectoryPath().string());
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto available = app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(bootstrap->provider);
    ASSERT_TRUE(available);

    const std::set<std::string> available_types(available->begin(), available->end());
    EXPECT_TRUE(available_types.contains("SineWaveSourceNode"));
    EXPECT_TRUE(available_types.contains("SpectrumAnalysisNode"));
    EXPECT_TRUE(available_types.contains("SpectrumSinkNode"));

    for (const auto& type_name : available_types) {
        const bool looks_metal_specific_spectrum_analysis =
            type_name.find("Metal") != std::string::npos &&
            type_name.find("SpectrumAnalysis") != std::string::npos &&
            type_name.find("Node") != std::string::npos;
        EXPECT_FALSE(looks_metal_specific_spectrum_analysis) << type_name;
    }
}
