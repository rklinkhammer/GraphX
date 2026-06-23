// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_LOCAL_BASELINE_RUNNER_PATH
#define SAR_LOCAL_BASELINE_RUNNER_PATH "examples/SAR/tools/sar_local_baseline_runner.py"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;

    nlohmann::json value;
    input >> value;
    return value;
}

} // namespace

TEST(SarLocalBaselineRunnerTest, ProbeDeclaresLocalOnlyBoundary) {
    const auto runner_path = std::filesystem::path{SAR_LOCAL_BASELINE_RUNNER_PATH};
    ASSERT_TRUE(std::filesystem::exists(runner_path));

    const auto output_path = std::filesystem::temp_directory_path() / "graphx_sar_local_baseline_probe.json";
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    const std::string command =
        "python3 " + Quote(runner_path) +
        " probe-environment --output-json " + Quote(output_path) +
        " > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    const auto probe = LoadJson(output_path);
    EXPECT_TRUE(probe.at("local_only").get<bool>());
    EXPECT_FALSE(probe.at("ci_safe").get<bool>());
    EXPECT_EQ(probe.at("selected_baseline").get<std::string>(), "SarPy");
    EXPECT_EQ(probe.at("opt_in_env").get<std::string>(), "GRAPHX_SAR_BASELINE_RUNNER_ENABLE");
    EXPECT_EQ(probe.at("dataset_env").get<std::string>(), "GRAPHX_SARPY_CRSD_FILE");
}

TEST(SarLocalBaselineRunnerTest, CiSafeSkipWhenOptInNotEnabled) {
    const auto runner_path = std::filesystem::path{SAR_LOCAL_BASELINE_RUNNER_PATH};
    ASSERT_TRUE(std::filesystem::exists(runner_path));

    const auto output_path = std::filesystem::temp_directory_path() / "graphx_sar_local_baseline_skip.json";
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    const std::string command =
        "GRAPHX_SAR_BASELINE_RUNNER_ENABLE=0 "
        "python3 " + Quote(runner_path) +
        " run-local-smoke --output-json " + Quote(output_path) +
        " > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    const auto report = LoadJson(output_path);
    EXPECT_EQ(report.at("status").get<std::string>(), "skipped");
    EXPECT_EQ(report.at("reason").get<std::string>(), "local_opt_in_not_enabled");
}

TEST(SarLocalBaselineRunnerTest, ExplicitOptInExercisesLocalSmokePath) {
    const auto runner_path = std::filesystem::path{SAR_LOCAL_BASELINE_RUNNER_PATH};
    ASSERT_TRUE(std::filesystem::exists(runner_path));

    const auto output_path = std::filesystem::temp_directory_path() / "graphx_sar_local_baseline_optin.json";
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    // Use a known-missing path so the test remains deterministic and CI-safe.
    const auto fake_crsd = std::filesystem::temp_directory_path() / "graphx_missing_local_smoke.crsd";
    std::filesystem::remove(fake_crsd, ec);

    const std::string command =
        "GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1 "
        "GRAPHX_SARPY_CRSD_FILE=" + Quote(fake_crsd) + " "
        "python3 " + Quote(runner_path) +
        " run-local-smoke --output-json " + Quote(output_path) +
        " > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    const auto report = LoadJson(output_path);
    EXPECT_TRUE(report.contains("status"));
    EXPECT_TRUE(report.contains("reason"));

    const auto status = report.at("status").get<std::string>();
    const auto reason = report.at("reason").get<std::string>();

    EXPECT_EQ(status, "skipped");
    EXPECT_TRUE(
        reason == "sarpy_not_installed" ||
        reason == "crsd_path_missing" ||
        reason == "validate_failed" ||
        reason == "validate_tool_missing");
}
