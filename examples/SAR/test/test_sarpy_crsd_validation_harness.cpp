#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SARPY_VALIDATE_CRSD_TOOL_PATH
#define SARPY_VALIDATE_CRSD_TOOL_PATH "tools/sarpy/validate_crsd.py"
#endif

#ifndef SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH
#define SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH "tools/sarpy/reference_image_from_crsd.py"
#endif

#ifndef SARPY_REQUIREMENTS_PATH
#define SARPY_REQUIREMENTS_PATH "tools/sarpy/requirements.txt"
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

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(SarpyCrsdValidationHarnessTest, RequiredFilesExistAndRequirementsContainSarpy) {
    const auto validate_tool = std::filesystem::path{SARPY_VALIDATE_CRSD_TOOL_PATH};
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH};
    const auto requirements = std::filesystem::path{SARPY_REQUIREMENTS_PATH};

    ASSERT_TRUE(std::filesystem::exists(validate_tool));
    ASSERT_TRUE(std::filesystem::exists(reference_tool));
    ASSERT_TRUE(std::filesystem::exists(requirements));

    const std::string requirements_text = ReadText(requirements);
    EXPECT_NE(requirements_text.find("sarpy"), std::string::npos);
}

TEST(SarpyCrsdValidationHarnessTest, ProbeCommandsDeclareLocalOnlyHarness) {
    const auto validate_tool = std::filesystem::path{SARPY_VALIDATE_CRSD_TOOL_PATH};
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH};

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_sarpy_crsd_probe";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto validate_probe_json = temp_dir / "validate_probe.json";
    const auto reference_probe_json = temp_dir / "reference_probe.json";

    const std::string validate_command =
        "python3 " + Quote(validate_tool) +
        " probe-environment --output-json " + Quote(validate_probe_json) +
        " > /dev/null";
    const std::string reference_command =
        "python3 " + Quote(reference_tool) +
        " probe-environment --output-json " + Quote(reference_probe_json) +
        " > /dev/null";

    ASSERT_EQ(std::system(validate_command.c_str()), 0);
    ASSERT_EQ(std::system(reference_command.c_str()), 0);

    const auto validate_probe = LoadJson(validate_probe_json);
    const auto reference_probe = LoadJson(reference_probe_json);

    EXPECT_TRUE(validate_probe.at("local_only").get<bool>());
    EXPECT_FALSE(validate_probe.at("ci_safe").get<bool>());
    EXPECT_TRUE(reference_probe.at("local_only").get<bool>());
    EXPECT_FALSE(reference_probe.at("ci_safe").get<bool>());
}

TEST(SarpyCrsdValidationHarnessTest, OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable) {
    const auto validate_tool = std::filesystem::path{SARPY_VALIDATE_CRSD_TOOL_PATH};
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_CRSD_TOOL_PATH};

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_sarpy_crsd_smoke";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto validate_probe_json = temp_dir / "validate_probe.json";
    const std::string probe_command =
        "python3 " + Quote(validate_tool) +
        " probe-environment --output-json " + Quote(validate_probe_json) +
        " > /dev/null";
    ASSERT_EQ(std::system(probe_command.c_str()), 0);

    const auto probe = LoadJson(validate_probe_json);
    const bool has_sarpy = probe.at("packages").at("sarpy").at("installed").get<bool>();
    if (!has_sarpy) {
        GTEST_SKIP() << "SarPy not installed in local environment";
    }

    const char* crsd_path_env = std::getenv("GRAPHX_SARPY_CRSD_FILE");
    if (crsd_path_env == nullptr || std::string(crsd_path_env).empty()) {
        GTEST_SKIP() << "GRAPHX_SARPY_CRSD_FILE is not set";
    }

    const auto crsd_path = std::filesystem::path{crsd_path_env};
    if (!std::filesystem::exists(crsd_path)) {
        GTEST_SKIP() << "GRAPHX_SARPY_CRSD_FILE path does not exist";
    }

    const auto validate_report = temp_dir / "validate_report.json";
    const std::string validate_command =
        "python3 " + Quote(validate_tool) +
        " validate --input-crsd " + Quote(crsd_path) +
        " --output-json " + Quote(validate_report) +
        " > /dev/null";
    ASSERT_EQ(std::system(validate_command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(validate_report));

    const auto validate_json = LoadJson(validate_report);
    ASSERT_TRUE(validate_json.contains("crsd_version"));
    ASSERT_TRUE(validate_json.contains("dimensions"));
    ASSERT_TRUE(validate_json.contains("dtype"));
    ASSERT_TRUE(validate_json.contains("sample_slices"));
    ASSERT_TRUE(validate_json.contains("pvp_arrays"));
    ASSERT_TRUE(validate_json.contains("validation"));

    const auto magnitude_png = temp_dir / "reference_magnitude.png";
    const auto metadata_json = temp_dir / "reference_metadata.json";
    const std::string reference_command =
        "python3 " + Quote(reference_tool) +
        " generate-reference --input-crsd " + Quote(crsd_path) +
        " --output-magnitude-png " + Quote(magnitude_png) +
        " --output-metadata-json " + Quote(metadata_json) +
        " > /dev/null";
    ASSERT_EQ(std::system(reference_command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(magnitude_png));
    ASSERT_TRUE(std::filesystem::exists(metadata_json));
}
