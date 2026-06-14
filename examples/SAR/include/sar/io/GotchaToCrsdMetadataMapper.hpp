#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

struct GotchaMappedMetadata {
    std::vector<double> frequency_axis_hz{};
    double carrier_hz{0.0};
    double bandwidth_hz{0.0};
    std::uint64_t sample_count{0};
    std::array<double, 3> antenna_xyz_m{};
    double reference_range_m{0.0};
    std::string coordinate_frame{"gotcha_local_cartesian"};
};

class GotchaToCrsdMetadataMapper {
public:
    static constexpr const char* kLocalCartesianFrame = "gotcha_local_cartesian";

    [[nodiscard]] static std::optional<GotchaMappedMetadata> Map(const nlohmann::json& sidecar) {
        const auto k = ReadUnsigned(sidecar, "K");
        const auto delta_f = ReadNumber(sidecar, "deltaF");
        const auto min_f = ReadNumber(sidecar, "minF");
        const auto ant_x = ReadNumber(sidecar, "AntX");
        const auto ant_y = ReadNumber(sidecar, "AntY");
        const auto ant_z = ReadNumber(sidecar, "AntZ");
        const auto r0 = ReadNumber(sidecar, "R0");

        if (!k.has_value() || *k == 0 || !delta_f.has_value() || !min_f.has_value() ||
            !ant_x.has_value() || !ant_y.has_value() || !ant_z.has_value() || !r0.has_value()) {
            return std::nullopt;
        }

        GotchaMappedMetadata metadata{};
        metadata.sample_count = *k;
        metadata.frequency_axis_hz.reserve(static_cast<std::size_t>(*k));
        for (std::uint64_t i = 0; i < *k; ++i) {
            metadata.frequency_axis_hz.push_back(*min_f + (static_cast<double>(i) * *delta_f));
        }
        metadata.carrier_hz = *min_f + ((static_cast<double>(*k - 1U) * *delta_f) / 2.0);
        metadata.bandwidth_hz = static_cast<double>(*k) * *delta_f;
        metadata.antenna_xyz_m = {*ant_x, *ant_y, *ant_z};
        metadata.reference_range_m = *r0;
        metadata.coordinate_frame = kLocalCartesianFrame;
        return metadata;
    }

    static void ApplyToProductCollection(
        const GotchaMappedMetadata& metadata,
        CollectionMetadata& collection) {
        collection.coordinate_frame = metadata.coordinate_frame;
    }

    static void ApplyToWaveform(
        const GotchaMappedMetadata& metadata,
        WaveformMetadata& waveform) {
        waveform.frequency_axis_hz = metadata.frequency_axis_hz;
        waveform.carrier_hz = metadata.carrier_hz;
        waveform.bandwidth_hz = metadata.bandwidth_hz;
        if (waveform.sample_rate_hz <= 0.0) {
            waveform.sample_rate_hz = metadata.bandwidth_hz;
        }
    }

    static void ApplyToPulse(
        const GotchaMappedMetadata& metadata,
        PerVectorParameters& parameters) {
        parameters.platform.position_m = metadata.antenna_xyz_m;
        parameters.reference_range_m = metadata.reference_range_m;
    }

private:
    [[nodiscard]] static std::optional<double> ReadNumber(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_number()) {
            return std::nullopt;
        }
        return it->get<double>();
    }

    [[nodiscard]] static std::optional<std::uint64_t> ReadUnsigned(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end()) {
            return std::nullopt;
        }
        if (it->is_number_unsigned()) {
            return it->get<std::uint64_t>();
        }
        if (!it->is_number_integer()) {
            return std::nullopt;
        }
        const auto value = it->get<std::int64_t>();
        if (value < 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(value);
    }
};

} // namespace graphx::sar
