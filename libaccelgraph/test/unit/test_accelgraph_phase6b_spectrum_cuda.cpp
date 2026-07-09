// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/SpectrumGraphNodes.hpp"
#include "config/JsonView.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

namespace {

std::filesystem::path CpuSpectrumTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase6b_spectrum_cpu_topology.json";
}

std::filesystem::path CudaSpectrumTopologyConfigPath() {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / "accelgraph_phase6b_spectrum_cuda_topology.json";
}

std::filesystem::path PluginDirectoryPath() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

std::shared_ptr<graph::GraphExecutor> BuildExecutor(const std::filesystem::path& config_path) {
    return graph::GraphExecutorBuilder()
        .WithJsonConfig(config_path.string())
        .WithPluginDirectory(PluginDirectoryPath().string())
        .WithExecutorTimeout(std::chrono::seconds(20))
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

void ConfigureNode(const std::shared_ptr<graph::INode>& node,
                   const nlohmann::json& config) {
    ASSERT_NE(node, nullptr);
    auto* configurable = dynamic_cast<graph::IConfigurable*>(node.get());
    ASSERT_NE(configurable, nullptr);
    configurable->Configure(graph::JsonView(config));
}

bool IsExpectedCudaSkipDiagnostic(const std::string& message) {
    return message.find(accelgraph::kCudaSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaToolkitUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaRuntimeHeadersUnavailableDiagnostic) != std::string::npos ||
           message.find("driver") != std::string::npos ||
           message.find("device") != std::string::npos ||
           message.find("CUDA") != std::string::npos;
}

std::shared_ptr<accelgraph::SpectrumSinkNode>
ConfigureAndRunSpectrumGraph(const std::filesystem::path& config_path,
                             const std::string& backend,
                             const std::string& fallback_policy,
                             int cuda_device_ordinal = 0) {
    auto executor = BuildExecutor(config_path);
    EXPECT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    EXPECT_NE(graph_manager, nullptr);

    auto source = ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);

    EXPECT_NE(source, nullptr);
    EXPECT_NE(analysis, nullptr);
    EXPECT_NE(sink, nullptr);

    ConfigureNode(source, nlohmann::json{
        {"sample_rate_hz", 48000.0},
        {"tone_frequency_hz", 1000.0},
        {"amplitude", 1.0},
        {"phase_radians", 0.0},
        {"complex_sample_count", 256},
        {"frame_count", 1},
    });

    ConfigureNode(analysis, nlohmann::json{
        {"backend", backend},
        {"fallback_policy", fallback_policy},
        {"output_bins", 129},
        {"cuda_device_ordinal", cuda_device_ordinal},
    });

    ConfigureNode(sink, nlohmann::json::object());

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    return sink;
}

}  // namespace

TEST(AccelGraphPhase6BCudaSpectrumTest, CpuCudaParityAndStrictNativeExecutionViaGraphExecutor) {
    ASSERT_TRUE(std::filesystem::exists(CpuSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(CudaSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    try {
        auto cpu_sink = ConfigureAndRunSpectrumGraph(
            CpuSpectrumTopologyConfigPath(),
            "cpu",
            "strict");
        ASSERT_NE(cpu_sink, nullptr);
        ASSERT_TRUE(cpu_sink->LastSpectrum().has_value());

        auto cuda_sink = ConfigureAndRunSpectrumGraph(
            CudaSpectrumTopologyConfigPath(),
            "cuda",
            "strict",
            0);
        ASSERT_NE(cuda_sink, nullptr);
        ASSERT_TRUE(cuda_sink->LastSpectrum().has_value());

        const auto cpu = cpu_sink->LastSpectrum().value();
        const auto cuda = cuda_sink->LastSpectrum().value();

        ASSERT_FALSE(cpu.magnitudes.empty());
        ASSERT_EQ(cpu.magnitudes.size(), cuda.magnitudes.size());

        EXPECT_EQ(cuda.requested_backend, accelgraph::AcceleratorBackend::Cuda);
        EXPECT_EQ(cuda.selected_backend, accelgraph::AcceleratorBackend::Cuda);
        EXPECT_FALSE(cuda.used_fallback);
        EXPECT_TRUE(cuda.fallback_diagnostic.empty());

        EXPECT_EQ(cpu.peak_bin, cuda.peak_bin);

        const double bin_hz = 48000.0 / 256.0;
        EXPECT_NEAR(cpu.peak_frequency_hz, cuda.peak_frequency_hz, bin_hz);

        std::vector<std::size_t> compare_bins{1, 2, 3, 4, 5, cpu.peak_bin};
        if (cpu.peak_bin > 0) {
            compare_bins.push_back(cpu.peak_bin - 1);
        }
        if (cpu.peak_bin + 1 < cpu.magnitudes.size()) {
            compare_bins.push_back(cpu.peak_bin + 1);
        }

        for (const auto idx : compare_bins) {
            if (idx >= cpu.magnitudes.size()) {
                continue;
            }
            const float cpu_mag = cpu.magnitudes[idx];
            const float cuda_mag = cuda.magnitudes[idx];
            const float tolerance = std::max(1e-4f, std::abs(cpu_mag) * 1e-3f);
            EXPECT_NEAR(cpu_mag, cuda_mag, tolerance) << "bin=" << idx;
        }
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedCudaSkipDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}

TEST(AccelGraphPhase6BCudaSpectrumTest, StrictFallbackPolicyIsEnforced) {
    ASSERT_TRUE(std::filesystem::exists(CpuSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    try {
        auto executor = BuildExecutor(CpuSpectrumTopologyConfigPath());
        ASSERT_NE(executor, nullptr);

        auto graph_manager = executor->GetGraphManager();
        ASSERT_NE(graph_manager, nullptr);

        auto source = ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
        auto analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
        auto sink = ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
        ASSERT_NE(source, nullptr);
        ASSERT_NE(analysis, nullptr);
        ASSERT_NE(sink, nullptr);

        ConfigureNode(source, nlohmann::json{
            {"sample_rate_hz", 48000.0},
            {"tone_frequency_hz", 1000.0},
            {"amplitude", 1.0},
            {"phase_radians", 0.0},
            {"complex_sample_count", 256},
            {"frame_count", 1},
        });

        EXPECT_THROW(
            ConfigureNode(analysis, nlohmann::json{
                {"backend", "cuda"},
                {"fallback_policy", "strict"},
                {"output_bins", 129},
                {"cuda_device_ordinal", 9999},
            }),
            graph::ConfigError);

        ConfigureNode(analysis, nlohmann::json{
            {"backend", "cuda"},
            {"fallback_policy", "allow"},
            {"output_bins", 129},
            {"cuda_device_ordinal", 9999},
        });
        ConfigureNode(sink, nlohmann::json::object());

        const auto run_result = executor->Execute();
        ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
        ASSERT_TRUE(sink->LastSpectrum().has_value());

        const auto resolved = sink->LastSpectrum().value();
        EXPECT_EQ(resolved.requested_backend, accelgraph::AcceleratorBackend::Cuda);
        EXPECT_EQ(resolved.selected_backend, accelgraph::AcceleratorBackend::Cpu);
        EXPECT_TRUE(resolved.used_fallback);
        EXPECT_FALSE(resolved.fallback_diagnostic.empty());
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedCudaSkipDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}
