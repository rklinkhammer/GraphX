#include "sar/SarVisualizationSinkNode.hpp"

#include "config/ConfigError.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

sar::SarFrameMarker DecodeMarker(std::uint64_t token) {
    return static_cast<sar::SarFrameMarker>(token & 0x3u);
}

std::uint32_t DecodeTileId(std::uint64_t token) {
    return static_cast<std::uint32_t>((token >> 2u) & 0xFFFu);
}

std::uint64_t DecodeSequenceId(std::uint64_t token) {
    return (token >> 14u) & 0xFFFFFFu;
}

std::size_t DecodeByteCount(std::uint64_t token) {
    return static_cast<std::size_t>((token >> 38u) & 0xFFFFu);
}

} // namespace

std::optional<graph::gpu::accel::HostPinnedBufferView> SarVisualizationSinkNode::Transfer(
    const graph::gpu::accel::HostPinnedBufferView& value,
    std::integral_constant<std::size_t, 0>,
    std::integral_constant<std::size_t, 0>) {
    if (!config_.enabled) {
        return value;
    }

    const auto token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.host_ptr));
    if (DecodeMarker(token) != SarFrameMarker::Data) {
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

bool SarVisualizationSinkNode::WriteArtifact(const graph::gpu::accel::HostPinnedBufferView& value) {
    std::filesystem::create_directories(config_.output_dir);

    const auto token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value.host_ptr));
    const auto sequence_id = DecodeSequenceId(token);
    const auto tile_id = DecodeTileId(token);
    const auto encoded_byte_count = DecodeByteCount(token);
    const auto byte_count = std::max<std::size_t>(encoded_byte_count, static_cast<std::size_t>(value.bytes));
    const auto element_count = std::max<std::size_t>(1u, byte_count / sizeof(float));

    const std::string base =
        config_.file_prefix +
        "_seq" + std::to_string(sequence_id) +
        "_tile" + std::to_string(tile_id);

    if (config_.format == "csv") {
        const std::string path =
            (std::filesystem::path(config_.output_dir) / (base + ".csv")).string();
        return WriteCsv(element_count, path);
    }

    const std::string path =
        (std::filesystem::path(config_.output_dir) / (base + ".pgm")).string();
    return WritePgm(element_count, path);
}

bool SarVisualizationSinkNode::WritePgm(std::size_t element_count, const std::string& path) {
    if (element_count == 0) {
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "P2\n";
    out << element_count << " 1\n";
    out << "255\n";

    const float min_val = 0.0f;
    const float max_val = static_cast<float>(std::max<std::size_t>(1u, element_count - 1u));

    const float denom = (max_val - min_val);
    const bool apply_normalize = config_.normalize && denom > 0.0f;

    for (std::size_t idx = 0; idx < element_count; ++idx) {
        float v = static_cast<float>(idx);
        if (apply_normalize) {
            v = (v - min_val) / denom;
        }
        const int gray = std::clamp(static_cast<int>(v * 255.0f), 0, 255);
        out << gray;
        if (idx + 1 < element_count) {
            out << " ";
        }
    }
    out << "\n";

    return true;
}

bool SarVisualizationSinkNode::WriteCsv(std::size_t element_count, const std::string& path) {
    if (element_count == 0) {
        return false;
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    for (std::size_t idx = 0; idx < element_count; ++idx) {
        out << static_cast<float>(idx);
        if (idx + 1 < element_count) {
            out << ",";
        }
    }
    out << "\n";

    return true;
}

bool SarVisualizationSinkNode::IsSupportedFormat(const std::string& format) {
    return format == "pgm" || format == "csv";
}

} // namespace sar
