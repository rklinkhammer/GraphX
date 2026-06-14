#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_GOTCHA_BACK_ADAPTER_PATH
#define SAR_GOTCHA_BACK_ADAPTER_PATH "examples/SAR/tools/gotcha_back_adapter.py"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
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

std::string LoadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

void WriteFloat32Raster(const std::filesystem::path& path, std::size_t count) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write raw raster: " << path;

    for (std::size_t index = 0; index < count; ++index) {
        const float value = static_cast<float>(index) / static_cast<float>(count + 1u);
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

} // namespace

TEST(Rrp3GotchaBackAdapterTest, Scenario001ProducesPinnedInvocationSpec) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto reference_dir = std::filesystem::temp_directory_path() / "graphx_rrp3_reference_scaffold";
    std::error_code remove_error;
    std::filesystem::remove_all(reference_dir, remove_error);

    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_GOTCHA_BACK_ADAPTER_PATH}) +
        " scaffold-reference --scenario " + Quote(scenario_path) +
        " --reference-dir " + Quote(reference_dir) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const auto invocation_path = reference_dir / "gotcha_back_invocation.json";
    const auto contract_path = reference_dir / "reference_output_contract.json";
    const auto script_path = reference_dir / "run_gotcha_back.sh";
    ASSERT_TRUE(std::filesystem::exists(invocation_path));
    ASSERT_TRUE(std::filesystem::exists(contract_path));
    ASSERT_TRUE(std::filesystem::exists(script_path));

    const auto invocation = LoadJson(invocation_path);
    EXPECT_EQ(invocation.at("tool").get<std::string>(), "gotcha-back");
    EXPECT_EQ(invocation.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(invocation.at("pinned_profile").at("pass").get<int>(), 1);
    EXPECT_EQ(invocation.at("pinned_profile").at("first_az").get<int>(), 38);
    EXPECT_EQ(invocation.at("pinned_profile").at("last_az").get<int>(), 41);

    const auto args = invocation.at("command");
    ASSERT_TRUE(args.is_array());
    EXPECT_NE(args.dump().find("--pass"), std::string::npos);
    EXPECT_NE(args.dump().find("--first-az"), std::string::npos);
    EXPECT_NE(args.dump().find("--last-az"), std::string::npos);
    EXPECT_NE(args.dump().find("--output-file"), std::string::npos);

    const auto script = LoadText(script_path);
    EXPECT_NE(script.find("${GOTCHA_BACK_BIN:-./build-release/sarbp} \\\n  --pass \\\n  1"), std::string::npos);
    EXPECT_NE(script.find("--first-az \\\n  38"), std::string::npos);
    EXPECT_NE(script.find("--last-az \\\n  41"), std::string::npos);
    EXPECT_NE(script.find("--output-file \\\n  "), std::string::npos);
    EXPECT_NE(script.find("${GOTCHA_DIR}"), std::string::npos);
    EXPECT_NE(script.find("# Invocation spec: "), std::string::npos);
    EXPECT_EQ(script.find("\n+  --pass"), std::string::npos);

    const auto contract = LoadJson(contract_path);
    EXPECT_EQ(contract.at("source_tool").get<std::string>(), "gotcha-back");
    EXPECT_EQ(contract.at("provenance_class").get<std::string>(), "external_baseline");
    EXPECT_EQ(contract.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(contract.at("format").get<std::string>(), "float32_raster");
    EXPECT_EQ(contract.at("layout").get<std::string>(), "row_major");
    EXPECT_EQ(contract.at("artifact_kind").get<std::string>(), "materialized_image");
    EXPECT_EQ(contract.at("dtype").get<std::string>(), "float32");
    EXPECT_EQ(contract.at("width").get<int>(), 16);
    EXPECT_EQ(contract.at("height").get<int>(), 16);
}

TEST(Rrp3GotchaBackAdapterTest, NormalizesRawFloat32OutputArtifactForScenario001) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    ASSERT_TRUE(std::filesystem::exists(scenario_path));

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp3_normalize_output";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto raw_path = temp_dir / "image.bin";
    WriteFloat32Raster(raw_path, 16u * 16u);

    const auto normalized_path = temp_dir / "normalized_reference.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_GOTCHA_BACK_ADAPTER_PATH}) +
        " normalize-output --scenario " + Quote(scenario_path) +
        " --input-raw " + Quote(raw_path) +
        " --output-json " + Quote(normalized_path) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(normalized_path));

    const auto normalized = LoadJson(normalized_path);
    EXPECT_EQ(normalized.at("source_tool").get<std::string>(), "gotcha-back");
    EXPECT_EQ(normalized.at("provenance_class").get<std::string>(), "external_baseline");
    EXPECT_EQ(normalized.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(normalized.at("format").get<std::string>(), "float32_raster");
    EXPECT_EQ(normalized.at("layout").get<std::string>(), "row_major");
    EXPECT_EQ(normalized.at("dtype").get<std::string>(), "float32");
    EXPECT_EQ(normalized.at("width").get<int>(), 16);
    EXPECT_EQ(normalized.at("height").get<int>(), 16);
    EXPECT_EQ(normalized.at("byte_count").get<int>(), 16 * 16 * 4);
    EXPECT_EQ(
        std::filesystem::weakly_canonical(
            std::filesystem::path{normalized.at("raw_path").get<std::string>()}).string(),
        std::filesystem::weakly_canonical(raw_path).string());
}