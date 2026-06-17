// SPDX-License-Identifier: MIT

/**
 * @file test_cpu_reference_backprojection.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/SarScenario001CpuReference.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
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

std::size_t PeakIndex(const std::vector<float>& pixels) {
    if (pixels.empty()) {
        throw std::invalid_argument("pixels must be non-empty");
    }
    std::size_t peak = 0;
    for (std::size_t i = 1; i < pixels.size(); ++i) {
        if (pixels[i] > pixels[peak]) {
            peak = i;
        }
    }
    return peak;
}

} // namespace

TEST(CpuReferenceBackprojectionTest, LoadsScenario001FixtureAndProducesArtifactMetadata) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    ASSERT_TRUE(std::filesystem::exists(scenario_path));
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    const auto artifact = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);

    EXPECT_EQ(artifact.scenario_id, "scenario_001");
    EXPECT_EQ(artifact.fixture_id, "deterministic_iq_phase_history_fixture_v1");
    EXPECT_EQ(artifact.algorithm, "cpu_reference_backprojection");
    EXPECT_EQ(artifact.dtype, "float32");
    EXPECT_EQ(artifact.layout, "row_major");
    EXPECT_EQ(artifact.width, 16u);
    EXPECT_EQ(artifact.height, 16u);
    EXPECT_FALSE(artifact.checksum.empty());

    ASSERT_EQ(artifact.pixels.size(), static_cast<std::size_t>(artifact.width) * artifact.height);
}

TEST(CpuReferenceBackprojectionTest, OutputIsDeterministicFiniteAndNonZero) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto first = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);
    const auto second = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);

    EXPECT_EQ(first.width, second.width);
    EXPECT_EQ(first.height, second.height);
    EXPECT_EQ(first.checksum, second.checksum);
    ASSERT_EQ(first.pixels.size(), second.pixels.size());

    bool has_nonzero = false;
    for (std::size_t i = 0; i < first.pixels.size(); ++i) {
        EXPECT_FLOAT_EQ(first.pixels[i], second.pixels[i]);
        EXPECT_TRUE(std::isfinite(first.pixels[i]));
        has_nonzero = has_nonzero || (std::abs(first.pixels[i]) > 0.0f);
    }
    EXPECT_TRUE(has_nonzero);
}

TEST(CpuReferenceBackprojectionTest, PeakLocationIsStableForDeterministicFixture) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto first = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);
    const auto second = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);

    const auto peak_first = PeakIndex(first.pixels);
    const auto peak_second = PeakIndex(second.pixels);
    EXPECT_EQ(peak_first, peak_second);

    EXPECT_GT(first.pixels[peak_first], 0.0f);
}

TEST(CpuReferenceBackprojectionTest, WritesNormalizedReferenceArtifactContract) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto artifact = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);

    const auto out_dir = std::filesystem::temp_directory_path() / "graphx_reference_artifact";
    {
        std::error_code ec;
        std::filesystem::remove_all(out_dir, ec);
    }

    const auto paths = sar::reference::scenario001::WriteArtifact(artifact, out_dir, "scenario_001_cpu_reference");
    ASSERT_TRUE(std::filesystem::exists(paths.raster_path));
    ASSERT_TRUE(std::filesystem::exists(paths.contract_path));

    const auto contract = LoadJson(paths.contract_path);
    EXPECT_EQ(contract.at("source_tool").get<std::string>(), "cpu-reference-backprojection");
    EXPECT_EQ(contract.at("provenance_class").get<std::string>(), "deterministic_internal_reference");
    EXPECT_EQ(contract.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(contract.at("fixture_id").get<std::string>(), "deterministic_iq_phase_history_fixture_v1");
    EXPECT_EQ(contract.at("algorithm").get<std::string>(), "cpu_reference_backprojection");
    EXPECT_EQ(contract.at("format").get<std::string>(), "float32_raster");
    EXPECT_EQ(contract.at("layout").get<std::string>(), "row_major");
    EXPECT_EQ(contract.at("dtype").get<std::string>(), "float32");
    EXPECT_EQ(contract.at("width").get<std::uint32_t>(), artifact.width);
    EXPECT_EQ(contract.at("height").get<std::uint32_t>(), artifact.height);
    EXPECT_EQ(contract.at("deterministic_hash").get<std::string>(), artifact.checksum);
}

TEST(CpuReferenceBackprojectionTest, MissingRequiredFixtureFieldIsReported) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    auto broken = LoadJson(fixture_path);
    broken["geometry"].erase("carrier_hz");

    const auto broken_path = std::filesystem::temp_directory_path() / "graphx_broken_fixture.json";
    {
        std::ofstream out(broken_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << broken.dump(2) << '\n';
    }

    try {
        (void)sar::reference::scenario001::RunCpuReferenceBackprojection(scenario_path, broken_path);
        FAIL() << "expected missing field error";
    } catch (const std::invalid_argument& ex) {
        EXPECT_NE(std::string(ex.what()).find("fixture.geometry.carrier_hz"), std::string::npos);
    }
}
