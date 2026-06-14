#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_LOCAL_RUNNER_PATH
#define SAR_LOCAL_RUNNER_PATH "examples/SAR/tools/sar_local_runner.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
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

TEST(Rrp1LocalRunnerTest, CreatesExpectedArtifactLayoutFromScenario001) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_rrp1_runner_test_output";
    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(scenario_path) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);

    EXPECT_TRUE(std::filesystem::exists(output_dir / "manifest" / "scenario_001.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "graphx" / "graphx_config.json"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "graphx" / "run_graphx.sh"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "reference" / "run_gotcha_back.sh"));
    EXPECT_TRUE(std::filesystem::exists(output_dir / "reports" / "orchestration_plan.json"));

    const auto plan = LoadJson(output_dir / "reports" / "orchestration_plan.json");
    EXPECT_EQ(plan.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(plan.at("status").get<std::string>(), "prepared");
    EXPECT_FALSE(plan.at("requires_external_data").get<bool>());
    EXPECT_FALSE(plan.at("requires_external_reference_binary").get<bool>());
}

TEST(Rrp1LocalRunnerTest, RejectsMissingScenarioPath) {
    const auto output_dir = std::filesystem::temp_directory_path() / "graphx_rrp1_runner_missing_scenario";
    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const auto missing_scenario = output_dir / "missing_scenario.json";
    const std::string command =
        "python3 " + Quote(std::filesystem::path{SAR_LOCAL_RUNNER_PATH}) +
        " --scenario " + Quote(missing_scenario) +
        " --output-dir " + Quote(output_dir) +
        " > /dev/null 2>&1";

    EXPECT_NE(std::system(command.c_str()), 0);
}