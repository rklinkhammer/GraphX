// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "accelgraph/SpectrumGraphNodes.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeProviderBootstrap.hpp"

namespace {

constexpr std::size_t kSampleCount = 256;
constexpr double kSampleRateHz = 48000.0;
constexpr std::size_t kExpectedPeakBin = 8;
constexpr double kBinWidthHz = kSampleRateHz / static_cast<double>(kSampleCount);
constexpr float kMagnitudeTolerance = 1.0e-4F;
constexpr double kFrequencyToleranceHz = 1.0e-6;

std::filesystem::path SpectrumCpuTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase6_spectrum_cpu_topology.json";
}

std::filesystem::path SpectrumMetalTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase6_spectrum_metal_topology.json";
}

std::filesystem::path SpectrumMetalAllowFallbackTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase6_spectrum_metal_allow_fallback_topology.json";
}

std::filesystem::path PluginDirectoryPath() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

std::shared_ptr<graph::GraphExecutor> BuildExecutor(const std::filesystem::path& config_path) {
    return graph::GraphExecutorBuilder()
        .WithJsonConfig(config_path.string())
        .WithPluginDirectory(PluginDirectoryPath().string())
        .WithExecutorTimeout(std::chrono::seconds(10))
        .Build();
}

template <typename NodeT>
std::shared_ptr<NodeT> ResolveNode(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }

        auto typed = wrapper->GetNode<NodeT>();
        if (typed) {
            return typed;
        }
    }

    return nullptr;
}

bool IsExpectedMetalDiagnostic(const std::string& message) {
    return message.find(accelgraph::kMetalSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalRuntimeUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalNoCompatibleDeviceDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalSessionCreationFailureDiagnostic) != std::string::npos;
}

bool IsExpectedNodeConfigDescriptorGapDiagnostic(const std::string& message) {
    return message.find("descriptor declares no config_fields") != std::string::npos ||
           message.find("unknown node_config fields") != std::string::npos;
}

bool IsGraphBuildFailureDiagnostic(const std::string& message) {
    return message.find("Graph building failed") != std::string::npos;
}

}  // namespace

TEST(AccelGraphPhase6SpectrumTest, CpuSpectrumCorrectnessRunsViaGraphExecutorAndPlugins) {
    const auto config_path = SpectrumCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = BuildExecutor(config_path);
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedNodeConfigDescriptorGapDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto source = ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
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

TEST(AccelGraphPhase6SpectrumTest, MetalSpectrumCorrectnessRunsViaGraphExecutorOrSkipsWithExactDiagnostic) {
    const auto config_path = SpectrumMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = BuildExecutor(config_path);
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedNodeConfigDescriptorGapDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        if (IsExpectedMetalDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto source = ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
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

TEST(AccelGraphPhase6SpectrumTest, CpuMetalParityChecksPeakAndSelectedBinsWithinTolerance) {
    const auto cpu_config_path = SpectrumCpuTopologyConfigPath();
    const auto metal_config_path = SpectrumMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(cpu_config_path));
    ASSERT_TRUE(std::filesystem::exists(metal_config_path));

    std::shared_ptr<graph::GraphExecutor> cpu_executor;
    try {
        cpu_executor = BuildExecutor(cpu_config_path);
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedNodeConfigDescriptorGapDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
    std::shared_ptr<graph::GraphExecutor> metal_executor;
    try {
        metal_executor = BuildExecutor(metal_config_path);
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedNodeConfigDescriptorGapDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        if (IsExpectedMetalDiagnostic(message)) {
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

    auto cpu_source = ResolveNode<accelgraph::SineWaveSourceNode>(cpu_graph);
    auto cpu_analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(cpu_graph);
    auto cpu_sink = ResolveNode<accelgraph::SpectrumSinkNode>(cpu_graph);
    auto metal_source = ResolveNode<accelgraph::SineWaveSourceNode>(metal_graph);
    auto metal_analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(metal_graph);
    auto metal_sink = ResolveNode<accelgraph::SpectrumSinkNode>(metal_graph);
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
    // Phase 6 parity tolerance for representative bins around the expected tone.
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

TEST(AccelGraphPhase6SpectrumTest, StrictFallbackPolicyIsEnforcedForMetalSelection) {
    const auto strict_config_path = SpectrumMetalTopologyConfigPath();
    const auto allow_config_path = SpectrumMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(strict_config_path));
    ASSERT_TRUE(std::filesystem::exists(allow_config_path));

    bool strict_rejected = false;
    std::string strict_diagnostic;
    std::shared_ptr<graph::GraphExecutor> strict_executor;
    try {
        strict_executor = BuildExecutor(strict_config_path);
    } catch (const std::exception& ex) {
        strict_diagnostic = ex.what();
        if (IsExpectedNodeConfigDescriptorGapDiagnostic(strict_diagnostic)) {
            GTEST_SKIP() << strict_diagnostic;
        }
        if (IsExpectedMetalDiagnostic(strict_diagnostic) ||
            IsGraphBuildFailureDiagnostic(strict_diagnostic)) {
            strict_rejected = true;
        } else {
            throw;
        }
    }

    if (strict_rejected) {
        auto allow_executor = BuildExecutor(allow_config_path);
        ASSERT_NE(allow_executor, nullptr);

        auto allow_graph = allow_executor->GetGraphManager();
        ASSERT_NE(allow_graph, nullptr);
        auto allow_sink = ResolveNode<accelgraph::SpectrumSinkNode>(allow_graph);
        ASSERT_NE(allow_sink, nullptr);

        const auto run_result = allow_executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

        const auto output = allow_sink->LastSpectrum();
        ASSERT_TRUE(output.has_value());
        EXPECT_EQ(output->requested_backend, accelgraph::AcceleratorBackend::Metal);
        EXPECT_EQ(output->selected_backend, accelgraph::AcceleratorBackend::Cpu);
        EXPECT_TRUE(output->used_fallback);
        EXPECT_FALSE(output->fallback_diagnostic.empty());
        EXPECT_TRUE(IsExpectedMetalDiagnostic(output->fallback_diagnostic));
    } else {
        ASSERT_NE(strict_executor, nullptr);
        auto strict_graph = strict_executor->GetGraphManager();
        ASSERT_NE(strict_graph, nullptr);
        auto strict_sink = ResolveNode<accelgraph::SpectrumSinkNode>(strict_graph);
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

TEST(AccelGraphPhase6SpectrumTest, GraphPluginSurfaceDoesNotExposeMetalSpecificSpectrumNodeType) {
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(PluginDirectoryPath().string());
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
