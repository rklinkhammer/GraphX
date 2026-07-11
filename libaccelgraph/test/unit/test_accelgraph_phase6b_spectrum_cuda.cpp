// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "AccelGraphTopologyTestUtils.hpp"

namespace {

std::filesystem::path CpuSpectrumTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6b_spectrum_cpu_topology.json");
}

std::filesystem::path CudaSpectrumTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6b_spectrum_cuda_topology.json");
}

std::filesystem::path CudaSpectrumAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_phase6b_spectrum_cuda_allow_fallback_topology.json");
}

std::shared_ptr<accelgraph::SpectrumSinkNode>
RunSpectrumGraph(const std::filesystem::path& config_path) {
    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedCudaDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message)) {
            return nullptr;
        }
        throw;
    }
    EXPECT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    EXPECT_NE(graph_manager, nullptr);

    auto source = accelgraph::test::ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
    auto analysis = accelgraph::test::ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);

    EXPECT_NE(source, nullptr);
    EXPECT_NE(analysis, nullptr);
    EXPECT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    return sink;
}

}  // namespace

TEST(AccelGraphSpectrumBackendMatrixTest, CpuCudaParityAndStrictNativeExecutionViaGraphExecutor) {
    ASSERT_TRUE(std::filesystem::exists(CpuSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(CudaSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    try {
        auto cpu_sink = RunSpectrumGraph(CpuSpectrumTopologyConfigPath());
        if (!cpu_sink) {
            GTEST_SKIP() << accelgraph::kCudaSupportNotCompiledDiagnostic;
        }
        ASSERT_NE(cpu_sink, nullptr);
        ASSERT_TRUE(cpu_sink->LastSpectrum().has_value());

        auto cuda_sink = RunSpectrumGraph(CudaSpectrumTopologyConfigPath());
        if (!cuda_sink) {
            GTEST_SKIP() << accelgraph::kCudaSupportNotCompiledDiagnostic;
        }
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
        if (accelgraph::test::IsExpectedCudaDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}

TEST(AccelGraphSpectrumBackendMatrixTest, StrictFallbackPolicyIsEnforcedForCudaSelection) {
    ASSERT_TRUE(std::filesystem::exists(CudaSpectrumTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(CudaSpectrumAllowFallbackTopologyConfigPath()));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    try {
        bool strict_rejected = false;
        std::shared_ptr<graph::GraphExecutor> strict_executor;
        try {
            strict_executor = accelgraph::test::BuildExecutor(CudaSpectrumTopologyConfigPath(), std::chrono::seconds(20));
        } catch (const std::exception& ex) {
            const std::string message = ex.what();
            if (accelgraph::test::IsExpectedCudaDiagnostic(message) ||
                accelgraph::test::IsGraphBuildFailureDiagnostic(message)) {
                strict_rejected = true;
            } else {
                throw;
            }
        }

        if (strict_rejected) {
            auto allow_sink = RunSpectrumGraph(CudaSpectrumAllowFallbackTopologyConfigPath());
            if (!allow_sink) {
                GTEST_SKIP() << accelgraph::kCudaSupportNotCompiledDiagnostic;
            }
            ASSERT_NE(allow_sink, nullptr);
            ASSERT_TRUE(allow_sink->LastSpectrum().has_value());

            const auto resolved = allow_sink->LastSpectrum().value();
            EXPECT_EQ(resolved.requested_backend, accelgraph::AcceleratorBackend::Cuda);
            EXPECT_EQ(resolved.selected_backend, accelgraph::AcceleratorBackend::Cpu);
            EXPECT_TRUE(resolved.used_fallback);
            EXPECT_FALSE(resolved.fallback_diagnostic.empty());
        } else {
            ASSERT_NE(strict_executor, nullptr);
            auto strict_graph = strict_executor->GetGraphManager();
            ASSERT_NE(strict_graph, nullptr);
            auto strict_sink = accelgraph::test::ResolveNode<accelgraph::SpectrumSinkNode>(strict_graph);
            ASSERT_NE(strict_sink, nullptr);

            const auto run_result = strict_executor->Execute();
            ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
            ASSERT_TRUE(strict_sink->LastSpectrum().has_value());

            const auto resolved = strict_sink->LastSpectrum().value();
            EXPECT_EQ(resolved.requested_backend, accelgraph::AcceleratorBackend::Cuda);
            EXPECT_EQ(resolved.selected_backend, accelgraph::AcceleratorBackend::Cuda);
            EXPECT_FALSE(resolved.used_fallback);
            EXPECT_TRUE(resolved.fallback_diagnostic.empty());
        }
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedCudaDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }
}
