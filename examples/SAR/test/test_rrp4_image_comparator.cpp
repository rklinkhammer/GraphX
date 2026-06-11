#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_RRP4_IMAGE_COMPARATOR_PATH
#define SAR_RRP4_IMAGE_COMPARATOR_PATH "examples/SAR/tools/rrp4_image_comparator.py"
#endif

#ifndef SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH
#define SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH "examples/SAR/tools/rrp4_image_comparison_report.schema.json"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

std::string PythonCommandPrefix() {
    return "PYTHONDONTWRITEBYTECODE=1 python3 -B";
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

nlohmann::json MakeContract(std::string source_tool,
                            const std::filesystem::path& raw_path,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::string scenario_id = "scenario_001") {
    return {
        {"source_tool", std::move(source_tool)},
        {"scenario_id", std::move(scenario_id)},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", width},
        {"height", height},
        {"byte_count", static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u},
        {"raw_path", raw_path.string()},
    };
}

} // namespace

TEST(Rrp4ImageComparatorTest, MatchingImageContractsProducePassReport) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_comparator_pass";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const std::vector<float> pixels(16u * 16u, 0.25f);
    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, pixels);
    WriteFloat32Raster(reference_raw, pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(graphx_contract_path, MakeContract("graphx", graphx_raw, 16u, 16u));
    WriteJson(reference_contract_path, MakeContract("gotcha-back", reference_raw, 16u, 16u));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(report_path));

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("schema_version").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report.at("passed").get<bool>());
    ASSERT_TRUE(report.at("checks").is_array());
    ASSERT_TRUE(report.at("reasons").is_array());
    EXPECT_TRUE(report.at("reasons").empty());

    const auto& metrics = report.at("metrics");
    EXPECT_DOUBLE_EQ(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("rms").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("relative_l2").get<double>(), 0.0);
}

TEST(Rrp4ImageComparatorTest, MismatchedImageContractsProduceFailReport) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_comparator_fail";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    std::vector<float> graphx_pixels(16u * 16u, 0.25f);
    std::vector<float> reference_pixels(16u * 16u, 0.25f);
    graphx_pixels[17] = 0.75f;

    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, graphx_pixels);
    WriteFloat32Raster(reference_raw, reference_pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(graphx_contract_path, MakeContract("graphx", graphx_raw, 16u, 16u));
    WriteJson(reference_contract_path, MakeContract("gotcha-back", reference_raw, 16u, 16u));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_NE(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(report_path));

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("verdict").get<std::string>(), "fail");
    EXPECT_FALSE(report.at("passed").get<bool>());
    ASSERT_TRUE(report.at("checks").is_array());
    ASSERT_FALSE(report.at("reasons").empty());

    const auto& metrics = report.at("metrics");
    EXPECT_GT(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_GT(metrics.at("rms").get<double>(), 0.0);
    EXPECT_GT(metrics.at("relative_l2").get<double>(), 0.0);
}

TEST(Rrp4ImageComparatorTest, ReportSchemaDeclaresPassFailShape) {
    const auto schema_path = std::filesystem::path{SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH};
    ASSERT_TRUE(std::filesystem::exists(schema_path));

    const auto schema = LoadJson(schema_path);
    EXPECT_EQ(schema.at("$id").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    ASSERT_TRUE(schema.contains("required"));
    ASSERT_TRUE(schema.contains("properties"));

    const auto required = schema.at("required");
    ASSERT_TRUE(required.is_array());
    EXPECT_NE(std::find(required.begin(), required.end(), "verdict"), required.end());
    EXPECT_NE(std::find(required.begin(), required.end(), "checks"), required.end());
    EXPECT_NE(std::find(required.begin(), required.end(), "metrics"), required.end());
}