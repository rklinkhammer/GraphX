// SPDX-License-Identifier: MIT

/**
 * @file test_image_comparator_metrics.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_IMAGE_COMPARATOR_PATH
#define SAR_IMAGE_COMPARATOR_PATH "examples/SAR/tools/sar_image_comparator.py"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

std::string PythonCommandPrefix() {
    return "PYTHONDONTWRITEBYTECODE=1 python3 -B";
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;
    nlohmann::json value;
    input >> value;
    return value;
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write json file: " << path;
    output << value.dump(2) << '\n';
}

void WriteFloat32Raster(const std::filesystem::path& path, const std::vector<float>& values) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write raw raster: " << path;
    for (const float value : values) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

nlohmann::json MakeContract(const std::filesystem::path& raw_path,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::string source_tool,
                            std::string provenance_class) {
    return {
        {"source_tool", std::move(source_tool)},
        {"provenance_class", std::move(provenance_class)},
        {"scenario_id", "scenario_001"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", width},
        {"height", height},
        {"byte_count", static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * sizeof(float)},
        {"raw_path", raw_path.string()},
    };
}

std::filesystem::path WriteArtifactsAndContracts(const std::filesystem::path& temp_dir,
                                                 const std::vector<float>& graphx_pixels,
                                                 const std::vector<float>& reference_pixels,
                                                 std::uint32_t width,
                                                 std::uint32_t height) {
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    EXPECT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, graphx_pixels);
    WriteFloat32Raster(reference_raw, reference_pixels);

    WriteJson(temp_dir / "graphx_contract.json", MakeContract(graphx_raw, width, height, "graphx", "graphx_runtime"));
    WriteJson(
        temp_dir / "reference_contract.json",
        MakeContract(reference_raw, width, height, "cpu-reference-backprojection", "deterministic_internal_reference"));
    return temp_dir;
}

std::string BuildCompareCommand(const std::filesystem::path& temp_dir,
                                const std::string& extra_args = std::string()) {
    std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(temp_dir / "graphx_contract.json") +
        " --reference-contract " + Quote(temp_dir / "reference_contract.json") +
        " --report-json " + Quote(temp_dir / "report.json");
    if (!extra_args.empty()) {
        command += " " + extra_args;
    }
    command += " > /dev/null";
    return command;
}

} // namespace

TEST(ImageComparatorMetricsTest, StrictModePassesForIdenticalArtifactsAndIsDeterministic) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_image_metrics_strict_identical";
    const std::vector<float> pixels{
        0.0f, 0.1f, 0.0f, 0.0f,
        0.2f, 1.0f, 0.2f, 0.0f,
        0.0f, 0.2f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    WriteArtifactsAndContracts(temp_dir, pixels, pixels, 4u, 4u);

    const auto command = BuildCompareCommand(temp_dir, "--strict");
    ASSERT_EQ(std::system(command.c_str()), 0);
    const auto report_1 = LoadJson(temp_dir / "report.json");

    ASSERT_EQ(std::system(command.c_str()), 0);
    const auto report_2 = LoadJson(temp_dir / "report.json");

    EXPECT_EQ(report_1.dump(), report_2.dump());
    EXPECT_EQ(report_1.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report_1.at("passed").get<bool>());
    EXPECT_EQ(report_1.at("thresholds").at("mode").get<std::string>(), "strict");

    const auto& metrics = report_1.at("metrics");
    EXPECT_DOUBLE_EQ(metrics.at("rms").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("relative_l2").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("max_abs_error").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("peak_coordinate_delta_pixels").get<double>(), 0.0);
    EXPECT_TRUE(metrics.contains("pslr_db"));
    EXPECT_TRUE(metrics.contains("islr_db"));
}

TEST(ImageComparatorMetricsTest, ConfiguredThresholdsAllowBoundedMismatchAndReportPeakDelta) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_image_metrics_configured_thresholds";
    const std::vector<float> graphx_pixels{
        0.0f, 0.1f, 0.0f, 0.0f,
        0.2f, 0.0f, 1.02f, 0.2f,
        0.0f, 0.2f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    const std::vector<float> reference_pixels{
        0.0f, 0.1f, 0.0f, 0.0f,
        0.2f, 1.0f, 0.0f, 0.2f,
        0.0f, 0.2f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    WriteArtifactsAndContracts(temp_dir, graphx_pixels, reference_pixels, 4u, 4u);

    WriteJson(
        temp_dir / "thresholds.json",
        nlohmann::json{
            {"peak_window_radius_pixels", 1},
            {"rms_max", 1.0},
            {"relative_l2_max", 2.0},
            {"max_abs_error_max", 2.0},
            {"peak_coordinate_delta_pixels_max", 2.0},
            {"pslr_db_min", nullptr},
            {"islr_db_min", nullptr},
        });

    const auto command = BuildCompareCommand(temp_dir, "--thresholds-json " + Quote(temp_dir / "thresholds.json"));
    ASSERT_EQ(std::system(command.c_str()), 0);

    const auto report = LoadJson(temp_dir / "report.json");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report.at("passed").get<bool>());
    EXPECT_EQ(report.at("thresholds").at("mode").get<std::string>(), "configured");

    const auto& metrics = report.at("metrics");
    EXPECT_GT(metrics.at("max_abs_error").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(metrics.at("peak_coordinate_delta_x_pixels").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(metrics.at("peak_coordinate_delta_y_pixels").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("peak_coordinate_delta_pixels").get<double>(), 1.0);
}

TEST(ImageComparatorMetricsTest, StrictModeFailsWhenMetricThresholdsAreExceeded) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_image_metrics_strict_fail";
    const std::vector<float> graphx_pixels{
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    const std::vector<float> reference_pixels{
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    };
    WriteArtifactsAndContracts(temp_dir, graphx_pixels, reference_pixels, 4u, 4u);

    const auto command = BuildCompareCommand(temp_dir, "--strict");
    ASSERT_NE(std::system(command.c_str()), 0);

    const auto report = LoadJson(temp_dir / "report.json");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "fail");
    EXPECT_FALSE(report.at("passed").get<bool>());
    const auto reasons = report.at("reasons").dump();
    EXPECT_NE(reasons.find("peak_coordinate_delta_within_threshold"), std::string::npos);
}

TEST(ImageComparatorMetricsTest, MalformedThresholdConfigurationFailsExplicitly) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_image_metrics_bad_thresholds";
    const std::vector<float> pixels(16u, 0.25f);
    WriteArtifactsAndContracts(temp_dir, pixels, pixels, 4u, 4u);

    WriteJson(
        temp_dir / "thresholds.json",
        nlohmann::json{
            {"peak_window_radius_pixels", 1},
            {"relative_l2_max", 0.0},
            {"max_abs_error_max", 0.0},
            {"peak_coordinate_delta_pixels_max", 0.0},
            {"pslr_db_min", nullptr},
            {"islr_db_min", nullptr},
        });

    const auto stderr_path = temp_dir / "stderr.log";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(temp_dir / "graphx_contract.json") +
        " --reference-contract " + Quote(temp_dir / "reference_contract.json") +
        " --thresholds-json " + Quote(temp_dir / "thresholds.json") +
        " --report-json " + Quote(temp_dir / "report.json") +
        " 1>/dev/null 2>" + Quote(stderr_path);

    ASSERT_NE(std::system(command.c_str()), 0);
    const auto stderr_text = ReadTextFile(stderr_path);
    EXPECT_NE(stderr_text.find("thresholds.rms_max is required"), std::string::npos);
}