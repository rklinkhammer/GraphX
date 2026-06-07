#include "sar/SarVisualizationSinkNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>

namespace sar {

namespace {

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

std::optional<SarImageTileMessage> SarVisualizationSinkNode::Transfer(
    const SarImageTileMessage& value,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled) {
        return value;
    }

    if (value.envelope.marker != SarFrameMarker::Data) {
        return value;
    }

    if (!WriteArtifact(value)) {
        return std::nullopt;
    }

    ++artifact_count_;
    return value;
}

void SarVisualizationSinkNode::Configure(const graph::JsonView& cfg) {
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

    if (cfg.Contains("format")) {
        auto value = cfg.TryGetString("format");
        if (!value) {
            throw value.error();
        }
        const auto format = Lowercase(value.value());
        if (!IsSupportedFormat(format)) {
            throw graph::ConfigError("format must be one of: pgm, csv");
        }
        next.format = format;
    }

    if (cfg.Contains("normalize")) {
        auto value = cfg.TryGetBool("normalize");
        if (!value) {
            throw value.error();
        }
        next.normalize = value.value();
    }

    if (cfg.Contains("file_prefix")) {
        auto value = cfg.TryGetString("file_prefix");
        if (!value) {
            throw value.error();
        }
        if (value.value().empty()) {
            throw graph::ConfigError("file_prefix must not be empty");
        }
        next.file_prefix = value.value();
    }

    config_ = next;
}

graph::JsonView SarVisualizationSinkNode::GetParameters() const {
    parameters_cache_ = nlohmann::json::object();
    parameters_cache_["enabled"] = config_.enabled;
    parameters_cache_["output_dir"] = config_.output_dir;
    parameters_cache_["format"] = config_.format;
    parameters_cache_["normalize"] = config_.normalize;
    parameters_cache_["file_prefix"] = config_.file_prefix;
    return graph::JsonView(parameters_cache_);
}

graph::JsonView SarVisualizationSinkNode::GetParameterDescription(
    const std::string& param_name) const {
    parameter_description_cache_ = nlohmann::json::object();
    for (const auto& field : Fields()) {
        if (field.name == param_name) {
            const auto type = field.type;
            const char* type_name = "object";
            switch (type) {
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

std::vector<std::string> SarVisualizationSinkNode::GetParameterNames() const {
    return {
        "enabled",
        "output_dir",
        "format",
        "normalize",
        "file_prefix",
    };
}

bool SarVisualizationSinkNode::WriteArtifact(const SarImageTileMessage& value) {
    std::filesystem::create_directories(config_.output_dir);

    const std::string base =
        config_.file_prefix +
        "_seq" + std::to_string(value.envelope.sequence_id) +
        "_tile" + std::to_string(value.envelope.tile_id);

    if (config_.format == "csv") {
        const std::string path =
            (std::filesystem::path(config_.output_dir) / (base + ".csv")).string();
        return WriteCsv(value, path);
    }

    const std::string path =
        (std::filesystem::path(config_.output_dir) / (base + ".pgm")).string();
    return WritePgm(value, path);
}

bool SarVisualizationSinkNode::WritePgm(const SarImageTileMessage& value, const std::string& path) {
    if (value.width == 0 || value.height == 0 || value.pixels.empty()) {
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "P2\n";
    out << value.width << " " << value.height << "\n";
    out << "255\n";

    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::lowest();
    for (float px : value.pixels) {
        min_val = std::min(min_val, px);
        max_val = std::max(max_val, px);
    }

    const float denom = (max_val - min_val);
    const bool apply_normalize = config_.normalize && denom > 0.0f;

    std::size_t idx = 0;
    for (std::uint32_t y = 0; y < value.height; ++y) {
        for (std::uint32_t x = 0; x < value.width; ++x) {
            if (idx >= value.pixels.size()) {
                out << "0";
            } else {
                float v = value.pixels[idx];
                if (apply_normalize) {
                    v = (v - min_val) / denom;
                }
                const int gray = std::clamp(static_cast<int>(v * 255.0f), 0, 255);
                out << gray;
            }
            ++idx;
            if (x + 1 < value.width) {
                out << " ";
            }
        }
        out << "\n";
    }

    return true;
}

bool SarVisualizationSinkNode::WriteCsv(const SarImageTileMessage& value, const std::string& path) {
    if (value.width == 0 || value.height == 0 || value.pixels.empty()) {
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    std::size_t idx = 0;
    for (std::uint32_t y = 0; y < value.height; ++y) {
        for (std::uint32_t x = 0; x < value.width; ++x) {
            if (idx < value.pixels.size()) {
                out << value.pixels[idx];
            } else {
                out << 0.0f;
            }
            ++idx;
            if (x + 1 < value.width) {
                out << ",";
            }
        }
        out << "\n";
    }

    return true;
}

bool SarVisualizationSinkNode::IsSupportedFormat(const std::string& format) {
    return format == "pgm" || format == "csv";
}

} // namespace sar
