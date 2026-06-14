#include <gtest/gtest.h>

#include "sar/io/GotchaMatInspector.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class GotchaMatInspectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_mat_inspector_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    void WriteHdf5SignatureFixture(const std::string& relative) const {
        const std::array<unsigned char, 8> signature{
            0x89U, 0x48U, 0x44U, 0x46U, 0x0dU, 0x0aU, 0x1aU, 0x0aU};
        std::ofstream stream{Path(relative), std::ios::binary};
        ASSERT_TRUE(stream);
        stream.write(reinterpret_cast<const char*>(signature.data()),
                     static_cast<std::streamsize>(signature.size()));
        stream << "synthetic hdf5-signature mat v7.3 fixture";
    }

    void WriteClassicMatHeaderFixture(const std::string& relative) const {
        std::ofstream stream{Path(relative), std::ios::binary};
        ASSERT_TRUE(stream);
        std::string header = "MATLAB 5.0 MAT-file, Platform: GraphX, Created by naming lint test";
        header.resize(128, ' ');
        stream.write(header.data(), static_cast<std::streamsize>(header.size()));
    }

    [[nodiscard]] graphx::sar::GotchaMatInspectionResult Inspect(
        const std::string& input_relative) const {
        return graphx::sar::GotchaMatInspector::Inspect(graphx::sar::GotchaMatInspectionOptions{
            .input_path = Path(input_relative),
            .output_directory = Path("inspection"),
        });
    }

    [[nodiscard]] nlohmann::json LoadJson(const std::filesystem::path& path) const {
        std::ifstream stream{path};
        EXPECT_TRUE(stream) << path;
        nlohmann::json value{};
        stream >> value;
        return value;
    }

    std::filesystem::path root_{};
};

std::vector<std::string> ErrorCodes(const graphx::sar::GotchaMatInspectionResult& result) {
    std::vector<std::string> codes{};
    for (const auto& error : result.errors) {
        codes.push_back(error.code);
    }
    return codes;
}

} // namespace

TEST_F(GotchaMatInspectorTest, DetectsMatlabV73Hdf5SignatureWithoutMatlab) {
    WriteHdf5SignatureFixture("tiny_v73.mat");

    const auto result = Inspect("tiny_v73.mat");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.mat_format, "matlab_v7_3_hdf5");
    EXPECT_TRUE(std::filesystem::exists(result.field_inventory_path));
    EXPECT_TRUE(std::filesystem::exists(result.conversion_assumptions_path));

    const auto inventory = LoadJson(result.field_inventory_path);
    EXPECT_EQ(inventory.at("mat_format"), "matlab_v7_3_hdf5");
    EXPECT_EQ(inventory.at("hdf5_signature_detected"), true);
    EXPECT_EQ(inventory.at("fields").at(0).at("path"), "/");
    EXPECT_EQ(inventory.at("fields").at(0).at("dtype"), "hdf5_container");
}

TEST_F(GotchaMatInspectorTest, ClassicMatProducesDeterministicUnsupportedFormatError) {
    WriteClassicMatHeaderFixture("classic.mat");

    const auto result = Inspect("classic.mat");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.mat_format, "classic_or_non_hdf5_mat");
    EXPECT_EQ(result.status, "unsupported_format");
    EXPECT_EQ(ErrorCodes(result), std::vector<std::string>{"classic_mat_unsupported"});

    const auto inventory = LoadJson(result.field_inventory_path);
    EXPECT_EQ(inventory.at("inspection_status"), "unsupported_format");
    EXPECT_EQ(inventory.at("errors").at(0).at("code"), "classic_mat_unsupported");
}

TEST_F(GotchaMatInspectorTest, FieldInventoryReportHasStableShapeKeysAndDtypes) {
    WriteHdf5SignatureFixture("tiny_v73.mat");

    const auto result = Inspect("tiny_v73.mat");
    const auto inventory = LoadJson(result.field_inventory_path);

    EXPECT_EQ(inventory.at("schema"), "graphx.sar.mat_field_inventory.v1");
    ASSERT_TRUE(inventory.at("fields").is_array());
    ASSERT_FALSE(inventory.at("fields").empty());
    const auto field = inventory.at("fields").at(0);
    EXPECT_TRUE(field.contains("key"));
    EXPECT_TRUE(field.contains("path"));
    EXPECT_TRUE(field.contains("kind"));
    EXPECT_TRUE(field.contains("shape"));
    EXPECT_TRUE(field.at("shape").is_array());
    EXPECT_TRUE(field.contains("dtype"));
}

TEST_F(GotchaMatInspectorTest, ConversionAssumptionsReportHasStableInspectionOnlyShape) {
    WriteHdf5SignatureFixture("tiny_v73.mat");

    const auto result = Inspect("tiny_v73.mat");
    const auto assumptions = LoadJson(result.conversion_assumptions_path);

    EXPECT_EQ(assumptions.at("schema"), "graphx.sar.conversion_assumptions.v1");
    EXPECT_EQ(assumptions.at("matlab_dependency"), "not_used");
    EXPECT_EQ(assumptions.at("normalized_product_emitted"), false);
    EXPECT_EQ(assumptions.at("graphx_crsd_lite_emitted"), false);
    EXPECT_EQ(assumptions.at("crsd_emitted"), false);
    ASSERT_TRUE(assumptions.at("assumptions").is_array());
}

TEST_F(GotchaMatInspectorTest, NoMatlabBehaviorIsEncodedInReports) {
    WriteClassicMatHeaderFixture("classic.mat");

    const auto result = Inspect("classic.mat");
    const auto assumptions = LoadJson(result.conversion_assumptions_path);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(assumptions.at("matlab_dependency"), "not_used");
    EXPECT_EQ(assumptions.at("normalized_product_emitted"), false);
    EXPECT_EQ(assumptions.at("crsd_emitted"), false);
    EXPECT_EQ(assumptions.at("errors").at(0).at("code"), "classic_mat_unsupported");
}
