#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

#ifndef SAR_EXAMPLE_EXECUTABLE_PATH
#define SAR_EXAMPLE_EXECUTABLE_PATH "./sar_example"
#endif

#ifndef SAR_DEFINITIVE_JSON_CONFIG_PATH
#define SAR_DEFINITIVE_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_definitive.json"
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
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

void ExpectContains(const std::string& output, const std::string& expected) {
    EXPECT_NE(output.find(expected), std::string::npos) << output;
}

} // namespace

TEST(SarMainExecutableTest, DefinitiveConfigReportsRuntimeAndDiagnostics) {
    const std::filesystem::path executable{SAR_EXAMPLE_EXECUTABLE_PATH};
    const std::filesystem::path config{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};

    ASSERT_TRUE(std::filesystem::exists(executable)) << executable;
    ASSERT_TRUE(std::filesystem::exists(config)) << config;
    ASSERT_TRUE(std::filesystem::exists(plugin_dir)) << plugin_dir;

    const std::string command = ShellQuote(executable) + " " +
                                ShellQuote(config) + " " +
                                ShellQuote(plugin_dir) + " 2>&1";
    const auto result = RunCommand(command);

    EXPECT_EQ(result.exit_code, 0) << result.output;
    ExpectContains(result.output, "GraphX SAR example runtime");
    ExpectContains(result.output, "Topology config:");
    ExpectContains(result.output, "Plugin directory:");
    ExpectContains(result.output, "Loaded nodes: 9");
    ExpectContains(result.output, "Loaded edges: 8");
    ExpectContains(result.output, "Execution completed successfully.");
    ExpectContains(result.output, "Completion signaled: true");
    ExpectContains(result.output, "Diagnostics queue_backpressure_events:");
    ExpectContains(result.output, "Diagnostics peak_queue_depth:");
}
