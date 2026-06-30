// SPDX-License-Identifier: MIT

/**
 * @file test_sarpy_metadata_harness.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SARPY_METADATA_HARNESS_PATH
#define SARPY_METADATA_HARNESS_PATH "examples/SAR/tools/sarpy_metadata_harness.py"
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

TEST(SarpyMetadataHarnessTest, HarnessEntryPointExistsAndProbeIsLocalOnlyFriendly) {
    const auto harness_path = std::filesystem::path{SARPY_METADATA_HARNESS_PATH};
    ASSERT_TRUE(std::filesystem::exists(harness_path));

    const auto output_path = std::filesystem::temp_directory_path() / "graphx_sarpy_metadata_probe.json";
    std::error_code ec;
    std::filesystem::remove(output_path, ec);

    const std::string command =
        "python3 " + Quote(harness_path) +
        " probe-environment --output-json " + Quote(output_path) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_path));

    const auto probe = LoadJson(output_path);
    EXPECT_EQ(probe.at("tool").get<std::string>(), "SarPy");
    EXPECT_TRUE(probe.at("local_only").get<bool>());
    EXPECT_FALSE(probe.at("ci_safe").get<bool>());
    EXPECT_TRUE(probe.at("requires_manual_dataset_path").get<bool>());
    EXPECT_EQ(probe.at("comparison_scope").get<std::string>(), "product_metadata_validation_only");
}

TEST(SarpyMetadataHarnessTest, MetadataNormalizationProducesGraphxCompatibleContract) {
    const auto harness_path = std::filesystem::path{SARPY_METADATA_HARNESS_PATH};
    ASSERT_TRUE(std::filesystem::exists(harness_path));

    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_sarpy_metadata_normalize";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto input_json = temp_dir / "sarpy_metadata.json";
    {
        std::ofstream out(input_json);
        ASSERT_TRUE(out.good());
        out << nlohmann::json{
            {"source_tool", "SarPy"},
            {"product_type", "SICD"},
            {"source_product_path", "/local/manual/sample.sicd"},
            {"metadata_fields", nlohmann::json{
                {"collector_name", "DemoCollector"},
                {"rows", 2048},
                {"cols", 4096},
                {"pixel_type", "RE32F_IM32F"}
            }}
        }.dump(2) << '\n';
    }

    const auto output_json = temp_dir / "normalized_contract.json";
    const std::string command =
        "python3 " + Quote(harness_path) +
        " normalize-metadata --scenario " + Quote(std::filesystem::path{SAR_SCENARIO_001_JSON_PATH}) +
        " --input-json " + Quote(input_json) +
        " --output-json " + Quote(output_json) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(output_json));

    const auto contract = LoadJson(output_json);
    EXPECT_EQ(contract.at("schema").get<std::string>(), "graphx.sar.product_metadata_contract.v1");
    EXPECT_EQ(contract.at("source_tool").get<std::string>(), "SarPy");
    EXPECT_EQ(contract.at("provenance_class").get<std::string>(), "external_baseline");
    EXPECT_EQ(contract.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(contract.at("artifact_kind").get<std::string>(), "product_metadata_summary");
    EXPECT_EQ(contract.at("comparison_scope").get<std::string>(), "product_metadata_validation_only");
    EXPECT_TRUE(contract.at("local_only").get<bool>());
    EXPECT_FALSE(contract.at("ci_safe").get<bool>());
    ASSERT_TRUE(contract.at("metadata_fields").is_object());
}
