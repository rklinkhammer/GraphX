#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sar::reference::scenario001 {

struct LoadedRecord {
    std::array<double, 3> platform_position_m{};
    std::uint32_t range_bin_start{};
    std::uint32_t range_bin_count{};
    std::vector<std::complex<double>> iq_samples{};
};

struct LoadedFixture {
    std::string scenario_id{};
    std::string fixture_id{};
    std::string sample_layout{};
    std::string complex_encoding{};
    std::array<double, 3> scene_center_m{};
    double carrier_hz{};
    double sample_rate_hz{};
    std::vector<LoadedRecord> records{};
};

struct LoadedScenario {
    std::string scenario_id{};
    std::uint32_t image_width{};
    std::uint32_t image_height{};
    double pixel_spacing_m{};
    std::array<double, 3> scene_center_m{};
};

struct ReferenceImageArtifact {
    std::string scenario_id{};
    std::string fixture_id{};
    std::string algorithm{"cpu_reference_backprojection"};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string dtype{"float32"};
    std::string layout{"row_major"};
    std::string format{"float32_raster"};
    std::string artifact_kind{"materialized_image"};
    std::string provenance_class{"deterministic_internal_reference"};
    std::string checksum{};
    std::vector<float> pixels{};
};

struct WrittenArtifactPaths {
    std::filesystem::path raster_path{};
    std::filesystem::path contract_path{};
};

inline nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::invalid_argument("unable to open json file: " + path.string());
    }

    nlohmann::json value;
    input >> value;
    return value;
}

inline const nlohmann::json& RequireObjectField(const nlohmann::json& obj,
                                                const char* field,
                                                const char* object_name) {
    if (!obj.contains(field)) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " is required");
    }
    if (!obj.at(field).is_object()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be an object");
    }
    return obj.at(field);
}

inline std::string RequireString(const nlohmann::json& obj,
                                 const char* field,
                                 const char* object_name) {
    if (!obj.contains(field) || !obj.at(field).is_string()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be a string");
    }
    return obj.at(field).get<std::string>();
}

inline double RequireNumber(const nlohmann::json& obj,
                            const char* field,
                            const char* object_name) {
    if (!obj.contains(field) || !obj.at(field).is_number()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be numeric");
    }
    return obj.at(field).get<double>();
}

inline std::uint32_t RequireUint(const nlohmann::json& obj,
                                 const char* field,
                                 const char* object_name) {
    if (!obj.contains(field) || !obj.at(field).is_number_unsigned()) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be unsigned integer");
    }
    return obj.at(field).get<std::uint32_t>();
}

inline std::array<double, 3> RequireVec3(const nlohmann::json& obj,
                                         const char* field,
                                         const char* object_name) {
    if (!obj.contains(field) || !obj.at(field).is_array() || obj.at(field).size() != 3u) {
        throw std::invalid_argument(std::string(object_name) + "." + field + " must be a 3-element array");
    }

    std::array<double, 3> value{};
    for (std::size_t i = 0; i < 3u; ++i) {
        if (!obj.at(field).at(i).is_number()) {
            throw std::invalid_argument(std::string(object_name) + "." + field + " values must be numeric");
        }
        value[i] = obj.at(field).at(i).get<double>();
    }
    return value;
}

inline LoadedScenario LoadScenario(const std::filesystem::path& scenario_path) {
    const auto scenario = LoadJsonFile(scenario_path);

    LoadedScenario loaded{};
    loaded.scenario_id = RequireString(scenario, "scenario_id", "scenario");

    const auto& image_grid = RequireObjectField(scenario, "image_grid", "scenario");
    loaded.image_width = RequireUint(image_grid, "width", "scenario.image_grid");
    loaded.image_height = RequireUint(image_grid, "height", "scenario.image_grid");
    loaded.pixel_spacing_m = RequireNumber(image_grid, "pixel_spacing_m", "scenario.image_grid");

    const auto& scene_center = RequireObjectField(scenario, "scene_center", "scenario");
    loaded.scene_center_m = {
        RequireNumber(scene_center, "x_m", "scenario.scene_center"),
        RequireNumber(scene_center, "y_m", "scenario.scene_center"),
        RequireNumber(scene_center, "z_m", "scenario.scene_center"),
    };

    if (loaded.image_width == 0u || loaded.image_height == 0u) {
        throw std::invalid_argument("scenario.image_grid dimensions must be > 0");
    }
    if (!(loaded.pixel_spacing_m > 0.0)) {
        throw std::invalid_argument("scenario.image_grid.pixel_spacing_m must be > 0");
    }

    return loaded;
}

inline LoadedFixture LoadFixture(const std::filesystem::path& fixture_path) {
    const auto fixture = LoadJsonFile(fixture_path);

    LoadedFixture loaded{};
    loaded.scenario_id = RequireString(fixture, "scenario_id", "fixture");
    loaded.fixture_id = RequireString(fixture, "fixture_id", "fixture");
    loaded.sample_layout = RequireString(fixture, "sample_layout", "fixture");
    loaded.complex_encoding = RequireString(fixture, "complex_encoding", "fixture");

    const auto& geometry = RequireObjectField(fixture, "geometry", "fixture");
    loaded.scene_center_m = RequireVec3(geometry, "scene_center_m", "fixture.geometry");
    loaded.carrier_hz = RequireNumber(geometry, "carrier_hz", "fixture.geometry");
    loaded.sample_rate_hz = RequireNumber(geometry, "sample_rate_hz", "fixture.geometry");

    if (!(loaded.carrier_hz > 0.0)) {
        throw std::invalid_argument("fixture.geometry.carrier_hz must be > 0");
    }
    if (!(loaded.sample_rate_hz > 0.0)) {
        throw std::invalid_argument("fixture.geometry.sample_rate_hz must be > 0");
    }

    if (!fixture.contains("records") || !fixture.at("records").is_array() ||
        fixture.at("records").empty()) {
        throw std::invalid_argument("fixture.records must be a non-empty array");
    }

    for (const auto& record : fixture.at("records")) {
        LoadedRecord out{};
        out.platform_position_m = RequireVec3(record, "platform_position_m", "fixture.records[]");
        out.range_bin_start = RequireUint(record, "range_bin_start", "fixture.records[]");
        out.range_bin_count = RequireUint(record, "range_bin_count", "fixture.records[]");

        if (!record.contains("iq_samples") || !record.at("iq_samples").is_array()) {
            throw std::invalid_argument("fixture.records[].iq_samples must be an array");
        }

        out.iq_samples.reserve(record.at("iq_samples").size());
        for (const auto& sample : record.at("iq_samples")) {
            const double real = RequireNumber(sample, "real", "fixture.records[].iq_samples[]");
            const double imag = RequireNumber(sample, "imag", "fixture.records[].iq_samples[]");
            out.iq_samples.emplace_back(real, imag);
        }

        if (out.range_bin_count == 0u) {
            throw std::invalid_argument("fixture.records[].range_bin_count must be > 0");
        }
        if (out.iq_samples.size() != out.range_bin_count) {
            throw std::invalid_argument("fixture.records[].iq_samples size must equal range_bin_count");
        }

        loaded.records.push_back(std::move(out));
    }

    return loaded;
}

inline double SlantRange(const std::array<double, 3>& sensor, const std::array<double, 3>& point) {
    const double dx = point[0] - sensor[0];
    const double dy = point[1] - sensor[1];
    const double dz = point[2] - sensor[2];
    return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
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

inline ReferenceImageArtifact RunCpuReferenceBackprojection(
    const std::filesystem::path& scenario_path,
    const std::filesystem::path& fixture_path) {
    constexpr double kC = 299792458.0;
    constexpr double kPi = 3.141592653589793238462643383279502884;

    const auto scenario = LoadScenario(scenario_path);
    const auto fixture = LoadFixture(fixture_path);

    if (scenario.scenario_id != fixture.scenario_id) {
        throw std::invalid_argument("scenario_id mismatch between scenario and fixture");
    }

    // Assumption: range bins are centered around the scene-center slant range for each pulse.
    const double wavelength_m = kC / fixture.carrier_hz;
    const double range_spacing_m = kC / (2.0 * fixture.sample_rate_hz);

    const auto pixel_count = static_cast<std::size_t>(scenario.image_width) * scenario.image_height;
    std::vector<float> pixels(pixel_count, 0.0f);

    const double cx = (static_cast<double>(scenario.image_width) - 1.0) * 0.5;
    const double cy = (static_cast<double>(scenario.image_height) - 1.0) * 0.5;

    for (std::uint32_t y = 0; y < scenario.image_height; ++y) {
        const double py = scenario.scene_center_m[1] +
                          (static_cast<double>(y) - cy) * scenario.pixel_spacing_m;
        for (std::uint32_t x = 0; x < scenario.image_width; ++x) {
            const double px = scenario.scene_center_m[0] +
                              (static_cast<double>(x) - cx) * scenario.pixel_spacing_m;
            const std::array<double, 3> pixel_point{px, py, scenario.scene_center_m[2]};

            std::complex<double> accum{0.0, 0.0};
            for (const auto& record : fixture.records) {
                const double r = SlantRange(record.platform_position_m, pixel_point);
                const double r0 = SlantRange(record.platform_position_m, fixture.scene_center_m);
                const double delta_r = r - r0;
                const long long sample_offset = static_cast<long long>(std::llround(delta_r / range_spacing_m));

                const long long center_bin = static_cast<long long>(record.range_bin_start) +
                                             static_cast<long long>(record.range_bin_count / 2u);
                const long long absolute_bin = center_bin + sample_offset;
                const long long local_index = absolute_bin - static_cast<long long>(record.range_bin_start);

                if (local_index < 0 ||
                    local_index >= static_cast<long long>(record.iq_samples.size())) {
                    continue;
                }

                const std::complex<double> sample = record.iq_samples[static_cast<std::size_t>(local_index)];
                const double phase = 4.0 * kPi * r / wavelength_m;
                const std::complex<double> correction{std::cos(phase), std::sin(phase)};
                accum += sample * correction;
            }

            const auto idx = static_cast<std::size_t>(y) * scenario.image_width + x;
            pixels[idx] = static_cast<float>(std::abs(accum) /
                                             static_cast<double>(fixture.records.size()));
        }
    }

    ReferenceImageArtifact artifact{};
    artifact.scenario_id = scenario.scenario_id;
    artifact.fixture_id = fixture.fixture_id;
    artifact.width = scenario.image_width;
    artifact.height = scenario.image_height;
    artifact.pixels = std::move(pixels);
    artifact.checksum = Fnv1a64Hex(artifact.pixels);
    return artifact;
}

inline nlohmann::json BuildArtifactContract(const ReferenceImageArtifact& artifact,
                                            const std::filesystem::path& raw_path) {
    return nlohmann::json{
        {"source_tool", "cpu-reference-backprojection"},
        {"provenance_class", artifact.provenance_class},
        {"scenario_id", artifact.scenario_id},
        {"fixture_id", artifact.fixture_id},
        {"algorithm", artifact.algorithm},
        {"format", artifact.format},
        {"layout", artifact.layout},
        {"artifact_kind", artifact.artifact_kind},
        {"dtype", artifact.dtype},
        {"width", artifact.width},
        {"height", artifact.height},
        {"byte_count", static_cast<std::uint64_t>(artifact.pixels.size() * sizeof(float))},
        {"raw_path", raw_path.string()},
        {"deterministic_hash", artifact.checksum},
    };
}

inline WrittenArtifactPaths WriteArtifact(const ReferenceImageArtifact& artifact,
                                          const std::filesystem::path& output_dir,
                                          const std::string& base_name) {
    std::filesystem::create_directories(output_dir);

    WrittenArtifactPaths paths{};
    paths.raster_path = output_dir / (base_name + ".bin");
    paths.contract_path = output_dir / (base_name + "_contract.json");

    {
        std::ofstream raster(paths.raster_path, std::ios::binary | std::ios::trunc);
        if (!raster.good()) {
            throw std::invalid_argument("unable to write raster artifact: " + paths.raster_path.string());
        }
        for (float value : artifact.pixels) {
            raster.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    }

    {
        std::ofstream contract(paths.contract_path, std::ios::binary | std::ios::trunc);
        if (!contract.good()) {
            throw std::invalid_argument("unable to write contract artifact: " + paths.contract_path.string());
        }
        const auto json = BuildArtifactContract(artifact, paths.raster_path);
        contract << json.dump(2) << '\n';
    }

    return paths;
}

} // namespace sar::reference::scenario001
