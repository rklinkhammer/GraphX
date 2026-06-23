// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_DEMO_EXECUTABLE_PATH
#define DSP_FHSS_DEMO_EXECUTABLE_PATH "./graphx-dsp-fhss-demo"
#endif

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                           \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef DSP_FHSS_DEMO_MESSAGE_PATH
#define DSP_FHSS_DEMO_MESSAGE_PATH "examples/DSP/fixtures/fhss_demo_messages.json"
#endif

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

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
  ExpectContains(result.output, "GraphExecutorBuilder");
  ExpectNotContains(result.output, "--reference-correlator-graph");
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
      " --decoded-pulse-limit 3 --executor-timeout-s 12 2>&1";

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
      " --decoded-pulse-limit 4 --executor-timeout-s 12 2>&1";

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
  EXPECT_EQ(diagnostics.at("decoded_pulses").at(16).at("decoded_value").get<std::uint32_t>(),
            3735928559u);
  EXPECT_EQ(diagnostics.at("decoded_pulses").at(17).at("decoded_value").get<std::uint32_t>(),
            3405691582u);
}
