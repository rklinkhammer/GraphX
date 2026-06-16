#include "sar/CrsdFocusedImageSinkNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace sar {

namespace {

constexpr std::array<char, 8> kBinaryMagic{{'G', 'X', 'F', 'I', 'M', 'G', '0', '1'}};

std::string ExecutionLaneFromToken(const SarAccelControlToken& control) {
    switch (control.sidecar.backend) {
        case SarBackendKind::NativeDevice:
            return "metal";
        case SarBackendKind::SimulatedDevice:
            return "simulated_device";
        case SarBackendKind::Host:
            return "cpu";
    }
    return "cpu";
}

struct BinaryHeader {
    std::array<char, 8> magic{kBinaryMagic};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t samples_per_pulse{0};
    std::uint32_t total_pulses{0};
    std::uint64_t output_hash{0};
    std::uint64_t ordered_set_hash{0};
};

} // namespace

std::optional<FocusedImageResult> CrsdFocusedImageSinkNode::Transfer(
    const FocusedImageResult& value,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled) {
        return value;
    }

    if (value.pixels.empty() || value.grid.width == 0u || value.grid.height == 0u) {
        return std::nullopt;
    }

    if (!PersistArtifacts(value)) {
        return std::nullopt;
    }

    ++artifact_count_;
    return value;
}

void CrsdFocusedImageSinkNode::Configure(const graph::JsonView& cfg) {
    auto next = config_;

    if (cfg.Contains("enabled")) {
        auto value = cfg.TryGetBool("enabled");
        if (!value) {
            throw value.error();
        }
        next.enabled = value.value();
    }

    if (cfg.Contains("output_dir")) {
        auto value = cfg.TryGetString("output_dir");
        if (!value) {
            throw value.error();
        }
        if (value.value().empty()) {
            throw graph::ConfigError("output_dir must not be empty");
        }
        next.output_dir = value.value();
    }

    if (cfg.Contains("artifact_stem")) {
        auto value = cfg.TryGetString("artifact_stem");
        if (!value) {
            throw value.error();
        }
        if (value.value().empty()) {
            throw graph::ConfigError("artifact_stem must not be empty");
        }
        next.artifact_stem = value.value();
    }

    if (cfg.Contains("convenience_image_format")) {
        auto value = cfg.TryGetString("convenience_image_format");
        if (!value) {
            throw value.error();
        }
        const auto format = Lowercase(value.value());
        if (!IsSupportedImageFormat(format)) {
            throw graph::ConfigError("convenience_image_format must be pgm");
        }
        next.convenience_image_format = format;
    }

    config_ = next;
}

graph::JsonView CrsdFocusedImageSinkNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    parameters_cache_["output_dir"] = config_.output_dir;
    parameters_cache_["artifact_stem"] = config_.artifact_stem;
    parameters_cache_["convenience_image_format"] = config_.convenience_image_format;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView CrsdFocusedImageSinkNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const char* type_name = "object";
            switch (field.type) {
                case graph::JsonType::String: type_name = "string"; break;
                case graph::JsonType::Number: type_name = "number"; break;
                case graph::JsonType::Integer: type_name = "integer"; break;
                case graph::JsonType::Boolean: type_name = "boolean"; break;
                case graph::JsonType::Object: type_name = "object"; break;
                case graph::JsonType::Array: type_name = "array"; break;
            }
            parameter_description_cache_["type"] = type_name;
            parameter_description_cache_["required"] = field.required;
            parameter_description_cache_["description"] = field.description;
            break;
        }
    }
    return graph::JsonView(parameter_description_cache_);
}

std::vector<std::string> CrsdFocusedImageSinkNode::GetParameterNames() const {
    return {
        "enabled",
        "output_dir",
        "artifact_stem",
        "convenience_image_format",
    };
}

bool CrsdFocusedImageSinkNode::PersistArtifacts(const FocusedImageResult& value) {
    std::filesystem::create_directories(config_.output_dir);

    const std::string base =
        config_.artifact_stem +
        "_seq" + std::to_string(value.control.sidecar.sequence_id) +
        "_tile" + std::to_string(value.control.sidecar.tile_id);

    const auto base_path = std::filesystem::path(config_.output_dir) / base;
    const auto bin_path = (base_path.string() + ".bin");
    const auto json_path = (base_path.string() + ".json");
    const auto pgm_path = (base_path.string() + ".pgm");

    if (!WriteBinary(value, bin_path)) {
        return false;
    }
    if (!WriteJson(value, bin_path, json_path)) {
        return false;
    }
    if (!WritePgm(value, pgm_path)) {
        return false;
    }

    return true;
}

bool CrsdFocusedImageSinkNode::WriteBinary(
    const FocusedImageResult& value,
    const std::string& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    BinaryHeader header{};
    header.width = value.grid.width;
    header.height = value.grid.height;
    header.samples_per_pulse = value.samples_per_pulse;
    header.total_pulses = value.total_pulses;
    header.output_hash = value.output_hash;
    header.ordered_set_hash = value.input_ordered_set_hash;

    out.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(BinaryHeader)));
    out.write(
        reinterpret_cast<const char*>(value.pixels.data()),
        static_cast<std::streamsize>(value.pixels.size() * sizeof(float)));

    return out.good();
}

bool CrsdFocusedImageSinkNode::WriteJson(
    const FocusedImageResult& value,
    const std::string& bin_path,
    const std::string& path) const {
    const auto binary_bytes = ReadFileBytes(bin_path);
    if (binary_bytes.empty()) {
        return false;
    }

    const auto binary_payload_hash = HashBytes(binary_bytes);

    nlohmann::json json = nlohmann::json::object();
    json["schema_version"] = "graphx.sar.focused_image_artifact.v1";

    json["shape"] = nlohmann::json{{"width", value.grid.width}, {"height", value.grid.height}};
    json["spacing"] = nlohmann::json{{"pixel_spacing_m", value.grid.pixel_spacing_m},
                                      {"range_spacing_m", value.grid.range_spacing_m}};
    json["geometry_assumptions"] = nlohmann::json{
        {"scene_center_x_m", value.grid.scene_center_x_m},
        {"scene_center_y_m", value.grid.scene_center_y_m},
        {"range_origin_m", value.grid.range_origin_m},
        {"wavelength_m", value.grid.wavelength_m},
        {"platform_x_start_m", value.grid.platform_x_start_m},
        {"platform_x_end_m", value.grid.platform_x_end_m},
        {"platform_y_m", value.grid.platform_y_m}
    };

    json["hashes"] = nlohmann::json{
        {"per_segment_input_hashes", value.per_segment_input_hashes},
        {"ordered_set_hash", value.input_ordered_set_hash},
        {"output_hash", value.output_hash},
        {"binary_payload_hash", binary_payload_hash}
    };

    json["provenance"] = nlohmann::json{
        {"pipeline", "crsd_to_focused_image"},
        {"node", "CrsdFocusedImageSinkNode"},
        {"artifact_stem", config_.artifact_stem},
        {"sequence_id", value.control.sidecar.sequence_id},
        {"tile_id", value.control.sidecar.tile_id}
    };

    json["execution_lane"] = nlohmann::json{
        {"lane", ExecutionLaneFromToken(value.control)},
        {"backend_id", value.control.sidecar.backend_id},
        {"kernel_dispatches", value.control.sidecar.kernel_dispatches},
        {"bytes_h2d", value.control.sidecar.bytes_h2d},
        {"bytes_d2h", value.control.sidecar.bytes_d2h}
    };

    json["ordered_crsd_segments"] = value.ordered_crsd_segment_indices;
    json["per_segment_input_hashes"] = value.per_segment_input_hashes;
    json["ordered_set_hash"] = value.input_ordered_set_hash;
    json["output_hash"] = value.output_hash;

    json["lineage"] = nlohmann::json{
        {"complete_aperture", value.lineage_complete_aperture},
        {"total_pulses", value.total_pulses},
        {"samples_per_pulse", value.samples_per_pulse},
        {"segment_count", value.ordered_crsd_segment_indices.size()},
        {"binary_artifact", std::filesystem::path(bin_path).filename().string()}
    };

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << json.dump(2) << '\n';
    return out.good();
}

bool CrsdFocusedImageSinkNode::WritePgm(
    const FocusedImageResult& value,
    const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    const auto width = static_cast<std::size_t>(value.grid.width);
    const auto height = static_cast<std::size_t>(value.grid.height);
    if (width == 0u || height == 0u || value.pixels.size() != width * height) {
        return false;
    }

    const auto [min_it, max_it] = std::minmax_element(value.pixels.begin(), value.pixels.end());
    const float min_val = *min_it;
    const float max_val = *max_it;
    const float denom = max_val - min_val;

    out << "P2\n";
    out << width << " " << height << "\n";
    out << "255\n";

    for (std::size_t idx = 0u; idx < value.pixels.size(); ++idx) {
        float normalized = 0.0f;
        if (denom > 0.0f) {
            normalized = (value.pixels[idx] - min_val) / denom;
        }
        const int gray = std::clamp(static_cast<int>(normalized * 255.0f), 0, 255);
        out << gray;
        if (((idx + 1u) % width) == 0u) {
            out << '\n';
        } else {
            out << ' ';
        }
    }

    return out.good();
}

bool CrsdFocusedImageSinkNode::IsSupportedImageFormat(const std::string& format) {
    return format == "pgm";
}

std::string CrsdFocusedImageSinkNode::Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::uint64_t CrsdFocusedImageSinkNode::HashBytes(const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto b : bytes) {
        hash ^= static_cast<std::uint8_t>(b);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::byte> CrsdFocusedImageSinkNode::ReadFileBytes(const std::string& path) {
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

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in.good() && !in.eof()) {
        return {};
    }

    return bytes;
}

} // namespace sar
