// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

namespace {

std::filesystem::path SourceRoot() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT);
}

std::string LoadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open text file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open JSON file: " + path.string());
    }
    nlohmann::json value;
    input >> value;
    return value;
}

void ExpectContains(const std::string& text, const std::string& needle) {
    EXPECT_NE(text.find(needle), std::string::npos) << "missing: " << needle;
}

void ExpectNotContains(const std::string& text, const std::string& needle) {
    EXPECT_EQ(text.find(needle), std::string::npos) << "unexpected: " << needle;
}

std::string ActiveMetalSpectrumDftText() {
    const auto root = SourceRoot();
    return LoadText(root / "libdsp/include/dsp/MetalSpectrumDftNode.hpp") + "\n" +
           LoadText(root / "libdsp/src/dsp/MetalSpectrumDftNode.cpp") + "\n" +
           LoadText(root / "libdsp/plugins/metal_spectrum_dft_node_256_plugin.cpp");
}

std::string ActiveDspPerformanceDocsText() {
    const auto root = SourceRoot();
    return LoadText(root / "README.md");
}

}  // namespace

TEST(DspGpuTruthInLabelingTest, GpuDocsStateDirectDftNotFft) {
    const auto readme = LoadText(SourceRoot() / "README.md");

    ExpectContains(readme, "DSP Spectrum Demo And GPU DFT Lane");
    ExpectContains(readme, "CpuSpectrumDftNode<float, 256>");
    ExpectContains(readme, "Metal direct DFT");
    ExpectContains(readme, "not a GPU FFT");
    ExpectContains(readme, "future true Metal FFT");
}

TEST(DspGpuTruthInLabelingTest, GpuNodeNamesDoNotClaimFftForDftImplementation) {
    const auto config = LoadJson(SourceRoot() / "libdsp/config/dsp_sine_metal_dft_spectrum_256.json");
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    bool saw_metal_dft = false;
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        if (type.find("Metal") != std::string::npos) {
            EXPECT_NE(type.find("Dft"), std::string::npos) << type;
            EXPECT_EQ(type.find("Fft"), std::string::npos) << type;
            EXPECT_EQ(type.find("FFT"), std::string::npos) << type;
        }
        saw_metal_dft = saw_metal_dft || type == "MetalSpectrumDftNode<256>";
    }
    EXPECT_TRUE(saw_metal_dft);

    const auto plugin = LoadText(SourceRoot() / "libdsp/plugins/metal_spectrum_dft_node_256_plugin.cpp");
    ExpectContains(plugin, "MetalSpectrumDftNode<256>");
    ExpectContains(plugin, "Metal direct DFT spectrum transform");
    ExpectNotContains(plugin, "GPU FFT");
}

TEST(DspGpuTruthInLabelingTest, GpuLabeledNodesRequireKernelTicketDiagnostics) {
    const auto source = LoadText(SourceRoot() / "libdsp/src/dsp/MetalSpectrumDftNode.cpp");
    const auto runtime_test =
        LoadText(SourceRoot() / "libdsp/test/unit/test_dsp_gpu_spectrum_graph_runtime.cpp");

    ExpectContains(source, "\"has_kernel_ticket\"");
    ExpectContains(source, "IsValidKernelTicket(last_kernel_ticket_)");
    ExpectContains(source, "\"algorithm\", \"direct_dft\"");
    ExpectContains(runtime_test, "has_kernel_ticket");
    ExpectContains(runtime_test, "kernel_registered");
}

TEST(DspGpuTruthInLabelingTest, MetalSpectrumDftDoesNotReferenceFFTManager) {
    const auto combined = ActiveMetalSpectrumDftText();

    ExpectNotContains(combined, "FFTManager");
    ExpectNotContains(combined, "ProcessPacket");
    ExpectContains(combined, "direct DFT");
    ExpectContains(combined, "does not implement an optimized FFT");
}

TEST(DspGpuTruthInLabelingTest, CpuConfigRemainsCpuOnly) {
    const auto config = LoadJson(SourceRoot() / "libdsp/config/dsp_sine_fft_spectrum_256.json");
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    bool saw_sine = false;
    bool saw_fft = false;
    bool saw_sink = false;
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        EXPECT_EQ(type.find("Metal"), std::string::npos) << type;
        EXPECT_EQ(type.find("GPU"), std::string::npos) << type;
        EXPECT_EQ(type.find("Gpu"), std::string::npos) << type;
        EXPECT_NE(type, "DspIqH2DNode<256>");
        EXPECT_NE(type, "MetalSpectrumDftNode<256>");
        EXPECT_NE(type, "DspMagnitudeD2HNode<256>");

        saw_sine = saw_sine || type == "SineSignalNode<256>";
        saw_fft = saw_fft || type == "CpuSpectrumDftNode<256>";
        saw_sink = saw_sink || type == "SpectrumSinkNode<256>";
    }

    EXPECT_TRUE(saw_sine);
    EXPECT_TRUE(saw_fft);
    EXPECT_TRUE(saw_sink);
}

TEST(DspGpuTruthInLabelingTest, PerformanceDocsRequireMeasuredOnHostQualifier) {
    const auto readme = LoadText(SourceRoot() / "README.md");

    ExpectContains(readme, "execute-timing comparisons");
    ExpectContains(readme, "measured on the current host/config");
}

TEST(DspGpuTruthInLabelingTest, PerformanceDocsDoNotClaimGeneralGpuSuperiority) {
    const auto text = ActiveDspPerformanceDocsText();

    ExpectNotContains(text, "Metal is faster");
    ExpectNotContains(text, "GPU is faster");
    ExpectNotContains(text, "Metal outperforms");
    ExpectNotContains(text, "GPU outperforms");
    ExpectNotContains(text, "general GPU superiority");
    ExpectNotContains(text, "guaranteed speedup");
}

TEST(DspGpuTruthInLabelingTest, MetalDftIsNeverDocumentedAsGpuFft) {
    const auto text = ActiveDspPerformanceDocsText() + "\n" +
                      ActiveMetalSpectrumDftText();

    ExpectContains(text, "Metal direct DFT");
    ExpectContains(text, "not a GPU FFT");
    ExpectContains(text, "not implement an optimized FFT");

    ExpectNotContains(text, "MetalSpectrumDftNode<256>` is a GPU FFT");
    ExpectNotContains(text, "MetalSpectrumDftNode<256> is a GPU FFT");
    ExpectNotContains(text, "MetalSpectrumDftNode<256>` implements a GPU FFT");
    ExpectNotContains(text, "MetalSpectrumDftNode<256> implements a GPU FFT");
}

TEST(DspGpuTruthInLabelingTest, DefaultCiDoesNotRequireSpeedup) {
    const auto dsp_test_cmake = LoadText(SourceRoot() / "examples/DSP/test/CMakeLists.txt");
    const auto libdsp_test_cmake = LoadText(SourceRoot() / "libdsp/test/CMakeLists.txt");
    const auto readme = LoadText(SourceRoot() / "README.md");
    const auto example_test = LoadText(SourceRoot() / "examples/DSP/test/test_dsp_spectrum_demo.cpp");

    ExpectNotContains(dsp_test_cmake, "GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1");
    ExpectNotContains(libdsp_test_cmake, "GRAPHX_DSP_REQUIRE_METAL_SPEEDUP=1");
    ExpectContains(readme, "not part of default CI");
    ExpectContains(example_test, "DefaultComparisonModeDoesNotFailForUnavailableOrSlowerMetal");
    ExpectContains(example_test, "StrictGateRequiresExplicitEnvironmentOptIn");
}

TEST(DspGpuTruthInLabelingTest, PerformanceReportsUseExecuteResultTiming) {
    const auto schema = LoadJson(
        SourceRoot() / "examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json");
    const auto runner = LoadText(SourceRoot() / "examples/DSP/src/main.cpp");
    const auto readme = LoadText(SourceRoot() / "README.md");

    EXPECT_EQ(schema.at("properties").at("timing_source").at("const").get<std::string>(),
              "GraphExecutor::Execute() ExecutionResult");
    EXPECT_EQ(schema.at("properties").at("strict_gate")
                  .at("properties").at("basis").at("const").get<std::string>(),
              "run_elapsed_time_ms");
    ExpectContains(runner, "&graph::ExecutionResult::run_elapsed_time_ms");
    ExpectContains(runner, "Timing source: GraphExecutor::Execute() ExecutionResult");
    ExpectContains(readme, "`GraphExecutor::Execute()`");
    ExpectContains(readme, "`GraphExecutor::Execute()`");
}
