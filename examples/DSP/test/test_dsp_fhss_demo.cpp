// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_DEMO_EXECUTABLE_PATH
#define DSP_FHSS_DEMO_EXECUTABLE_PATH "./graphx-dsp-fhss-demo"
#endif

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                       \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH
#define DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH                                   \
  "libdsp/config/fhss_phase2_binary_iq_receiver.json"
#endif

#ifndef DSP_FHSS_DEMO_MESSAGE_PATH
#define DSP_FHSS_DEMO_MESSAGE_PATH                                             \
  "examples/DSP/fixtures/fhss_demo_messages.json"
#endif

#ifndef DSP_FHSS_MESSAGE_TOOL_PATH
#define DSP_FHSS_MESSAGE_TOOL_PATH "examples/DSP/tools/fhss_message_tool.py"
#endif

#ifndef DSP_PYTHON_EXECUTABLE
#define DSP_PYTHON_EXECUTABLE "python3"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

constexpr int ExecutorTimeoutSeconds(const int native_seconds) {
#if defined(__SANITIZE_ADDRESS__)
  return native_seconds * 5;
#else
  return native_seconds;
#endif
}

std::string ShellQuote(const std::filesystem::path &path) {
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

CommandResult RunCommand(const std::string &command) {
  std::array<char, 512> buffer{};
  std::string output;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return CommandResult{.exit_code = -1, .output = "popen failed"};
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    output += buffer.data();
  }
  return CommandResult{.exit_code = pclose(pipe), .output = output};
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

void ExpectContains(const std::string &text, const std::string &needle) {
  EXPECT_NE(text.find(needle), std::string::npos) << text;
}

void ExpectNotContains(const std::string &text, const std::string &needle) {
  EXPECT_EQ(text.find(needle), std::string::npos) << text;
}

std::filesystem::path TempOutputDir(const std::string &name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove_all(path, error);
  std::filesystem::create_directories(path, error);
  return path;
}

} // namespace

TEST(DspFhssDemoExecutableTest, PrintsHelp) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  ASSERT_TRUE(std::filesystem::exists(executable)) << executable;

  const auto result = RunCommand(ShellQuote(executable) + " --help 2>&1");
  EXPECT_EQ(result.exit_code, 0) << result.output;
  ExpectContains(result.output, "graphx-dsp-fhss-demo");
  ExpectContains(result.output, "--message-json");
  ExpectContains(result.output, "--channel-iq-dir");
  ExpectContains(result.output, "SigMF");
  ExpectContains(result.output, "GraphExecutorBuilder");
  ExpectNotContains(result.output, "--reference-correlator-graph");
}

TEST(DspFhssDemoExecutableTest, DashboardModesAreMutuallyExclusive) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  ASSERT_TRUE(std::filesystem::exists(executable)) << executable;

  const auto result = RunCommand(ShellQuote(executable) +
                                 " --dashboard --dashboard-no-run 2>&1");
  EXPECT_NE(result.exit_code, 0) << result.output;
  ExpectContains(result.output,
                 "--dashboard and --dashboard-no-run are mutually exclusive");
}

TEST(DspFhssDevelopmentEnvironmentTest,
     MessageToolCreatesAndValidatesRunnableSchedule) {
  const auto output_dir = TempOutputDir("graphx_dsp_fhss_message_tool_test");
  const auto message_path = output_dir / "messages.json";
  const auto python = std::filesystem::path(DSP_PYTHON_EXECUTABLE);
  const auto tool = std::filesystem::path(DSP_FHSS_MESSAGE_TOOL_PATH);

  const std::string create_command =
      ShellQuote(python) + " " + ShellQuote(tool) + " create " +
      ShellQuote(message_path) +
      " --message-id 77 --transmit-start-sample 0"
      " --active-frequencies 24,28,32,36"
      " --preamble-words 0xaaaaaaaa,0x77777777,0x12121212,0x62626262"
      " --body 36:0xdeadbeef --body 24:0x12345678 2>&1";
  const auto create_result = RunCommand(create_command);
  EXPECT_EQ(create_result.exit_code, 0) << create_result.output;
  ASSERT_TRUE(std::filesystem::exists(message_path));

  const std::string add_command =
      ShellQuote(python) + " " + ShellQuote(tool) + " add-message " +
      ShellQuote(message_path) +
      " --message-id 78 --transmit-start-sample 130000"
      " --active-frequencies 24,28,32,36"
      " --preamble-words 0xaaaaaaaa,0x77777777,0x12121212,0x62626262"
      " --body 28:0xcafebabe 2>&1";
  const auto add_result = RunCommand(add_command);
  EXPECT_EQ(add_result.exit_code, 0) << add_result.output;

  const auto validate_result =
      RunCommand(ShellQuote(python) + " " + ShellQuote(tool) + " validate " +
                 ShellQuote(message_path) + " 2>&1");
  EXPECT_EQ(validate_result.exit_code, 0) << validate_result.output;
  ExpectContains(validate_result.output, "Valid FHSS schedule");

  const auto schedule = LoadJson(message_path);
  EXPECT_EQ(schedule.at("messages").size(), 2u);
  EXPECT_EQ(schedule.at("messages").at(0).at("pulses").size(), 18u);
  EXPECT_EQ(schedule.at("messages")
                .at(0)
                .at("pulses")
                .at(16)
                .at("value")
                .get<std::uint32_t>(),
            0xdeadbeefu);
  EXPECT_EQ(schedule.at("messages")
                .at(1)
                .at("pulses")
                .at(16)
                .at("value")
                .get<std::uint32_t>(),
            0xcafebabeu);
}

TEST(DspFhssDevelopmentEnvironmentTest,
     DemoCapturesSelectedChannelizerOutputAsSigMf) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  const auto output_dir = TempOutputDir("graphx_dsp_fhss_sigmf_test");
  const auto capture_dir = output_dir / "channel_iq";
  const auto summary_path = output_dir / "summary.json";

  const std::string command =
      ShellQuote(executable) + " --graph-config " +
      ShellQuote(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)) +
      " --plugin-dir " +
      ShellQuote(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY)) +
      " --message-json " +
      ShellQuote(std::filesystem::path(DSP_FHSS_DEMO_MESSAGE_PATH)) +
      " --channel-iq-dir " + ShellQuote(capture_dir) +
      " --channel-iq-indices 24 --summary-json " + ShellQuote(summary_path) +
      " --executor-timeout-s " + std::to_string(ExecutorTimeoutSeconds(12)) +
      " 2>&1";

  const auto result = RunCommand(command);
  EXPECT_EQ(result.exit_code, 0) << result.output;

  const auto data_path = capture_dir / "channel_24_frequency_24.sigmf-data";
  const auto metadata_path = capture_dir / "channel_24_frequency_24.sigmf-meta";
  ASSERT_TRUE(std::filesystem::exists(data_path)) << result.output;
  ASSERT_TRUE(std::filesystem::exists(metadata_path)) << result.output;
  EXPECT_GT(std::filesystem::file_size(data_path), 0u);

  const auto metadata = LoadJson(metadata_path);
  EXPECT_EQ(metadata.at("global").at("core:datatype").get<std::string>(),
            "cf32_le");
  EXPECT_DOUBLE_EQ(metadata.at("global").at("core:sample_rate").get<double>(),
                   500'000'000.0);
  EXPECT_EQ(
      metadata.at("global").at("graphx:frequency_index").get<std::uint32_t>(),
      24u);

  const auto summary = LoadJson(summary_path);
  EXPECT_TRUE(summary.at("channel_iq_capture").at("enabled").get<bool>());
  EXPECT_EQ(summary.at("channel_iq_capture").at("metadata_files").size(), 1u);
}

TEST(DspFhssDemoExecutableTest, RunsDefaultChannelizedGraphAndWritesSummary) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  const auto output_dir = TempOutputDir("graphx_dsp_fhss_demo_default_test");
  const auto summary_path = output_dir / "summary.json";

  const std::string command =
      ShellQuote(executable) + " --graph-config " +
      ShellQuote(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)) +
      " --plugin-dir " +
      ShellQuote(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY)) +
      " --summary-json " + ShellQuote(summary_path) +
      " --decoded-pulse-limit 3 --executor-timeout-s " +
      std::to_string(ExecutorTimeoutSeconds(30)) + " 2>&1";

  const auto result = RunCommand(command);
  EXPECT_EQ(result.exit_code, 0) << result.output;
  ExpectContains(result.output, "GraphX DSP FHSS demo runtime");
  ExpectContains(result.output, "Canonical FHSS graph: true");
  ExpectContains(result.output, "Preamble lock: true");
  ASSERT_TRUE(std::filesystem::exists(summary_path)) << result.output;

  const auto summary = LoadJson(summary_path);
  EXPECT_EQ(summary.at("schema").get<std::string>(),
            "graphx.dsp.fhss_demo_summary.v1");
  EXPECT_TRUE(summary.at("canonical_fhss_graph").get<bool>());
  EXPECT_TRUE(summary.at("completion_signaled").get<bool>());
  EXPECT_EQ(summary.at("graph").at("node_count").get<std::size_t>(), 75u);
  EXPECT_EQ(summary.at("fhss_diagnostics")
                .at("truth_mismatch_count")
                .get<std::size_t>(),
            0u);
  EXPECT_TRUE(summary.at("fhss_diagnostics").at("preamble_lock").get<bool>());
}

TEST(DspFhssDemoExecutableTest,
     BinaryReceiverCompletesWithEmptyDiagnosticsForNoDetectionCapture) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  const auto output_dir =
      TempOutputDir("graphx_dsp_fhss_no_detection_completion_test");
  const auto iq_path = output_dir / "no_detection.cf32";
  const auto config_path = output_dir / "receiver.json";
  const auto summary_path = output_dir / "summary.json";

  constexpr std::size_t kComplexSamples = 20'000;
  const std::vector<float> zero_iq(kComplexSamples * 2u, 0.0F);
  {
    std::ofstream output(iq_path, std::ios::binary);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char *>(zero_iq.data()),
                 static_cast<std::streamsize>(zero_iq.size() * sizeof(float)));
    ASSERT_TRUE(output.good());
  }

  auto config =
      LoadJson(std::filesystem::path(DSP_FHSS_BINARY_RECEIVER_CONFIG_PATH));
  config.at("nodes").at(0).at("node_config")["file_path"] = iq_path.string();
  {
    std::ofstream output(config_path);
    ASSERT_TRUE(output.good());
    output << config.dump(2) << '\n';
  }

  const std::string command =
      ShellQuote(executable) + " --graph-config " + ShellQuote(config_path) +
      " --plugin-dir " +
      ShellQuote(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY)) +
      " --summary-json " + ShellQuote(summary_path) +
      " --executor-timeout-s " + std::to_string(ExecutorTimeoutSeconds(8)) +
      " 2>&1";
  const auto result = RunCommand(command);
  EXPECT_EQ(result.exit_code, 0) << result.output;
  ASSERT_TRUE(std::filesystem::exists(summary_path)) << result.output;

  const auto summary = LoadJson(summary_path);
  EXPECT_TRUE(summary.at("execution_result").at("success").get<bool>());
  EXPECT_TRUE(summary.at("completion_signaled").get<bool>());
  EXPECT_EQ(summary.at("fhss_diagnostics").at("pulse_count").get<std::size_t>(),
            0u);
  EXPECT_FALSE(summary.at("fhss_diagnostics").at("preamble_lock").get<bool>());
  EXPECT_TRUE(summary.at("fhss_diagnostics").at("decoded_pulses").empty());
}

TEST(DspFhssDemoExecutableTest, AcceptsExternalMessageJsonForInvestigation) {
  const std::filesystem::path executable{DSP_FHSS_DEMO_EXECUTABLE_PATH};
  const auto output_dir = TempOutputDir("graphx_dsp_fhss_demo_message_test");
  const auto summary_path = output_dir / "summary.json";
  const auto effective_path = output_dir / "effective.json";

  const std::string command =
      ShellQuote(executable) + " --graph-config " +
      ShellQuote(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH)) +
      " --plugin-dir " +
      ShellQuote(std::filesystem::path(DSP_PLUGIN_OUTPUT_DIRECTORY)) +
      " --message-json " +
      ShellQuote(std::filesystem::path(DSP_FHSS_DEMO_MESSAGE_PATH)) +
      " --summary-json " + ShellQuote(summary_path) +
      " --effective-config-json " + ShellQuote(effective_path) +
      " --decoded-pulse-limit 4 --executor-timeout-s " +
      std::to_string(ExecutorTimeoutSeconds(12)) + " 2>&1";

  const auto result = RunCommand(command);
  EXPECT_EQ(result.exit_code, 0) << result.output;
  ExpectContains(result.output, "Pulse count: 20");
  ASSERT_TRUE(std::filesystem::exists(summary_path)) << result.output;
  ASSERT_TRUE(std::filesystem::exists(effective_path)) << result.output;

  const auto summary = LoadJson(summary_path);
  const auto &diagnostics = summary.at("fhss_diagnostics");
  EXPECT_EQ(diagnostics.at("pulse_count").get<std::size_t>(), 20u);
  EXPECT_TRUE(diagnostics.at("preamble_lock").get<bool>());
  EXPECT_EQ(diagnostics.at("truth_mismatch_count").get<std::size_t>(), 0u);
  ASSERT_TRUE(diagnostics.at("decoded_pulses").is_array());
  ASSERT_GE(diagnostics.at("decoded_pulses").size(), 20u);
  EXPECT_EQ(diagnostics.at("decoded_pulses")
                .at(16)
                .at("decoded_value")
                .get<std::uint32_t>(),
            3735928559u);
  EXPECT_EQ(diagnostics.at("decoded_pulses")
                .at(17)
                .at("decoded_value")
                .get<std::uint32_t>(),
            3405691582u);
}
