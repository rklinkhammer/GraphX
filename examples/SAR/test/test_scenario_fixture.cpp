// SPDX-License-Identifier: MIT

/**
 * @file test_scenario_fixture.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH
#define SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH "examples/SAR/fixtures/scenario_001/fixture_manifest.json"
#endif

#ifndef SAR_SCENARIO_001_FIXTURE_PROVENANCE_PATH
#define SAR_SCENARIO_001_FIXTURE_PROVENANCE_PATH "examples/SAR/fixtures/scenario_001/provenance.md"
#endif

#ifndef SAR_SCENARIO_001_FIXTURE_CHECKSUMS_PATH
#define SAR_SCENARIO_001_FIXTURE_CHECKSUMS_PATH "examples/SAR/fixtures/scenario_001/checksums.txt"
#endif

#ifndef SAR_SCENARIO_001_FIXTURE_DATA_PATH
#define SAR_SCENARIO_001_FIXTURE_DATA_PATH "examples/SAR/fixtures/scenario_001/deterministic_iq_phase_history_fixture_v1.json"
#endif

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

    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

std::string ComputeFnv1a64(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "unable to open fixture data file: " << path;

    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;

    std::uint64_t hash = kOffsetBasis;
    char c = 0;
    while (input.get(c)) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kPrime;
    }

    std::ostringstream hex;
    hex << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return hex.str();
}

std::string ParseChecksumValue(const std::string& checksums_text,
                               const std::string& algorithm,
                               const std::string& file_name) {
    std::istringstream lines(checksums_text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream parts(line);
        std::string parsed_algorithm;
        std::string parsed_file;
        std::string parsed_value;
        parts >> parsed_algorithm >> parsed_file >> parsed_value;

        if (parsed_algorithm == algorithm && parsed_file == file_name) {
            return parsed_value;
        }
    }

    return {};
}

} // namespace

TEST(ScenarioFixtureTest, Scenario001FixtureArtifactsExist) {
    const auto manifest_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH};
    const auto provenance_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_PROVENANCE_PATH};
    const auto checksums_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_CHECKSUMS_PATH};
    const auto data_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    EXPECT_TRUE(std::filesystem::exists(manifest_path));
    EXPECT_TRUE(std::filesystem::exists(provenance_path));
    EXPECT_TRUE(std::filesystem::exists(checksums_path));
    EXPECT_TRUE(std::filesystem::exists(data_path));
}

TEST(ScenarioFixtureTest, FixtureManifestMatchesScenario001AndDefinesBoundedDimensions) {
    const auto manifest_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH};
    const auto data_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto manifest = LoadJson(manifest_path);
    const auto data = LoadJson(data_path);

    ASSERT_TRUE(manifest.contains("schema"));
    ASSERT_TRUE(manifest.contains("version"));
    EXPECT_EQ(manifest.at("schema").get<std::string>(), "graphx.sar.fixture_manifest.v1");
    EXPECT_FALSE(manifest.at("version").get<std::string>().empty());

    EXPECT_EQ(manifest.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(manifest.at("fixture_id").get<std::string>(), "deterministic_iq_phase_history_fixture_v1");

    EXPECT_TRUE(manifest.at("synthetic").get<bool>());
    EXPECT_TRUE(manifest.at("deterministic").get<bool>());
    EXPECT_FALSE(manifest.at("requires_external_packages").get<bool>());
    EXPECT_FALSE(manifest.at("requires_external_data").get<bool>());

    ASSERT_TRUE(manifest.contains("expected_dimensions"));
    const auto& dims = manifest.at("expected_dimensions");
    const auto pulse_count = dims.at("pulse_count").get<std::uint64_t>();
    const auto range_bin_count = dims.at("range_bin_count").get<std::uint64_t>();
    const auto sample_count = dims.at("complex_sample_count").get<std::uint64_t>();

    EXPECT_GT(pulse_count, 0u);
    EXPECT_GT(range_bin_count, 0u);
    EXPECT_GT(sample_count, 0u);
    EXPECT_LE(pulse_count, 1024u);
    EXPECT_LE(range_bin_count, 4096u);
    EXPECT_EQ(sample_count, pulse_count * range_bin_count);

    ASSERT_TRUE(data.contains("scenario_id"));
    ASSERT_TRUE(data.contains("schema"));
    EXPECT_EQ(data.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(data.at("schema").get<std::string>(), "graphx.sar.phase_history_fixture.v1");

    ASSERT_TRUE(data.contains("records"));
    ASSERT_TRUE(data.at("records").is_array());
    EXPECT_EQ(data.at("records").size(), pulse_count);
}

TEST(ScenarioFixtureTest, ChecksumIsStableAndMatchesManifestAndData) {
    const auto manifest_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_MANIFEST_PATH};
    const auto checksums_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_CHECKSUMS_PATH};
    const auto data_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto manifest = LoadJson(manifest_path);
    const auto checksums_text = ReadText(checksums_path);

    const auto listed_checksum = ParseChecksumValue(
        checksums_text,
        "fnv1a64",
        data_path.filename().string());
    ASSERT_FALSE(listed_checksum.empty());

    const auto computed = ComputeFnv1a64(data_path);
    EXPECT_EQ(listed_checksum, computed);

    ASSERT_TRUE(manifest.contains("checksum"));
    const auto& checksum = manifest.at("checksum");
    EXPECT_EQ(checksum.at("algorithm").get<std::string>(), "fnv1a64");
    EXPECT_EQ(checksum.at("data_file").get<std::string>(), data_path.filename().string());
    EXPECT_EQ(checksum.at("value").get<std::string>(), computed);
}

TEST(ScenarioFixtureTest, ProvenanceDeclaresSyntheticDeterministicAndNoExternalSources) {
    const auto provenance_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_PROVENANCE_PATH};
    const auto text = ReadText(provenance_path);

    EXPECT_NE(text.find("synthetic"), std::string::npos);
    EXPECT_NE(text.find("deterministic"), std::string::npos);
    EXPECT_NE(text.find("External dataset dependency: none"), std::string::npos);
    EXPECT_NE(text.find("External package dependency: none"), std::string::npos);
    EXPECT_NE(text.find("Network/download dependency: none"), std::string::npos);
    EXPECT_NE(text.find("not sourced from AFRL GOTCHA"), std::string::npos);
}
