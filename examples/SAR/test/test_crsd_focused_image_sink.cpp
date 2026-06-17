// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_focused_image_sink.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/CrsdFocusedImageSinkNode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SAR_FOCUSED_IMAGE_SCHEMA_PATH
#define SAR_FOCUSED_IMAGE_SCHEMA_PATH "examples/SAR/tools/sar_focused_image_artifact_schema.json"
#endif

namespace {

std::uint64_t HashBytes(const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto b : bytes) {
        hash ^= static_cast<std::uint8_t>(b);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::byte> ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return {};
    }

    std::vector<char> buffer(static_cast<std::size_t>(size));
    in.read(buffer.data(), size);
    if (!in.good() && !in.eof()) {
        return {};
    }

    std::vector<std::byte> bytes(buffer.size());
    std::transform(buffer.begin(), buffer.end(), bytes.begin(), [](char c) {
        return static_cast<std::byte>(static_cast<unsigned char>(c));
    });
    return bytes;
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

sar::FocusedImageResult MakeFocusedResult() {
    sar::FocusedImageResult result{};
    result.control.sidecar.sequence_id = 42u;
    result.control.sidecar.tile_id = 2u;
    result.control.sidecar.backend = sar::SarBackendKind::Host;
    result.grid.width = 4u;
    result.grid.height = 2u;
    result.grid.pixel_spacing_m = 0.3;
    result.grid.scene_center_x_m = 0.0;
    result.grid.scene_center_y_m = 0.0;
    result.grid.range_origin_m = 0.0;
    result.grid.range_spacing_m = 0.75;
    result.grid.wavelength_m = 0.03;
    result.grid.platform_x_start_m = -5.0;
    result.grid.platform_x_end_m = 5.0;
    result.grid.platform_y_m = -3.0;
    result.pixels = {0.0f, 1.0f, 0.5f, 0.25f, 0.75f, 0.1f, 0.9f, 0.6f};
    result.output_hash = 123456u;
    result.input_ordered_set_hash = 567890u;
    result.total_pulses = 6u;
    result.samples_per_pulse = 8u;
    result.ordered_crsd_segment_indices = {0u, 1u, 2u};
    result.per_segment_input_hashes = {11u, 22u, 33u};
    result.lineage_complete_aperture = true;
    return result;
}

} // namespace

TEST(CrsdFocusedImageSinkNodeTest, WritesDeterministicBinaryJsonAndPgmArtifacts) {
    const auto out_dir = std::filesystem::temp_directory_path() / "graphx_focused_sink_det";
    std::filesystem::remove_all(out_dir);

    sar::CrsdFocusedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{
        {"enabled", true},
        {"output_dir", out_dir.string()},
        {"artifact_stem", "focused"},
        {"convenience_image_format", "pgm"},
    }));

    const auto input = MakeFocusedResult();

    auto first = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());

    const auto bin_path = out_dir / "focused_seq42_tile2.bin";
    const auto json_path = out_dir / "focused_seq42_tile2.json";
    const auto pgm_path = out_dir / "focused_seq42_tile2.pgm";
    ASSERT_TRUE(std::filesystem::exists(bin_path));
    ASSERT_TRUE(std::filesystem::exists(json_path));
    ASSERT_TRUE(std::filesystem::exists(pgm_path));

    const auto first_bin = ReadBytes(bin_path);
    const auto first_json = ReadText(json_path);
    const auto first_pgm = ReadText(pgm_path);

    auto second = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(second.has_value());

    const auto second_bin = ReadBytes(bin_path);
    const auto second_json = ReadText(json_path);
    const auto second_pgm = ReadText(pgm_path);

    EXPECT_EQ(first_bin, second_bin);
    EXPECT_EQ(first_json, second_json);
    EXPECT_EQ(first_pgm, second_pgm);
    EXPECT_EQ(sink.artifact_count(), 2u);

    std::filesystem::remove_all(out_dir);
}

TEST(CrsdFocusedImageSinkNodeTest, ArtifactJsonContainsSchemaContractFields) {
    const auto out_dir = std::filesystem::temp_directory_path() / "graphx_focused_sink_schema";
    std::filesystem::remove_all(out_dir);

    sar::CrsdFocusedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{
        {"enabled", true},
        {"output_dir", out_dir.string()},
        {"artifact_stem", "focused"},
        {"convenience_image_format", "pgm"},
    }));

    const auto input = MakeFocusedResult();
    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());

    std::ifstream schema_stream(SAR_FOCUSED_IMAGE_SCHEMA_PATH);
    ASSERT_TRUE(schema_stream.is_open());
    nlohmann::json schema;
    schema_stream >> schema;

    std::ifstream artifact_stream(out_dir / "focused_seq42_tile2.json");
    ASSERT_TRUE(artifact_stream.is_open());
    nlohmann::json artifact;
    artifact_stream >> artifact;

    const auto required = schema.at("required");
    for (const auto& key : required) {
        ASSERT_TRUE(artifact.contains(key.get<std::string>()))
            << "missing required key: " << key;
    }

    EXPECT_EQ(artifact["ordered_crsd_segments"].size(), 3u);
    EXPECT_EQ(artifact["per_segment_input_hashes"].size(), 3u);
    EXPECT_TRUE(artifact["lineage"]["complete_aperture"].get<bool>());

    std::filesystem::remove_all(out_dir);
}

TEST(CrsdFocusedImageSinkNodeTest, JsonAndBinaryArtifactsAreConsistent) {
    const auto out_dir = std::filesystem::temp_directory_path() / "graphx_focused_sink_consistency";
    std::filesystem::remove_all(out_dir);

    sar::CrsdFocusedImageSinkNode sink;
    sink.Configure(graph::JsonView(nlohmann::json{
        {"enabled", true},
        {"output_dir", out_dir.string()},
        {"artifact_stem", "focused"},
        {"convenience_image_format", "pgm"},
    }));

    const auto input = MakeFocusedResult();
    auto out = sink.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());

    std::ifstream artifact_stream(out_dir / "focused_seq42_tile2.json");
    ASSERT_TRUE(artifact_stream.is_open());
    nlohmann::json artifact;
    artifact_stream >> artifact;

    const auto bin_bytes = ReadBytes(out_dir / "focused_seq42_tile2.bin");
    ASSERT_FALSE(bin_bytes.empty());

    EXPECT_EQ(artifact["shape"]["width"].get<std::uint32_t>(), input.grid.width);
    EXPECT_EQ(artifact["shape"]["height"].get<std::uint32_t>(), input.grid.height);
    EXPECT_EQ(artifact["ordered_set_hash"].get<std::uint64_t>(), input.input_ordered_set_hash);
    EXPECT_EQ(artifact["output_hash"].get<std::uint64_t>(), input.output_hash);
    EXPECT_EQ(
        artifact["hashes"]["binary_payload_hash"].get<std::uint64_t>(),
        HashBytes(bin_bytes));

    std::filesystem::remove_all(out_dir);
}
