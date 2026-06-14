#include <gtest/gtest.h>

#include "sar/io/GotchaMatInspector.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

class GotchaFieldValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("graphx_gotcha_field_validation_" + std::to_string(now));
        ASSERT_TRUE(std::filesystem::create_directories(root_));
    }

    void TearDown() override {
        std::error_code error{};
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] std::filesystem::path Path(const std::string& relative) const {
        return root_ / relative;
    }

    void WriteSidecarJson(const std::string& relative, const nlohmann::json& data) const {
        std::ofstream stream{Path(relative)};
        ASSERT_TRUE(stream);
        stream << data.dump(2) << '\n';
    }

    [[nodiscard]] nlohmann::json MakeValidGotchaSidecar() const {
        return nlohmann::json{
            {"Np", 100},
            {"K", 512},
            {"deltaF", 1000000.0},
            {"minF", 9500000000.0},
            {"AntX", 0.0},
            {"AntY", 0.0},
            {"AntZ", 10.0},
            {"R0", 100000.0},
            {"phdata", nlohmann::json::array()},
        };
    }

    std::filesystem::path root_{};
};

TEST_F(GotchaFieldValidationTest, ValidGotchaSidecarPasses) {
    WriteSidecarJson("valid.json", MakeValidGotchaSidecar());

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("valid.json"));

    EXPECT_TRUE(result.is_valid());
    EXPECT_TRUE(result.missing_fields.empty());
    EXPECT_TRUE(result.type_errors.empty());
}

TEST_F(GotchaFieldValidationTest, MissingNpFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("Np");
    WriteSidecarJson("missing_np.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_np.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "Np");
}

TEST_F(GotchaFieldValidationTest, MissingKFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("K");
    WriteSidecarJson("missing_k.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_k.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "K");
}

TEST_F(GotchaFieldValidationTest, MissingDeltaFFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("deltaF");
    WriteSidecarJson("missing_deltaf.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_deltaf.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "deltaF");
}

TEST_F(GotchaFieldValidationTest, MissingMinFFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("minF");
    WriteSidecarJson("missing_minf.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_minf.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "minF");
}

TEST_F(GotchaFieldValidationTest, MissingAntXFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("AntX");
    WriteSidecarJson("missing_antx.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_antx.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "AntX");
}

TEST_F(GotchaFieldValidationTest, MissingAntYFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("AntY");
    WriteSidecarJson("missing_anty.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_anty.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "AntY");
}

TEST_F(GotchaFieldValidationTest, MissingAntZFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("AntZ");
    WriteSidecarJson("missing_antz.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_antz.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "AntZ");
}

TEST_F(GotchaFieldValidationTest, MissingR0FieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("R0");
    WriteSidecarJson("missing_r0.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_r0.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "R0");
}

TEST_F(GotchaFieldValidationTest, MissingPhdataFieldIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("phdata");
    WriteSidecarJson("missing_phdata.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_phdata.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "phdata");
}

TEST_F(GotchaFieldValidationTest, MultipleFieldsCanBeMissingSimultaneously) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("Np");
    sidecar.erase("K");
    sidecar.erase("deltaF");
    WriteSidecarJson("missing_multiple.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("missing_multiple.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 3);
}

TEST_F(GotchaFieldValidationTest, IncorrectNpTypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["Np"] = "not_a_number";
    WriteSidecarJson("wrong_np_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_np_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "Np");
}

TEST_F(GotchaFieldValidationTest, IncorrectKTypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["K"] = nullptr;
    WriteSidecarJson("wrong_k_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_k_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "K");
}

TEST_F(GotchaFieldValidationTest, IncorrectDeltaFTypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["deltaF"] = "1e6";
    WriteSidecarJson("wrong_deltaf_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_deltaf_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "deltaF");
}

TEST_F(GotchaFieldValidationTest, IncorrectMinFTypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["minF"] = true;
    WriteSidecarJson("wrong_minf_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_minf_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "minF");
}

TEST_F(GotchaFieldValidationTest, IncorrectAntXTypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["AntX"] = nlohmann::json::array();
    WriteSidecarJson("wrong_antx_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_antx_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "AntX");
}

TEST_F(GotchaFieldValidationTest, IncorrectR0TypeIsDetected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["R0"] = nlohmann::json::object({{"nested", "object"}});
    WriteSidecarJson("wrong_r0_type.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("wrong_r0_type.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "R0");
}

TEST_F(GotchaFieldValidationTest, PhdataCanBeArray) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["phdata"] = nlohmann::json::array({1.0, 2.0, 3.0});
    WriteSidecarJson("phdata_array.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("phdata_array.json"));

    EXPECT_TRUE(result.is_valid());
}

TEST_F(GotchaFieldValidationTest, PhdataCanBeObject) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["phdata"] = nlohmann::json::object({{"data", "encoded"}});
    WriteSidecarJson("phdata_object.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("phdata_object.json"));

    EXPECT_TRUE(result.is_valid());
}

TEST_F(GotchaFieldValidationTest, PhdataCanBeString) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["phdata"] = "base64_encoded_data";
    WriteSidecarJson("phdata_string.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("phdata_string.json"));

    EXPECT_TRUE(result.is_valid());
}

TEST_F(GotchaFieldValidationTest, PhdataNumberTypeIsRejected) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["phdata"] = 12345;
    WriteSidecarJson("phdata_number.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("phdata_number.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "phdata");
}

TEST_F(GotchaFieldValidationTest, MissingFileReturnsError) {
    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("nonexistent.json"));

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_EQ(result.missing_fields[0].field_name, "sidecar_json");
}

TEST_F(GotchaFieldValidationTest, InvalidJsonReturnsError) {
    std::ofstream stream{Path("invalid.json")};
    ASSERT_TRUE(stream);
    stream << "{ this is not valid json";

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("invalid.json"));

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_EQ(result.type_errors[0].field_name, "sidecar_json");
}

TEST_F(GotchaFieldValidationTest, ValidationErrorMessagesAreDescriptive) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar["Np"] = "not_a_number";
    WriteSidecarJson("descriptive_error.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("descriptive_error.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.type_errors.size(), 1);
    EXPECT_FALSE(result.type_errors[0].message.empty());
    EXPECT_NE(result.type_errors[0].message.find("incorrect type"), std::string::npos);
}

TEST_F(GotchaFieldValidationTest, ValidationErrorIncludesSourcePath) {
    auto sidecar = MakeValidGotchaSidecar();
    sidecar.erase("Np");
    WriteSidecarJson("with_path.json", sidecar);

    const auto result = graphx::sar::GotchaMatInspector::ValidateRequiredFields(Path("with_path.json"));

    EXPECT_FALSE(result.is_valid());
    EXPECT_EQ(result.missing_fields.size(), 1);
    EXPECT_TRUE(result.missing_fields[0].source_path.generic_string().find("with_path.json") != std::string::npos);
}

} // namespace
