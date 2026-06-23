// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_GRAPHX_VS_BASELINE_HARNESS_PATH
#define SAR_GRAPHX_VS_BASELINE_HARNESS_PATH "examples/SAR/tools/sar_graphx_vs_baseline_harness.py"
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

void WriteFloat32Raster(const std::filesystem::path& path, const std::vector<float>& values) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write raster: " << path;
    for (const float value : values) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write json: " << path;
    output << value.dump(2) << '\n';
}

nlohmann::json MakeContract(const std::filesystem::path& raw_path,
                            const std::string& source_tool,
                            const std::string& provenance_class) {
    return {
        {"source_tool", source_tool},
        {"provenance_class", provenance_class},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", 4},
        {"height", 4},
        {"byte_count", 64},
        {"raw_path", raw_path.string()},
    };
}

} // namespace

TEST(GraphxVsBaselineHarnessTest, CiTinyFixtureComparisonIsDeterministicAndCiSafe) {
    const auto harness = std::filesystem::path{SAR_GRAPHX_VS_BASELINE_HARNESS_PATH};
    ASSERT_TRUE(std::filesystem::exists(harness));

    const auto root = std::filesystem::temp_directory_path() / "graphx_vs_baseline_ci_tiny";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    const auto output_json = root / "harness_output.json";
    const auto output_dir = root / "artifacts";

    const std::string command =
        "python3 " + Quote(harness) +
        " run-ci-tiny-fixture --output-dir " + Quote(output_dir) +
        " --output-json " + Quote(output_json) +
        " --strict > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_json));

    const auto first = LoadJson(output_json);
    ASSERT_EQ(std::system(command.c_str()), 0);
    const auto second = LoadJson(output_json);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first.at("mode").get<std::string>(), "ci_tiny_fixture");
    EXPECT_TRUE(first.at("ci_safe").get<bool>());
    EXPECT_FALSE(first.at("local_only").get<bool>());
    EXPECT_EQ(first.at("status").get<std::string>(), "pass");
}

TEST(GraphxVsBaselineHarnessTest, LocalComparisonSkipsWhenOptInNotEnabled) {
    const auto harness = std::filesystem::path{SAR_GRAPHX_VS_BASELINE_HARNESS_PATH};
    ASSERT_TRUE(std::filesystem::exists(harness));

    const auto root = std::filesystem::temp_directory_path() / "graphx_vs_baseline_local_skip";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    const auto output_json = root / "local_skip.json";
    const auto missing_graphx = root / "graphx_contract.json";
    const auto missing_reference = root / "reference_contract.json";

    const std::string command =
        "GRAPHX_SAR_BASELINE_RUNNER_ENABLE=0 "
        "python3 " + Quote(harness) +
        " run-local-comparison --graphx-contract " + Quote(missing_graphx) +
        " --reference-contract " + Quote(missing_reference) +
        " --output-json " + Quote(output_json) +
        " > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_json));

    const auto report = LoadJson(output_json);
    EXPECT_EQ(report.at("mode").get<std::string>(), "local_baseline_comparison");
    EXPECT_TRUE(report.at("local_only").get<bool>());
    EXPECT_FALSE(report.at("ci_safe").get<bool>());
    EXPECT_EQ(report.at("status").get<std::string>(), "skipped");
    EXPECT_EQ(report.at("reason").get<std::string>(), "local_opt_in_not_enabled");
}

TEST(GraphxVsBaselineHarnessTest, LocalComparisonRunsWhenEnabledWithContracts) {
    const auto harness = std::filesystem::path{SAR_GRAPHX_VS_BASELINE_HARNESS_PATH};
    ASSERT_TRUE(std::filesystem::exists(harness));

    const auto root = std::filesystem::temp_directory_path() / "graphx_vs_baseline_local_enabled";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    ASSERT_TRUE(std::filesystem::create_directories(root));

    const auto graphx_raw = root / "graphx.bin";
    const auto reference_raw = root / "reference.bin";
    const std::vector<float> pixels{
        0.0f, 0.1f, 0.0f, 0.0f,
        0.2f, 1.0f, 0.2f, 0.0f,
        0.0f, 0.2f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    WriteFloat32Raster(graphx_raw, pixels);
    WriteFloat32Raster(reference_raw, pixels);

    const auto graphx_contract = root / "graphx_contract.json";
    const auto reference_contract = root / "reference_contract.json";
    WriteJson(graphx_contract, MakeContract(graphx_raw, "graphx", "graphx_runtime"));
    WriteJson(reference_contract, MakeContract(reference_raw, "cpu-reference-backprojection", "deterministic_internal_reference"));

    const auto output_json = root / "local_enabled.json";
    const std::string command =
        "GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1 "
        "python3 " + Quote(harness) +
        " run-local-comparison --graphx-contract " + Quote(graphx_contract) +
        " --reference-contract " + Quote(reference_contract) +
        " --output-json " + Quote(output_json) +
        " --strict > /dev/null";

    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_json));

    const auto report = LoadJson(output_json);
    EXPECT_EQ(report.at("mode").get<std::string>(), "local_baseline_comparison");
    EXPECT_TRUE(report.at("local_only").get<bool>());
    EXPECT_EQ(report.at("status").get<std::string>(), "pass");
    ASSERT_TRUE(report.contains("comparison_report"));
    EXPECT_EQ(report.at("comparison_report").at("verdict").get<std::string>(), "pass");
}
