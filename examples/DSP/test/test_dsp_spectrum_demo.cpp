// SPDX-License-Identifier: MIT

/**
 * @file test_dsp_spectrum_demo.cpp
 * @brief GraphX source file.
 */

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

#ifndef DSP_METAL_SPECTRUM_CONFIG_PATH
#define DSP_METAL_SPECTRUM_CONFIG_PATH "libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef DSP_CPU_VS_METAL_SCHEMA_PATH
#define DSP_CPU_VS_METAL_SCHEMA_PATH "examples/DSP/tools/dsp_cpu_vs_metal_performance_report.schema.json"
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

std::string EnvPrefix(std::initializer_list<std::pair<std::string, std::string>> values) {
    std::string prefix;
    for (const auto& [name, value] : values) {
        prefix += name + "=" + value + " ";
    }
    return prefix;
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

void ExpectContains(const std::string& output, const std::string& expected) {
    EXPECT_NE(output.find(expected), std::string::npos) << output;
}

void ExpectObjectHasFields(const nlohmann::json& object,
                           const std::vector<std::string>& fields) {
    ASSERT_TRUE(object.is_object());
    for (const auto& field : fields) {
        EXPECT_TRUE(object.contains(field)) << "missing field: " << field;
    }
}

std::filesystem::path WriteComparisonReport(uint32_t warmup_iterations,
                                            uint32_t measured_iterations,
                                            const std::string& output_name) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path cpu_config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path gpu_config{DSP_METAL_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};
    const auto output_dir = std::filesystem::temp_directory_path() /
                            "graphx_dsp_cpu_vs_metal_timing_test";
    std::error_code cleanup_error;
    std::filesystem::create_directories(output_dir, cleanup_error);
    const auto report_path = output_dir / output_name;

    const std::string command =
        ShellQuote(executable) +
        " --compare-cpu-metal"
        " --cpu-config " + ShellQuote(cpu_config) +
        " --gpu-config " + ShellQuote(gpu_config) +
        " --plugin-dir " + ShellQuote(plugin_dir) +
        " --warmup-iterations " + std::to_string(warmup_iterations) +
        " --measured-iterations " + std::to_string(measured_iterations) +
        " --executor-timeout-s 8"
        " --report-json " + ShellQuote(report_path) +
        " 2>&1";
    const auto result = RunCommand(command);
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_TRUE(std::filesystem::exists(report_path)) << result.output;
    return report_path;
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

TEST(DspCpuVsMetalExecuteTimingTest, WritesInformationalComparisonReport) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path cpu_config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path gpu_config{DSP_METAL_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};
    const auto output_dir = std::filesystem::temp_directory_path() /
                            "graphx_dsp_cpu_vs_metal_timing_test";
    const auto report_path = output_dir / "comparison_report.json";
    std::error_code cleanup_error;
    std::filesystem::remove_all(output_dir, cleanup_error);

    ASSERT_TRUE(std::filesystem::exists(executable)) << executable;
    ASSERT_TRUE(std::filesystem::exists(cpu_config)) << cpu_config;
    ASSERT_TRUE(std::filesystem::exists(gpu_config)) << gpu_config;
    ASSERT_TRUE(std::filesystem::exists(plugin_dir)) << plugin_dir;

    const std::string command =
        ShellQuote(executable) +
        " --compare-cpu-metal"
        " --cpu-config " + ShellQuote(cpu_config) +
        " --gpu-config " + ShellQuote(gpu_config) +
        " --plugin-dir " + ShellQuote(plugin_dir) +
        " --warmup-iterations 1"
        " --measured-iterations 2"
        " --executor-timeout-s 8"
        " --report-json " + ShellQuote(report_path) +
        " 2>&1";
    const auto result = RunCommand(command);

    EXPECT_EQ(result.exit_code, 0) << result.output;
    ExpectContains(result.output, "GraphX DSP CPU vs Metal execute-timing comparison");
    ExpectContains(result.output, "Timing source: GraphExecutor::Execute() ExecutionResult");
    ExpectContains(result.output, "Mode: informational");
    ASSERT_TRUE(std::filesystem::exists(report_path)) << result.output;

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("schema").get<std::string>(),
              "graphx.dsp.cpu_vs_metal_execute_timing.v1");
    EXPECT_EQ(report.at("mode").get<std::string>(), "informational");
    ASSERT_TRUE(report.contains("strict_gate"));
    EXPECT_FALSE(report.at("strict_gate").at("enabled").get<bool>());
    EXPECT_EQ(report.at("strict_gate").at("status").get<std::string>(), "disabled");
    EXPECT_EQ(report.at("measurement_context").get<std::string>(),
              "measured on this host/config");
    EXPECT_FALSE(report.at("build_preset_or_binary_path").get<std::string>().empty());
    EXPECT_EQ(report.at("timing_source").get<std::string>(),
              "GraphExecutor::Execute() ExecutionResult");
    EXPECT_EQ(report.at("warmup_iterations").get<int>(), 1);
    EXPECT_EQ(report.at("measured_iterations").get<int>(), 2);
    EXPECT_EQ(report.at("cpu_config_path").get<std::string>(), cpu_config.string());
    EXPECT_EQ(report.at("gpu_config_path").get<std::string>(), gpu_config.string());

    ASSERT_TRUE(report.contains("cpu"));
    ASSERT_TRUE(report.contains("gpu"));
    EXPECT_EQ(report.at("cpu").at("status").get<std::string>(), "ok");
    EXPECT_EQ(report.at("cpu").at("warmup_iterations").size(), 1u);
    EXPECT_EQ(report.at("cpu").at("measured_iterations").size(), 2u);

    const auto first_cpu_iteration = report.at("cpu").at("measured_iterations").at(0);
    ASSERT_TRUE(first_cpu_iteration.contains("execute_result"));
    const auto execute_result = first_cpu_iteration.at("execute_result");
    EXPECT_TRUE(execute_result.contains("elapsed_time_ms"));
    EXPECT_TRUE(execute_result.contains("init_elapsed_time_ms"));
    EXPECT_TRUE(execute_result.contains("start_elapsed_time_ms"));
    EXPECT_TRUE(execute_result.contains("run_elapsed_time_ms"));
    EXPECT_TRUE(execute_result.contains("stop_elapsed_time_ms"));
    EXPECT_TRUE(execute_result.contains("join_elapsed_time_ms"));
    EXPECT_TRUE(first_cpu_iteration.at("completion_signaled").get<bool>());
    EXPECT_NEAR(first_cpu_iteration.at("peak_frequency_hz").get<double>(), 1000.0, 187.5);
    EXPECT_GT(first_cpu_iteration.at("peak_magnitude").get<double>(), 0.0);
    ASSERT_TRUE(first_cpu_iteration.contains("selected_bins"));
    EXPECT_FALSE(first_cpu_iteration.at("selected_bins").empty());

    const auto cpu_elapsed_summary =
        report.at("cpu").at("summary").at("elapsed_time_ms");
    EXPECT_EQ(cpu_elapsed_summary.at("count").get<int>(), 2);
    EXPECT_TRUE(cpu_elapsed_summary.contains("min"));
    EXPECT_TRUE(cpu_elapsed_summary.contains("median"));
    EXPECT_TRUE(cpu_elapsed_summary.contains("mean"));
    EXPECT_TRUE(cpu_elapsed_summary.contains("stddev"));

    const auto gpu_status = report.at("gpu").at("status").get<std::string>();
    if (report.at("native_metal_available").get<bool>()) {
        EXPECT_EQ(gpu_status, "ok") << result.output;
        EXPECT_EQ(report.at("gpu").at("measured_iterations").size(), 2u);
    } else {
        EXPECT_EQ(gpu_status, "unavailable") << result.output;
        EXPECT_TRUE(report.at("gpu").at("measured_iterations").empty());
        EXPECT_FALSE(report.at("native_metal_diagnostics").get<std::string>().empty());
    }

    ASSERT_TRUE(report.contains("speedup_ratio"));
    ASSERT_TRUE(report.contains("correctness_summary"));

    std::filesystem::remove_all(output_dir, cleanup_error);
}

TEST(DspCpuVsMetalExecuteTimingTest, ReportMatchesStableSchemaContract) {
    const auto schema = LoadJson(std::filesystem::path(DSP_CPU_VS_METAL_SCHEMA_PATH));
    const auto report_path = WriteComparisonReport(0, 1, "schema_contract_report.json");
    const auto report = LoadJson(report_path);

    ASSERT_TRUE(schema.contains("required"));
    std::vector<std::string> top_level_fields;
    for (const auto& field : schema.at("required")) {
        top_level_fields.push_back(field.get<std::string>());
    }
    ExpectObjectHasFields(report, top_level_fields);
    EXPECT_EQ(report.at("schema").get<std::string>(),
              schema.at("properties").at("schema").at("const").get<std::string>());
    EXPECT_EQ(report.at("timing_source").get<std::string>(),
              "GraphExecutor::Execute() ExecutionResult");
    EXPECT_EQ(report.at("mode").get<std::string>(), "informational");

    ExpectObjectHasFields(report.at("strict_gate"),
                          {"enabled", "required_env", "threshold_env",
                           "min_speedup_ratio", "basis", "status", "message"});
    ExpectObjectHasFields(report.at("speedup_ratio"),
                          {"elapsed_time_ms", "run_elapsed_time_ms"});
    ExpectObjectHasFields(report.at("cpu"),
                          {"status", "message", "warmup_iterations",
                           "measured_iterations", "summary"});
    ExpectObjectHasFields(report.at("cpu").at("summary"),
                          {"elapsed_time_ms", "run_elapsed_time_ms"});

    const auto execute_result =
        report.at("cpu").at("measured_iterations").at(0).at("execute_result");
    ExpectObjectHasFields(execute_result,
                          {"success", "message", "elapsed_time_ms",
                           "init_elapsed_time_ms", "start_elapsed_time_ms",
                           "run_elapsed_time_ms", "stop_elapsed_time_ms",
                           "join_elapsed_time_ms", "error_details"});
}

TEST(DspCpuVsMetalExecuteTimingTest, StatisticsHandleOneMeasuredIteration) {
    const auto report_path = WriteComparisonReport(0, 1, "one_iteration_report.json");
    const auto report = LoadJson(report_path);
    const auto elapsed_summary =
        report.at("cpu").at("summary").at("elapsed_time_ms");
    const auto run_summary =
        report.at("cpu").at("summary").at("run_elapsed_time_ms");

    EXPECT_EQ(elapsed_summary.at("count").get<int>(), 1);
    EXPECT_EQ(run_summary.at("count").get<int>(), 1);
    EXPECT_DOUBLE_EQ(elapsed_summary.at("stddev").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(run_summary.at("stddev").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(elapsed_summary.at("min").get<double>(),
                     elapsed_summary.at("median").get<double>());
    EXPECT_DOUBLE_EQ(elapsed_summary.at("mean").get<double>(),
                     elapsed_summary.at("median").get<double>());
}

TEST(DspCpuVsMetalExecuteTimingTest, CpuAndGpuConfigsUseEquivalentSineSettings) {
    const auto cpu_config = LoadJson(std::filesystem::path(DSP_SPECTRUM_CONFIG_PATH));
    const auto gpu_config = LoadJson(std::filesystem::path(DSP_METAL_SPECTRUM_CONFIG_PATH));

    ASSERT_TRUE(cpu_config.contains("nodes"));
    ASSERT_TRUE(gpu_config.contains("nodes"));

    const auto find_sine_config = [](const nlohmann::json& config) {
        for (const auto& node : config.at("nodes")) {
            if (node.at("type").get<std::string>() == "SineSignalNode<256>") {
                return node.at("node_config");
            }
        }
        throw std::runtime_error("missing SineSignalNode<256>");
    };

    const auto cpu_sine = find_sine_config(cpu_config);
    const auto gpu_sine = find_sine_config(gpu_config);
    EXPECT_DOUBLE_EQ(cpu_sine.at("frequency_hz").get<double>(), -1000.0);
    EXPECT_DOUBLE_EQ(gpu_sine.at("frequency_hz").get<double>(), -1000.0);
    EXPECT_DOUBLE_EQ(cpu_sine.at("amplitude").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(gpu_sine.at("amplitude").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(cpu_sine.at("sample_rate_hz").get<double>(), 48000.0);
    EXPECT_DOUBLE_EQ(gpu_sine.at("sample_rate_hz").get<double>(), 48000.0);
}

TEST(DspCpuVsMetalExecuteTimingTest, DefaultComparisonModeDoesNotFailForUnavailableOrSlowerMetal) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path cpu_config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path gpu_config{DSP_METAL_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};

    const std::string command =
        ShellQuote(executable) +
        " --compare-cpu-metal"
        " --cpu-config " + ShellQuote(cpu_config) +
        " --gpu-config " + ShellQuote(gpu_config) +
        " --plugin-dir " + ShellQuote(plugin_dir) +
        " --warmup-iterations 0"
        " --measured-iterations 1"
        " --executor-timeout-s 8"
        " 2>&1";
    const auto result = RunCommand(command);

    EXPECT_EQ(result.exit_code, 0) << result.output;
    ExpectContains(result.output, "Comparison completed informationally.");
}

TEST(DspCpuVsMetalExecuteTimingTest, StrictGateRequiresExplicitEnvironmentOptIn) {
    const auto report_path = WriteComparisonReport(0, 1, "strict_disabled_report.json");
    const auto report = LoadJson(report_path);

    EXPECT_EQ(report.at("mode").get<std::string>(), "informational");
    EXPECT_FALSE(report.at("strict_gate").at("enabled").get<bool>());
    EXPECT_EQ(report.at("strict_gate").at("required_env").get<std::string>(),
              "GRAPHX_DSP_REQUIRE_METAL_SPEEDUP");
    EXPECT_EQ(report.at("strict_gate").at("threshold_env").get<std::string>(),
              "GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO");
    EXPECT_EQ(report.at("strict_gate").at("basis").get<std::string>(),
              "run_elapsed_time_ms");
    EXPECT_EQ(report.at("strict_gate").at("status").get<std::string>(), "disabled");
}

TEST(DspCpuVsMetalExecuteTimingTest, StrictGateFailsClearlyWhenMetalUnavailable) {
    const std::filesystem::path executable{DSP_SPECTRUM_DEMO_EXECUTABLE_PATH};
    const std::filesystem::path cpu_config{DSP_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path gpu_config{DSP_METAL_SPECTRUM_CONFIG_PATH};
    const std::filesystem::path plugin_dir{DSP_PLUGIN_OUTPUT_DIRECTORY};
    const auto output_dir = std::filesystem::temp_directory_path() /
                            "graphx_dsp_cpu_vs_metal_timing_test";
    const auto report_path = output_dir / "strict_gate_report.json";
    std::error_code cleanup_error;
    std::filesystem::create_directories(output_dir, cleanup_error);

    const std::string command =
        EnvPrefix({{"GRAPHX_DSP_REQUIRE_METAL_SPEEDUP", "1"},
                   {"GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO", "1.25"}}) +
        ShellQuote(executable) +
        " --compare-cpu-metal"
        " --cpu-config " + ShellQuote(cpu_config) +
        " --gpu-config " + ShellQuote(gpu_config) +
        " --plugin-dir " + ShellQuote(plugin_dir) +
        " --warmup-iterations 0"
        " --measured-iterations 1"
        " --executor-timeout-s 8"
        " --report-json " + ShellQuote(report_path) +
        " 2>&1";
    const auto result = RunCommand(command);

    ASSERT_TRUE(std::filesystem::exists(report_path)) << result.output;
    const auto report = LoadJson(report_path);
    ASSERT_TRUE(report.at("strict_gate").at("enabled").get<bool>());
    EXPECT_EQ(report.at("mode").get<std::string>(), "gate_enforced");
    EXPECT_DOUBLE_EQ(report.at("strict_gate").at("min_speedup_ratio").get<double>(), 1.25);

    if (!report.at("native_metal_available").get<bool>()) {
        EXPECT_NE(result.exit_code, 0) << result.output;
        EXPECT_EQ(report.at("strict_gate").at("status").get<std::string>(),
                  "native_metal_unavailable");
        ExpectContains(result.output, "Strict gate status: native_metal_unavailable");
    } else {
        if (report.at("strict_gate").at("status").get<std::string>() == "passed") {
            EXPECT_EQ(result.exit_code, 0) << result.output;
        } else {
            EXPECT_NE(result.exit_code, 0) << result.output;
            ExpectContains(result.output, "Strict gate status:");
        }
    }
}
