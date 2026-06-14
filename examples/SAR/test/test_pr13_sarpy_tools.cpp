#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH
#define SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH "tools/sarpy/reference_image_from_gotcha.py"
#endif

#ifndef SARPY_COMPARE_IMAGES_TOOL_PATH
#define SARPY_COMPARE_IMAGES_TOOL_PATH "tools/sarpy/compare_images.py"
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

TEST(Pr13SarpyToolsTest, RequiredFilesExistAndRequirementsDeclareExpectedPackages) {
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH};
    const auto compare_tool = std::filesystem::path{SARPY_COMPARE_IMAGES_TOOL_PATH};
    const auto requirements = std::filesystem::path{SARPY_REQUIREMENTS_PATH};

    ASSERT_TRUE(std::filesystem::exists(reference_tool));
    ASSERT_TRUE(std::filesystem::exists(compare_tool));
    ASSERT_TRUE(std::filesystem::exists(requirements));

    const std::string requirements_text = ReadText(requirements);
    EXPECT_NE(requirements_text.find("numpy"), std::string::npos);
    EXPECT_NE(requirements_text.find("scipy"), std::string::npos);
    EXPECT_NE(requirements_text.find("h5py"), std::string::npos);
    EXPECT_NE(requirements_text.find("matplotlib"), std::string::npos);
    EXPECT_NE(requirements_text.find("sarpy"), std::string::npos);
}

TEST(Pr13SarpyToolsTest, ProbeCommandsAreLocalOnlyAndNonBlocking) {
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH};
    const auto compare_tool = std::filesystem::path{SARPY_COMPARE_IMAGES_TOOL_PATH};

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_pr13_sarpy_probe";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto reference_probe_json = temp_dir / "reference_probe.json";
    const auto compare_probe_json = temp_dir / "compare_probe.json";

    const std::string reference_command =
        "python3 " + Quote(reference_tool) +
        " probe-environment --output-json " + Quote(reference_probe_json) +
        " > /dev/null";
    const std::string compare_command =
        "python3 " + Quote(compare_tool) +
        " probe-environment --output-json " + Quote(compare_probe_json) +
        " > /dev/null";

    ASSERT_EQ(std::system(reference_command.c_str()), 0);
    ASSERT_EQ(std::system(compare_command.c_str()), 0);

    const auto reference_probe = LoadJson(reference_probe_json);
    const auto compare_probe = LoadJson(compare_probe_json);

    EXPECT_TRUE(reference_probe.at("local_only").get<bool>());
    EXPECT_FALSE(reference_probe.at("ci_safe").get<bool>());
    EXPECT_TRUE(compare_probe.at("local_only").get<bool>());
    EXPECT_FALSE(compare_probe.at("ci_safe").get<bool>());
}

TEST(Pr13SarpyToolsTest, GeneratesReferenceAndDeterministicComparisonMetrics) {
    const auto reference_tool = std::filesystem::path{SARPY_REFERENCE_IMAGE_FROM_GOTCHA_TOOL_PATH};
    const auto compare_tool = std::filesystem::path{SARPY_COMPARE_IMAGES_TOOL_PATH};

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_pr13_sarpy_metrics";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto probe_json = temp_dir / "reference_probe.json";
    const std::string probe_command =
        "python3 " + Quote(reference_tool) +
        " probe-environment --output-json " + Quote(probe_json) +
        " > /dev/null";
    ASSERT_EQ(std::system(probe_command.c_str()), 0);

    const auto probe = LoadJson(probe_json);
    const bool has_numpy = probe.at("packages").at("numpy").at("installed").get<bool>();
    const bool has_matplotlib = probe.at("packages").at("matplotlib").at("installed").get<bool>();
    if (!has_numpy || !has_matplotlib) {
        GTEST_SKIP() << "numpy/matplotlib not installed in local environment";
    }

    const auto input_json = temp_dir / "complex_fixture.json";
    {
        std::ofstream out(input_json);
        ASSERT_TRUE(out.good());
        out << nlohmann::json{
            {"real", nlohmann::json::array({
                nlohmann::json::array({1.0, 0.0, 0.0, 0.0}),
                nlohmann::json::array({0.0, 1.0, 0.0, 0.0}),
                nlohmann::json::array({0.0, 0.0, 1.0, 0.0}),
                nlohmann::json::array({0.0, 0.0, 0.0, 1.0}),
            })},
            {"imag", nlohmann::json::array({
                nlohmann::json::array({0.0, 0.1, 0.2, 0.3}),
                nlohmann::json::array({0.4, 0.0, 0.5, 0.6}),
                nlohmann::json::array({0.7, 0.8, 0.0, 0.9}),
                nlohmann::json::array({1.0, 1.1, 1.2, 0.0}),
            })}
        }.dump(2) << '\n';
    }

    const auto reference_npy = temp_dir / "reference_image.npy";
    const auto magnitude_png = temp_dir / "reference_magnitude.png";
    const auto metadata_json = temp_dir / "reference_metadata.json";

    const std::string generate_command =
        "python3 " + Quote(reference_tool) +
        " generate-reference --input-json " + Quote(input_json) +
        " --output-reference-npy " + Quote(reference_npy) +
        " --output-magnitude-png " + Quote(magnitude_png) +
        " --output-metadata-json " + Quote(metadata_json) +
        " > /dev/null";
    ASSERT_EQ(std::system(generate_command.c_str()), 0);

    ASSERT_TRUE(std::filesystem::exists(reference_npy));
    ASSERT_TRUE(std::filesystem::exists(magnitude_png));
    ASSERT_TRUE(std::filesystem::exists(metadata_json));

    const auto report_json = temp_dir / "comparison_report.json";
    const auto diff_png = temp_dir / "difference_magnitude.png";
    const auto phase_png = temp_dir / "phase_difference.png";

    const std::string compare_command =
        "python3 " + Quote(compare_tool) +
        " compare --reference-npy " + Quote(reference_npy) +
        " --candidate-npy " + Quote(reference_npy) +
        " --output-report-json " + Quote(report_json) +
        " --output-diff-magnitude-png " + Quote(diff_png) +
        " --output-phase-difference-png " + Quote(phase_png) +
        " > /dev/null";
    ASSERT_EQ(std::system(compare_command.c_str()), 0);

    ASSERT_TRUE(std::filesystem::exists(report_json));
    ASSERT_TRUE(std::filesystem::exists(diff_png));
    ASSERT_TRUE(std::filesystem::exists(phase_png));

    const auto report = LoadJson(report_json);
    const auto metrics = report.at("metrics");
    EXPECT_NEAR(metrics.at("rmse_magnitude").get<double>(), 0.0, 1e-9);
    EXPECT_NEAR(metrics.at("phase_rmse_radians").get<double>(), 0.0, 1e-9);
    EXPECT_NEAR(metrics.at("peak_error_magnitude").get<double>(), 0.0, 1e-9);
    EXPECT_NEAR(metrics.at("magnitude_correlation").get<double>(), 1.0, 1e-9);
}
