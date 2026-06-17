// SPDX-License-Identifier: MIT

/**
 * @file test_scenario_manifest.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_SCENARIO_001_MD_PATH
#define SAR_SCENARIO_001_MD_PATH "examples/SAR/scenarios/scenario_001.md"
#endif

struct ValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
};

ValidationResult ValidateScenarioManifest(const nlohmann::json& manifest) {
    ValidationResult result;

    const auto fail = [&result](const std::string& error) {
        result.valid = false;
        result.errors.push_back(error);
    };

    if (!manifest.is_object()) {
        fail("manifest must be a JSON object");
        return result;
    }

    const std::vector<std::string> required_fields = {
        "schema",
        "version",
        "scenario_id",
        "comparison_level",
        "dataset",
        "fixture",
        "pulse_range",
        "range_bins",
        "image_grid",
        "scene_center",
        "algorithm",
        "window",
        "range_compression",
        "output",
        "output_artifact_contract",
        "comparator_profile",
        "immutability_rule",
    };

    for (const auto& field : required_fields) {
        if (!manifest.contains(field)) {
            fail("missing required field: " + field);
        }
    }

    if (!result.valid) {
        return result;
    }

    if (!manifest.at("version").is_string() ||
        manifest.at("version").get<std::string>() != "graphx.sar.scenario.v1") {
        fail("unsupported scenario manifest version");
    }

    if (!manifest.at("schema").is_string() || manifest.at("schema").get<std::string>().empty()) {
        fail("schema must be a non-empty string");
    }

    if (!manifest.at("scenario_id").is_string() ||
        manifest.at("scenario_id").get<std::string>() != "scenario_001") {
        fail("scenario_id must be deterministic and equal to scenario_001");
    }

    if (!manifest.at("comparison_level").is_string() ||
        manifest.at("comparison_level").get<std::string>() !=
            "internal_image_formation_correctness") {
        fail("comparison_level must be internal_image_formation_correctness");
    }

    const auto require_object = [&fail, &manifest](const char* field_name) {
        if (!manifest.at(field_name).is_object()) {
            fail(std::string(field_name) + " must be an object");
        }
    };

    require_object("dataset");
    require_object("fixture");
    require_object("pulse_range");
    require_object("range_bins");
    require_object("image_grid");
    require_object("scene_center");
    require_object("algorithm");
    require_object("window");
    require_object("range_compression");
    require_object("output");
    require_object("output_artifact_contract");
    require_object("comparator_profile");
    require_object("immutability_rule");

    if (!result.valid) {
        return result;
    }

    const auto require_member = [&fail](const nlohmann::json& object,
                                        const char* object_name,
                                        const char* member_name) {
        if (!object.contains(member_name)) {
            fail(std::string(object_name) + "." + member_name + " is required");
        }
    };

    const auto& dataset = manifest.at("dataset");
    require_member(dataset, "dataset", "name");
    require_member(dataset, "dataset", "subset");
    require_member(dataset, "dataset", "source_kind");
    require_member(dataset, "dataset", "normalization_schema");

    const auto& pulse_range = manifest.at("pulse_range");
    require_member(pulse_range, "pulse_range", "start");
    require_member(pulse_range, "pulse_range", "count");

    const auto& fixture = manifest.at("fixture");
    require_member(fixture, "fixture", "fixture_id");
    require_member(fixture, "fixture", "kind");

    const auto& range_bins = manifest.at("range_bins");
    require_member(range_bins, "range_bins", "start");
    require_member(range_bins, "range_bins", "count");

    const auto& image_grid = manifest.at("image_grid");
    require_member(image_grid, "image_grid", "width");
    require_member(image_grid, "image_grid", "height");
    require_member(image_grid, "image_grid", "pixel_spacing_m");

    const auto& scene_center = manifest.at("scene_center");
    require_member(scene_center, "scene_center", "x_m");
    require_member(scene_center, "scene_center", "y_m");
    require_member(scene_center, "scene_center", "z_m");
    require_member(scene_center, "scene_center", "frame");

    const auto& algorithm = manifest.at("algorithm");
    require_member(algorithm, "algorithm", "name");
    require_member(algorithm, "algorithm", "type");
    require_member(algorithm, "algorithm", "reference");
    require_member(algorithm, "algorithm", "graphx");
    if (algorithm.contains("type") &&
        algorithm.at("type").is_string() &&
        algorithm.at("type").get<std::string>() != "backprojection") {
        fail("algorithm.type must be backprojection");
    }

    const auto& window = manifest.at("window");
    require_member(window, "window", "name");
    require_member(window, "window", "enabled");

    const auto& range_compression = manifest.at("range_compression");
    require_member(range_compression, "range_compression", "mode");
    require_member(range_compression, "range_compression", "enabled");

    const auto& output = manifest.at("output");
    require_member(output, "output", "format");
    require_member(output, "output", "dtype");
    require_member(output, "output", "layout");
    require_member(output, "output", "artifact_kind");

    const auto& output_contract = manifest.at("output_artifact_contract");
    require_member(output_contract, "output_artifact_contract", "format");
    require_member(output_contract, "output_artifact_contract", "dtype");
    require_member(output_contract, "output_artifact_contract", "layout");
    require_member(output_contract, "output_artifact_contract", "dimensions");

    if (output_contract.contains("dimensions") && output_contract.at("dimensions").is_object()) {
        require_member(output_contract.at("dimensions"), "output_artifact_contract.dimensions", "width");
        require_member(output_contract.at("dimensions"), "output_artifact_contract.dimensions", "height");
    }

    const auto& comparator_profile = manifest.at("comparator_profile");
    require_member(comparator_profile, "comparator_profile", "mode");
    require_member(comparator_profile, "comparator_profile", "strict");

    const auto& immutability_rule = manifest.at("immutability_rule");
    require_member(immutability_rule, "immutability_rule", "changes_require");
    if (immutability_rule.contains("changes_require") &&
        immutability_rule.at("changes_require").is_string() &&
        immutability_rule.at("changes_require").get<std::string>() != "scenario_002") {
        fail("immutability_rule.changes_require must be scenario_002");
    }

    return result;
}

nlohmann::json LoadScenarioJson() {
    std::ifstream input(SAR_SCENARIO_001_JSON_PATH);
    EXPECT_TRUE(input.good()) << "unable to open scenario manifest: " << SAR_SCENARIO_001_JSON_PATH;

    nlohmann::json manifest;
    input >> manifest;
    return manifest;
}

std::string LoadScenarioMarkdown() {
    std::ifstream input(SAR_SCENARIO_001_MD_PATH);
    EXPECT_TRUE(input.good()) << "unable to open scenario doc: " << SAR_SCENARIO_001_MD_PATH;

    std::string text;
    std::string line;
    while (std::getline(input, line)) {
        text.append(line);
        text.push_back('\n');
    }
    return text;
}

} // namespace

TEST(ScenarioManifestTest, Scenario001DefinesRequiredFields) {
    const auto manifest = LoadScenarioJson();
    const auto validation = ValidateScenarioManifest(manifest);

    ASSERT_TRUE(validation.valid) << ::testing::PrintToString(validation.errors);
    EXPECT_EQ(manifest.at("version").get<std::string>(), "graphx.sar.scenario.v1");
    EXPECT_EQ(manifest.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(
        manifest.at("comparison_level").get<std::string>(),
        "internal_image_formation_correctness");
}

TEST(ScenarioManifestTest, Scenario001MarkdownDocumentsRequiredItems) {
    const auto text = LoadScenarioMarkdown();

    EXPECT_NE(text.find("Purpose"), std::string::npos);
    EXPECT_NE(text.find("Comparison Level"), std::string::npos);
    EXPECT_NE(text.find("Fixture Description"), std::string::npos);
    EXPECT_NE(text.find("Expected Future Flow"), std::string::npos);
    EXPECT_NE(text.find("Dataset"), std::string::npos);
    EXPECT_NE(text.find("Pulse Range"), std::string::npos);
    EXPECT_NE(text.find("Range Bins"), std::string::npos);
    EXPECT_NE(text.find("Output Format"), std::string::npos);
    EXPECT_NE(text.find("Output Artifact Contract"), std::string::npos);
    EXPECT_NE(text.find("Algorithm"), std::string::npos);
    EXPECT_NE(text.find("Immutability Rule"), std::string::npos);
    EXPECT_NE(text.find("scenario_002"), std::string::npos);
    EXPECT_NE(text.find("does not"), std::string::npos);
}

TEST(ScenarioManifestTest, RejectsUnsupportedVersionAndIncompleteManifest) {
    nlohmann::json unsupported = {
        {"schema", "graphx.sar.compare_scenario.v1"},
        {"version", "graphx.sar.scenario.v0"},
        {"scenario_id", "scenario_001"},
        {"comparison_level", "internal_image_formation_correctness"},
        {"dataset", nlohmann::json::object()},
        {"fixture", nlohmann::json::object()},
        {"pulse_range", nlohmann::json::object()},
        {"range_bins", nlohmann::json::object()},
        {"image_grid", nlohmann::json::object()},
        {"scene_center", nlohmann::json::object()},
        {"algorithm", nlohmann::json::object()},
        {"window", nlohmann::json::object()},
        {"range_compression", nlohmann::json::object()},
        {"output", nlohmann::json::object()},
        {"output_artifact_contract", nlohmann::json::object()},
        {"comparator_profile", nlohmann::json::object()},
        {"immutability_rule", nlohmann::json::object()},
    };
    const auto unsupported_validation = ValidateScenarioManifest(unsupported);
    EXPECT_FALSE(unsupported_validation.valid);
    EXPECT_NE(::testing::PrintToString(unsupported_validation.errors)
                  .find("unsupported scenario manifest version"),
              std::string::npos);

    nlohmann::json incomplete = {
        {"version", "graphx.sar.scenario.v1"},
        {"dataset", {{"name", "AFRL GOTCHA Challenge Problem"}}},
    };
    const auto incomplete_validation = ValidateScenarioManifest(incomplete);
    EXPECT_FALSE(incomplete_validation.valid);
    EXPECT_NE(::testing::PrintToString(incomplete_validation.errors)
                  .find("missing required field: pulse_range"),
              std::string::npos);
}