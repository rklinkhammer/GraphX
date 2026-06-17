// SPDX-License-Identifier: MIT

/**
 * @file SarScenario001GraphxRunner.hpp
 * @brief GraphX source file.
 */

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sar::scenario001::graphx {

inline nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::invalid_argument("unable to open json file: " + path.string());
    }

    nlohmann::json value;
    input >> value;
    return value;
}

inline const nlohmann::json& RequireObject(const nlohmann::json& value,
                                           const char* field,
                                           const char* object_name) {
    if (!value.contains(field)) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " is required");
    }
    if (!value.at(field).is_object()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be an object");
    }
    return value.at(field);
}

inline const nlohmann::json& RequireArray(const nlohmann::json& value,
                                          const char* field,
                                          const char* object_name) {
    if (!value.contains(field)) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " is required");
    }
    if (!value.at(field).is_array()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be an array");
    }
    return value.at(field);
}

inline std::string RequireString(const nlohmann::json& value,
                                 const char* field,
                                 const char* object_name) {
    if (!value.contains(field) || !value.at(field).is_string()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be a string");
    }
    return value.at(field).get<std::string>();
}

inline double RequireNumber(const nlohmann::json& value,
                            const char* field,
                            const char* object_name) {
    if (!value.contains(field) || !value.at(field).is_number()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be numeric");
    }
    return value.at(field).get<double>();
}

inline std::uint64_t RequireUint(const nlohmann::json& value,
                                 const char* field,
                                 const char* object_name) {
    if (!value.contains(field) || !value.at(field).is_number_unsigned()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be unsigned integer");
    }
    return value.at(field).get<std::uint64_t>();
}

inline std::array<double, 3> RequireVec3(const nlohmann::json& value,
                                         const char* field,
                                         const char* object_name) {
    const auto& arr = RequireArray(value, field, object_name);
    if (arr.size() != 3u) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must have 3 elements");
    }

    std::array<double, 3> out{};
    for (std::size_t i = 0; i < 3u; ++i) {
        if (!arr[i].is_number()) {
            throw std::invalid_argument(std::string(object_name) + "." + field + " values must be numeric");
        }
        out[i] = arr[i].get<double>();
    }
    return out;
}

inline std::string Fnv1a64Hex(const std::vector<float>& values) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;

    std::uint64_t hash = kOffsetBasis;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(values.data());
    const auto byte_count = values.size() * sizeof(float);
    for (std::size_t i = 0; i < byte_count; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }

    constexpr char kHex[] = "0123456789abcdef";
    std::string out(16u, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[hash & 0xFu];
        hash >>= 4u;
    }
    return out;
}

inline nlohmann::json ConvertToGotchaReplayFixture(const nlohmann::json& fixture) {
    const auto scenario_id = RequireString(fixture, "scenario_id", "fixture");
    const auto fixture_id = RequireString(fixture, "fixture_id", "fixture");

    const auto sample_layout = RequireString(fixture, "sample_layout", "fixture");
    const auto geometry = RequireObject(fixture, "geometry", "fixture");
    const auto scene_center_m = RequireVec3(geometry, "scene_center_m", "fixture.geometry");
    const auto carrier_hz = RequireNumber(geometry, "carrier_hz", "fixture.geometry");
    const auto bandwidth_hz = RequireNumber(geometry, "bandwidth_hz", "fixture.geometry");
    const auto sample_rate_hz = RequireNumber(geometry, "sample_rate_hz", "fixture.geometry");
    const auto coordinate_frame = RequireString(geometry, "coordinate_frame", "fixture.geometry");

    const auto& records = RequireArray(fixture, "records", "fixture");
    if (records.empty()) {
        throw std::invalid_argument("fixture.records must not be empty");
    }

    nlohmann::json out = {
        {"schema", "graphx.sar.gotcha.normalized.v1"},
        {"derived_from_scenario", scenario_id},
        {"source_fixture_id", fixture_id},
        {"ci_safe", true},
        {"records", nlohmann::json::array()},
    };

    std::uint64_t ordering_key = 0u;
    for (const auto& record : records) {
        const auto pulse_index = RequireUint(record, "pulse_index", "fixture.records[]");
        const auto timestamp_us = RequireUint(record, "timestamp_us", "fixture.records[]");
        const auto range_bin_start = RequireUint(record, "range_bin_start", "fixture.records[]");
        const auto range_bin_count = RequireUint(record, "range_bin_count", "fixture.records[]");
        const auto platform_position_m = RequireVec3(record, "platform_position_m", "fixture.records[]");
        const auto platform_velocity_mps = RequireVec3(record, "platform_velocity_mps", "fixture.records[]");

        const auto& iq_samples = RequireArray(record, "iq_samples", "fixture.records[]");
        if (iq_samples.size() != range_bin_count) {
            throw std::invalid_argument("fixture.records[].iq_samples size must equal range_bin_count");
        }

        nlohmann::json normalized_samples = nlohmann::json::array();
        for (const auto& sample : iq_samples) {
            const auto real = RequireNumber(sample, "real", "fixture.records[].iq_samples[]");
            const auto imag = RequireNumber(sample, "imag", "fixture.records[].iq_samples[]");
            normalized_samples.push_back({{"real", real}, {"imag", imag}});
        }

        out["records"].push_back({
            {"frame_id", pulse_index},
            {"pass_id", 1u},
            {"pulse_block_id", 9000u + pulse_index},
            {"range_bin_start", range_bin_start},
            {"range_bin_count", range_bin_count},
            {"aperture_span_start", pulse_index},
            {"aperture_span_count", 1u},
            {"timestamp_us", timestamp_us},
            {"ordering_key", ordering_key++},
            {"stream_id", 5u},
            {"backend_id", 0u},
            {"backend", 0u},
            {"platform_position_m", {platform_position_m[0], platform_position_m[1], platform_position_m[2]}},
            {"platform_velocity_mps", {platform_velocity_mps[0], platform_velocity_mps[1], platform_velocity_mps[2]}},
            {"scene_center_m", {scene_center_m[0], scene_center_m[1], scene_center_m[2]}},
            {"carrier_hz", carrier_hz},
            {"bandwidth_hz", bandwidth_hz},
            {"sample_rate_hz", sample_rate_hz},
            {"calibration_gain", 1.0},
            {"calibration_phase_rad", 0.0},
            {"polarization", "VV"},
            {"coordinate_frame", coordinate_frame},
            {"sample_layout", sample_layout},
            {"endianness", "native"},
            {"iq_samples", normalized_samples},
        });
    }

    return out;
}

inline void WriteFloat32Raster(const std::filesystem::path& path, const std::vector<float>& pixels) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        throw std::invalid_argument("unable to write raster: " + path.string());
    }
    for (const float value : pixels) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

inline void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        throw std::invalid_argument("unable to write json: " + path.string());
    }
    output << value.dump(2) << '\n';
}

inline nlohmann::json BuildGraphxArtifactContract(const std::string& scenario_id,
                                                  const std::string& fixture_id,
                                                  std::uint32_t width,
                                                  std::uint32_t height,
                                                  const std::filesystem::path& raw_path,
                                                  const std::string& hash) {
    return nlohmann::json{
        {"source_tool", "graphx"},
        {"provenance_class", "graphx_runtime"},
        {"scenario_id", scenario_id},
        {"fixture_id", fixture_id},
        {"algorithm", "graphx_sar_pipeline"},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", width},
        {"height", height},
        {"byte_count", static_cast<std::uint64_t>(width) * height * sizeof(float)},
        {"raw_path", raw_path.string()},
        {"deterministic_hash", hash},
    };
}

} // namespace sar::scenario001::graphx