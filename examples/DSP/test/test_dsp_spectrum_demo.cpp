#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#ifndef DSP_SPECTRUM_DEMO_EXECUTABLE_PATH
#define DSP_SPECTRUM_DEMO_EXECUTABLE_PATH "./graphx-dsp-spectrum-demo"
#endif

#ifndef DSP_SPECTRUM_CONFIG_PATH
#define DSP_SPECTRUM_CONFIG_PATH "libdsp/config/dsp_sine_fft_spectrum_256.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

namespace {

std::string ShellQuote(const std::filesystem::path& path) {
    std::string raw = path.string();
    std::string quoted{"'"};
    for (const char ch : raw) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

struct CommandResult {
    int exit_code{};
    std::string output;
};

CommandResult RunCommand(const std::string& command) {
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return CommandResult{.exit_code = -1, .output = "popen failed"};
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    return CommandResult{.exit_code = pclose(pipe), .output = output};
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open JSON file: " + path.string());
    }
    nlohmann::json json;
    input >> json;
    return json;
}

std::string LoadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open text file: " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void ExpectContains(const std::string& output, const std::string& expected) {
    EXPECT_NE(output.find(expected), std::string::npos) << output;
}

}  // namespace

TEST(DspSpectrumDemoExecutableTest, RunsConfigAndReportsCpuOnlyRuntime) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};

    ASSERT_TRUE(std::filesystem::exists(executable)) << executable;
    ASSERT_TRUE(std::filesystem::exists(config)) << config;
    ASSERT_TRUE(std::filesystem::exists(plugin_dir)) << plugin_dir;

    const std::string command = ShellQuote(executable) + " " +
                                ShellQuote(config) + " " +
                                ShellQuote(plugin_dir) + " 2>&1";
    const auto result = RunCommand(command);

    EXPECT_EQ(result.exit_code, 0) << result.output;
    ExpectContains(result.output, "GraphX DSP spectrum demo runtime");
    ExpectContains(result.output, "Execution mode: CPU-only direct DFT");
    ExpectContains(result.output, "Loaded nodes: 3");
    ExpectContains(result.output, "Loaded edges: 2");
    ExpectContains(result.output, "Execution completed successfully.");
    ExpectContains(result.output, "Completion signaled: true");
    ExpectContains(result.output, "Spectrum frames:");
    ExpectContains(result.output, "Peak frequency (Hz):");
}

TEST(DspSpectrumDemoExecutableTest, WritesDeterministicSummaryJson) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};
    const auto output_dir = std::filesystem::temp_directory_path() /
                            "graphx_dsp_spectrum_demo_test";
    const auto summary_path = output_dir / "summary.json";
    std::error_code cleanup_error;
    std::filesystem::remove_all(output_dir, cleanup_error);

    const std::string command = ShellQuote(executable) + " " +
                                ShellQuote(config) + " " +
                                ShellQuote(plugin_dir) +
                                " --summary-json " + ShellQuote(summary_path) + " 2>&1";
    const auto result = RunCommand(command);

    EXPECT_EQ(result.exit_code, 0) << result.output;
    ASSERT_TRUE(std::filesystem::exists(summary_path)) << result.output;

    const auto summary = LoadJson(summary_path);
    EXPECT_EQ(summary.at("schema").get<std::string>(), "graphx.dsp.spectrum_summary.v1");
    EXPECT_TRUE(summary.at("cpu_only").get<bool>());
    EXPECT_TRUE(summary.at("completion_signaled").get<bool>());
    EXPECT_GE(summary.at("frame_count").get<std::size_t>(), 1u);
    EXPECT_NEAR(summary.at("peak_frequency_hz").get<double>(), 1000.0, 187.5);
    EXPECT_GT(summary.at("peak_magnitude").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(summary.at("sample_rate_hz").get<double>(), 48000.0);
    EXPECT_EQ(summary.at("fft_size").get<std::size_t>(), 256u);
    EXPECT_TRUE(summary.contains("window_type"));
    EXPECT_EQ(summary.at("window_type_name").get<std::string>(), "hann");
    ASSERT_TRUE(summary.contains("node_metrics"));
    ASSERT_TRUE(summary.at("node_metrics").contains("spectrum"));
    EXPECT_GE(summary.at("node_metrics").at("spectrum").at("frame_count").get<std::size_t>(), 1u);

    std::filesystem::remove_all(output_dir, cleanup_error);
}

TEST(DspSpectrumDemoGuardrailTest, ConfigDeclaresCpuOnlyDspNodeTypes) {
    const auto config = LoadJson(std::filesystem::path(DSP_SPECTRUM_CONFIG_PATH));
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    bool saw_sine = false;
    bool saw_fft = false;
    bool saw_spectrum = false;

    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        EXPECT_EQ(type.find("Metal"), std::string::npos) << type;
        EXPECT_EQ(type.find("GPU"), std::string::npos) << type;
        EXPECT_EQ(type.find("Gpu"), std::string::npos) << type;

        saw_sine = saw_sine || type == "SineSignalNode<256>";
        saw_fft = saw_fft || type == "FFTNode<256>";
        saw_spectrum = saw_spectrum || type == "SpectrumSinkNode<256>";
    }

    EXPECT_TRUE(saw_sine);
    EXPECT_TRUE(saw_fft);
    EXPECT_TRUE(saw_spectrum);
}

TEST(DspSpectrumDemoGuardrailTest, RunnerAndDocsStateCpuOnlyDirectDftTruthInLabeling) {
    const auto demo_source = std::filesystem::path(GRAPHX_SOURCE_ROOT) /
                             "examples/DSP/src/main.cpp";
    const auto doc_source = std::filesystem::path(GRAPHX_SOURCE_ROOT) /
                            "docs/dsp/spectrum_demo.md";
    const auto readme_source = std::filesystem::path(GRAPHX_SOURCE_ROOT) /
                               "README.md";

    const auto demo_text = LoadText(demo_source);
    const auto doc_text = LoadText(doc_source);
    const auto readme_text = LoadText(readme_source);

    ExpectContains(demo_text, "Execution mode: CPU-only direct DFT");

    ExpectContains(doc_text, "SineSignalNode<256> -> FFTNode<float, 256> -> SpectrumSinkNode<float, 256>");
    ExpectContains(doc_text, "CPU-only");
    ExpectContains(doc_text, "direct DFT");
    ExpectContains(doc_text, "not a GPU FFT");
    ExpectContains(doc_text, "not an external FFT library");

    // README is example-indexed in this repository, so keep a narrow DSP truth-in-labeling entry.
    ExpectContains(readme_text, "DSP Spectrum Demo (CPU-Only)");
    ExpectContains(readme_text, "CPU-only direct DFT");
}
