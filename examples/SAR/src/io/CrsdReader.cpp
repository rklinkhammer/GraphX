#include "sar/io/CrsdReader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {
namespace {

constexpr const char* kTinyHelperSchema = "graphx.sar.crsd.tiny.v1";
constexpr std::size_t kCrsdHeaderProbeBytes = 1024u;

[[nodiscard]] std::uint64_t Fnv1a64(
    const void* data,
    std::size_t size,
    std::uint64_t seed = 14695981039346656037ull) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::uint64_t HashDouble(double value, std::uint64_t seed) {
    return Fnv1a64(&value, sizeof(value), seed);
}

[[nodiscard]] std::uint64_t HashUInt64(std::uint64_t value, std::uint64_t seed) {
    return Fnv1a64(&value, sizeof(value), seed);
}

[[nodiscard]] std::uint64_t HashFloat(float value, std::uint64_t seed) {
    return Fnv1a64(&value, sizeof(value), seed);
}

[[nodiscard]] bool IsProductCrsdPath(const std::filesystem::path& path) {
    return path.filename() == "product.crsd";
}

[[nodiscard]] std::vector<std::filesystem::path> ResolveDirectoryPaths(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> paths;
    if (!std::filesystem::exists(directory)) {
        return paths;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!IsProductCrsdPath(entry.path())) {
            continue;
        }
        paths.push_back(entry.path());
    }

    std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });
    return paths;
}

[[nodiscard]] std::vector<std::filesystem::path> ResolveManifestPaths(
    const std::filesystem::path& manifest_path) {
    std::ifstream in(manifest_path);
    if (!in) {
        return {};
    }

    nlohmann::json manifest;
    in >> manifest;
    if (!manifest.is_object() ||
        !manifest.contains("files") ||
        !manifest.at("files").is_array()) {
        return {};
    }

    std::vector<std::filesystem::path> paths;
    const auto parent = manifest_path.parent_path();
    for (const auto& entry : manifest.at("files")) {
        if (!entry.is_object() ||
            !entry.contains("path") ||
            !entry.at("path").is_string()) {
            continue;
        }
        auto path = std::filesystem::path(entry.at("path").get<std::string>());
        if (path.is_relative()) {
            path = parent / path;
        }
        paths.push_back(path);
    }
    return paths;
}

[[nodiscard]] bool GetTriplet(
    const nlohmann::json& object,
    const char* key,
    std::array<double, 3>& out) {
    if (!object.contains(key) ||
        !object.at(key).is_array() ||
        object.at(key).size() != 3u) {
        return false;
    }

    const auto& arr = object.at(key);
    if (!arr.at(0).is_number() ||
        !arr.at(1).is_number() ||
        !arr.at(2).is_number()) {
        return false;
    }

    out = {
        arr.at(0).get<double>(),
        arr.at(1).get<double>(),
        arr.at(2).get<double>(),
    };
    return true;
}

[[nodiscard]] CrsdReadResult ParseJsonHelperCrsd(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "crsd_open_failed:" + path.generic_string(),
        };
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (...) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "unsupported_non_crsd_file:" + path.generic_string(),
        };
    }
    if (!doc.is_object() ||
        !doc.contains("schema") ||
        !doc.at("schema").is_string() ||
        doc.at("schema").get<std::string>() != kTinyHelperSchema) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "unsupported_non_crsd_file:" + path.generic_string(),
        };
    }

    if (!doc.contains("segment_index") || !doc.at("segment_index").is_number_integer()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:segment_index:" + path.generic_string(),
        };
    }
    if (!doc.contains("carrier_hz") || !doc.at("carrier_hz").is_number()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:carrier_hz:" + path.generic_string(),
        };
    }
    if (!doc.contains("sample_rate_hz") || !doc.at("sample_rate_hz").is_number()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:sample_rate_hz:" + path.generic_string(),
        };
    }
    if (!doc.contains("vectors") ||
        !doc.at("vectors").is_array() ||
        doc.at("vectors").empty()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:vectors:" + path.generic_string(),
        };
    }

    CrsdSegmentRecord segment{};
    segment.crsd_path = path;
    segment.segment_index = static_cast<std::uint64_t>(
        doc.at("segment_index").get<std::int64_t>());
    segment.carrier_hz = doc.at("carrier_hz").get<double>();
    segment.sample_rate_hz = doc.at("sample_rate_hz").get<double>();

    const auto& vectors = doc.at("vectors");
    segment.vector_count = static_cast<std::uint64_t>(vectors.size());
    segment.samples_per_vector = 0u;
    segment.vectors.reserve(vectors.size());

    std::uint64_t payload_hash = 14695981039346656037ull;
    for (std::size_t vector_idx = 0; vector_idx < vectors.size(); ++vector_idx) {
        const auto& vector = vectors.at(vector_idx);
        if (!vector.is_object()) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "unsupported_crsd:vector_not_object:" + path.generic_string(),
            };
        }
        if (!vector.contains("vector_index") || !vector.at("vector_index").is_number_integer()) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "missing_required_metadata:vector_index:" + path.generic_string(),
            };
        }
        if (!vector.contains("rcv_time_s") || !vector.at("rcv_time_s").is_number()) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "missing_required_pvp:RcvTime:" + path.generic_string(),
            };
        }
        if (!vector.contains("samples") ||
            !vector.at("samples").is_array() ||
            vector.at("samples").empty()) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "missing_required_metadata:samples:" + path.generic_string(),
            };
        }

        std::array<double, 3> position{};
        std::array<double, 3> velocity{};
        if (!GetTriplet(vector, "platform_position_m", position) ||
            !GetTriplet(vector, "platform_velocity_mps", velocity)) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "missing_required_pvp:geometry:" + path.generic_string(),
            };
        }

        const auto samples_per_vector = vector.at("samples").size();
        if (segment.samples_per_vector == 0u) {
            segment.samples_per_vector = static_cast<std::uint64_t>(samples_per_vector);
        }
        if (segment.samples_per_vector != static_cast<std::uint64_t>(samples_per_vector)) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "unsupported_crsd:inconsistent_samples_per_vector:" + path.generic_string(),
            };
        }

        CrsdVectorRecord record{};
        record.vector_index = static_cast<std::uint64_t>(
            vector.at("vector_index").get<std::int64_t>());
        record.rcv_time_s = vector.at("rcv_time_s").get<double>();
        record.platform_position_m = position;
        record.platform_velocity_mps = velocity;

        for (const auto& sample : vector.at("samples")) {
            if (!sample.is_array() ||
                sample.size() != 2u ||
                !sample.at(0).is_number() ||
                !sample.at(1).is_number()) {
                return CrsdReadResult{
                    .success = false,
                    .diagnostic = "unsupported_crsd:complex_sample:" + path.generic_string(),
                };
            }
            const float real = sample.at(0).get<float>();
            const float imag = sample.at(1).get<float>();
            record.signal.emplace_back(real, imag);
            payload_hash = HashFloat(real, payload_hash);
            payload_hash = HashFloat(imag, payload_hash);
        }

        segment.vectors.push_back(record);
    }

    segment.first_vector = segment.vectors.front();
    segment.last_vector = segment.vectors.back();
    segment.payload_hash = payload_hash;

    auto hash_record = [](const CrsdVectorRecord& record) {
        std::uint64_t hash = 14695981039346656037ull;
        hash = HashUInt64(record.vector_index, hash);
        hash = HashDouble(record.rcv_time_s, hash);
        for (const auto value : record.platform_position_m) {
            hash = HashDouble(value, hash);
        }
        for (const auto value : record.platform_velocity_mps) {
            hash = HashDouble(value, hash);
        }
        for (const auto& sample : record.signal) {
            hash = HashFloat(sample.real(), hash);
            hash = HashFloat(sample.imag(), hash);
        }
        return hash;
    };

    segment.first_vector_hash = hash_record(segment.first_vector);
    segment.last_vector_hash = hash_record(segment.last_vector);

    OrderedCrsdSetReadResult out{};
    out.segments.push_back(std::move(segment));
    out.total_vector_count = out.segments.front().vector_count;
    out.ordered_set_payload_hash = out.segments.front().payload_hash;

    return CrsdReadResult{
        .success = true,
        .diagnostic = "ok:test_helper_json",
        .value = std::move(out),
    };
}

[[nodiscard]] std::optional<std::uint64_t> ParseUint64(const std::string& value) {
    try {
        std::size_t consumed = 0u;
        const auto parsed = std::stoull(value, &consumed, 10);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::string> ExtractFirstTag(
    const std::string& xml,
    const std::string& tag) {
    const std::regex pattern(
        "<" + tag + R"(>(.*?)</)" + tag + ">",
        std::regex::icase | std::regex::optimize | std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(xml, match, pattern) || match.size() < 2u) {
        return std::nullopt;
    }
    return match.str(1);
}

[[nodiscard]] std::optional<std::uint64_t> ExtractPvpOffsetWords(
    const std::string& xml,
    const std::string& field_name) {
    const std::regex pattern(
        "<" + field_name + R"(>\s*<Offset>([0-9]+)</Offset>)",
        std::regex::icase | std::regex::optimize | std::regex::ECMAScript);
    std::smatch match;
    if (!std::regex_search(xml, match, pattern) || match.size() < 2u) {
        return std::nullopt;
    }
    return ParseUint64(match.str(1));
}

[[nodiscard]] bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin());
}

[[nodiscard]] std::optional<std::uint64_t> DeriveSegmentIndexFromPath(
    const std::filesystem::path& path) {
    static const std::regex subdata_pattern(R"(subData([0-9]+)\.crsd_output)", std::regex::icase);
    static const std::regex segment_pattern(R"(segment_([0-9]+))", std::regex::icase);

    for (const auto& part : path) {
        const auto token = part.string();
        std::smatch match;
        if (std::regex_search(token, match, subdata_pattern) && match.size() >= 2u) {
            const auto value = ParseUint64(match.str(1));
            if (value && *value > 0u) {
                return *value - 1u;
            }
        }
        if (std::regex_search(token, match, segment_pattern) && match.size() >= 2u) {
            const auto value = ParseUint64(match.str(1));
            if (value) {
                return *value;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] double ReadBeF8(const std::uint8_t* data) {
    std::uint64_t value =
        (static_cast<std::uint64_t>(data[0]) << 56u) |
        (static_cast<std::uint64_t>(data[1]) << 48u) |
        (static_cast<std::uint64_t>(data[2]) << 40u) |
        (static_cast<std::uint64_t>(data[3]) << 32u) |
        (static_cast<std::uint64_t>(data[4]) << 24u) |
        (static_cast<std::uint64_t>(data[5]) << 16u) |
        (static_cast<std::uint64_t>(data[6]) << 8u) |
        (static_cast<std::uint64_t>(data[7]));
    double out = 0.0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

[[nodiscard]] float ReadBeF4(const std::uint8_t* data) {
    std::uint32_t value =
        (static_cast<std::uint32_t>(data[0]) << 24u) |
        (static_cast<std::uint32_t>(data[1]) << 16u) |
        (static_cast<std::uint32_t>(data[2]) << 8u) |
        (static_cast<std::uint32_t>(data[3]));
    float out = 0.0f;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

[[nodiscard]] CrsdReadResult ParseBinaryCrsd(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "crsd_open_failed:" + path.generic_string(),
        };
    }

    std::string header(kCrsdHeaderProbeBytes, '\0');
    in.read(header.data(), static_cast<std::streamsize>(header.size()));
    const auto read_count = static_cast<std::size_t>(in.gcount());
    header.resize(read_count);
    if (!StartsWith(header, "CRSD/")) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "unsupported_non_crsd_file:" + path.generic_string(),
        };
    }

    std::unordered_map<std::string, std::string> header_map;
    {
        std::size_t start = 0u;
        while (start < header.size()) {
            auto end = header.find('\n', start);
            if (end == std::string::npos) {
                end = header.size();
            }
            std::string line = header.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.find(":=") != std::string::npos) {
                const auto pos = line.find(":=");
                auto key = line.substr(0, pos);
                auto value = line.substr(pos + 2u);
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
                    key.pop_back();
                }
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
                    value.erase(value.begin());
                }
                header_map[key] = value;
            }
            if (end == header.size()) {
                break;
            }
            start = end + 1u;
        }
    }

    const auto xml_size = header_map.contains("XML_BLOCK_SIZE")
                              ? ParseUint64(header_map.at("XML_BLOCK_SIZE"))
                              : std::nullopt;
    const auto xml_offset = header_map.contains("XML_BLOCK_BYTE_OFFSET")
                                ? ParseUint64(header_map.at("XML_BLOCK_BYTE_OFFSET"))
                                : std::nullopt;
    const auto pvp_size = header_map.contains("PVP_BLOCK_SIZE")
                              ? ParseUint64(header_map.at("PVP_BLOCK_SIZE"))
                              : std::nullopt;
    const auto pvp_offset = header_map.contains("PVP_BLOCK_BYTE_OFFSET")
                                ? ParseUint64(header_map.at("PVP_BLOCK_BYTE_OFFSET"))
                                : std::nullopt;
    const auto signal_size = header_map.contains("SIGNAL_BLOCK_SIZE")
                                 ? ParseUint64(header_map.at("SIGNAL_BLOCK_SIZE"))
                                 : std::nullopt;
    const auto signal_offset = header_map.contains("SIGNAL_BLOCK_BYTE_OFFSET")
                                   ? ParseUint64(header_map.at("SIGNAL_BLOCK_BYTE_OFFSET"))
                                   : std::nullopt;

    if (!xml_size || !xml_offset || !pvp_size || !pvp_offset || !signal_size || !signal_offset) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "malformed_crsd:header_block_offsets:" + path.generic_string(),
        };
    }

    std::string xml(static_cast<std::size_t>(*xml_size), '\0');
    in.clear();
    in.seekg(static_cast<std::streamoff>(*xml_offset), std::ios::beg);
    if (!in.good()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "malformed_crsd:xml_seek_failed:" + path.generic_string(),
        };
    }
    in.read(xml.data(), static_cast<std::streamsize>(xml.size()));
    if (static_cast<std::size_t>(in.gcount()) != xml.size()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "malformed_crsd:xml_read_failed:" + path.generic_string(),
        };
    }

    const auto num_vectors_str = ExtractFirstTag(xml, "NumVectors");
    const auto num_samples_str = ExtractFirstTag(xml, "NumSamples");
    const auto num_bytes_pvp_str = ExtractFirstTag(xml, "NumBytesPVP");
    const auto signal_format = ExtractFirstTag(xml, "SignalArrayFormat");

    if (!num_vectors_str || !num_samples_str || !num_bytes_pvp_str || !signal_format) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:data_fields:" + path.generic_string(),
        };
    }

    const auto num_vectors = ParseUint64(*num_vectors_str);
    const auto num_samples = ParseUint64(*num_samples_str);
    const auto num_bytes_pvp = ParseUint64(*num_bytes_pvp_str);
    if (!num_vectors || !num_samples || !num_bytes_pvp ||
        *num_vectors == 0u || *num_samples == 0u || *num_bytes_pvp == 0u) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_metadata:invalid_dimensions:" + path.generic_string(),
        };
    }

    std::uint64_t bytes_per_complex_sample = 0u;
    if (*num_vectors > 0u && *num_samples > 0u) {
        const auto denom = (*num_vectors) * (*num_samples);
        if (denom > 0u && (*signal_size % denom) == 0u) {
            bytes_per_complex_sample = (*signal_size) / denom;
        }
    }
    if (bytes_per_complex_sample == 0u) {
        if (*signal_format == "CF4") {
            bytes_per_complex_sample = 4u;
        } else if (*signal_format == "CF8") {
            bytes_per_complex_sample = 8u;
        } else if (*signal_format == "CF16") {
            bytes_per_complex_sample = 16u;
        }
    }
    if (bytes_per_complex_sample != 4u &&
        bytes_per_complex_sample != 8u &&
        bytes_per_complex_sample != 16u) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "unsupported_crsd:signal_array_format:" + path.generic_string(),
        };
    }

    const auto rcv_time_offset_words = ExtractPvpOffsetWords(xml, "RcvTime");
    const auto rcv_pos_offset_words = ExtractPvpOffsetWords(xml, "RcvPos");
    const auto rcv_vel_offset_words = ExtractPvpOffsetWords(xml, "RcvVel");
    if (!rcv_time_offset_words || !rcv_pos_offset_words || !rcv_vel_offset_words) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_pvp:pvp_offsets:" + path.generic_string(),
        };
    }

    const std::uint64_t rcv_time_byte_offset = *rcv_time_offset_words * 8u;
    const std::uint64_t rcv_pos_byte_offset = *rcv_pos_offset_words * 8u;
    const std::uint64_t rcv_vel_byte_offset = *rcv_vel_offset_words * 8u;
    const std::uint64_t pvp_record_size = *num_bytes_pvp;

    if (rcv_time_byte_offset + 8u > pvp_record_size ||
        rcv_pos_byte_offset + 24u > pvp_record_size ||
        rcv_vel_byte_offset + 24u > pvp_record_size) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_required_pvp:record_layout:" + path.generic_string(),
        };
    }

    const std::uint64_t expected_pvp_size = (*num_vectors) * pvp_record_size;
    if (*pvp_size < expected_pvp_size) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "malformed_crsd:pvp_block_too_small:" + path.generic_string(),
        };
    }

    const std::uint64_t vector_signal_bytes = (*num_samples) * bytes_per_complex_sample;
    const std::uint64_t expected_signal_size = (*num_vectors) * vector_signal_bytes;
    if (*signal_size < expected_signal_size) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "malformed_crsd:signal_block_too_small:" + path.generic_string(),
        };
    }

    auto read_pvp_record = [&](std::uint64_t vector_index) -> std::optional<CrsdVectorRecord> {
        std::vector<std::uint8_t> pvp(static_cast<std::size_t>(pvp_record_size));
        in.clear();
        in.seekg(static_cast<std::streamoff>(*pvp_offset + vector_index * pvp_record_size), std::ios::beg);
        if (!in.good()) {
            return std::nullopt;
        }
        in.read(reinterpret_cast<char*>(pvp.data()), static_cast<std::streamsize>(pvp.size()));
        if (static_cast<std::size_t>(in.gcount()) != pvp.size()) {
            return std::nullopt;
        }

        CrsdVectorRecord record{};
        record.vector_index = vector_index;
        record.rcv_time_s = ReadBeF8(pvp.data() + rcv_time_byte_offset);
        record.platform_position_m = {
            ReadBeF8(pvp.data() + rcv_pos_byte_offset + 0u),
            ReadBeF8(pvp.data() + rcv_pos_byte_offset + 8u),
            ReadBeF8(pvp.data() + rcv_pos_byte_offset + 16u),
        };
        record.platform_velocity_mps = {
            ReadBeF8(pvp.data() + rcv_vel_byte_offset + 0u),
            ReadBeF8(pvp.data() + rcv_vel_byte_offset + 8u),
            ReadBeF8(pvp.data() + rcv_vel_byte_offset + 16u),
        };

        std::vector<std::uint8_t> vector_bytes(static_cast<std::size_t>(vector_signal_bytes));
        in.clear();
        in.seekg(static_cast<std::streamoff>(*signal_offset + vector_index * vector_signal_bytes), std::ios::beg);
        if (!in.good()) {
            return std::nullopt;
        }
        in.read(reinterpret_cast<char*>(vector_bytes.data()), static_cast<std::streamsize>(vector_bytes.size()));
        if (static_cast<std::size_t>(in.gcount()) != vector_bytes.size()) {
            return std::nullopt;
        }

        record.signal.reserve(static_cast<std::size_t>(*num_samples));
        if (bytes_per_complex_sample == 16u) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(*num_samples); ++i) {
                const auto offset = i * 16u;
                const auto real = static_cast<float>(ReadBeF8(vector_bytes.data() + offset + 0u));
                const auto imag = static_cast<float>(ReadBeF8(vector_bytes.data() + offset + 8u));
                record.signal.emplace_back(real, imag);
            }
        } else if (bytes_per_complex_sample == 8u) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(*num_samples); ++i) {
                const auto offset = i * 8u;
                const auto real = ReadBeF4(vector_bytes.data() + offset + 0u);
                const auto imag = ReadBeF4(vector_bytes.data() + offset + 4u);
                record.signal.emplace_back(real, imag);
            }
        } else {
            for (std::size_t i = 0; i < static_cast<std::size_t>(*num_samples); ++i) {
                const auto offset = i * 4u;
                const auto real_i16 = static_cast<std::int16_t>(
                    (static_cast<std::uint16_t>(vector_bytes[offset + 0u]) << 8u) |
                    static_cast<std::uint16_t>(vector_bytes[offset + 1u]));
                const auto imag_i16 = static_cast<std::int16_t>(
                    (static_cast<std::uint16_t>(vector_bytes[offset + 2u]) << 8u) |
                    static_cast<std::uint16_t>(vector_bytes[offset + 3u]));
                record.signal.emplace_back(
                    static_cast<float>(real_i16),
                    static_cast<float>(imag_i16));
            }
        }
        return record;
    };

    std::vector<CrsdVectorRecord> vectors;
    vectors.reserve(static_cast<std::size_t>(*num_vectors));
    for (std::uint64_t vector_index = 0u; vector_index < *num_vectors; ++vector_index) {
        const auto vector = read_pvp_record(vector_index);
        if (!vector) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "malformed_crsd:vector_read_failed:" + path.generic_string(),
            };
        }
        vectors.push_back(*vector);
    }

    std::uint64_t payload_hash = 14695981039346656037ull;
    {
        in.clear();
        in.seekg(static_cast<std::streamoff>(*signal_offset), std::ios::beg);
        if (!in.good()) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "malformed_crsd:signal_seek_failed:" + path.generic_string(),
            };
        }

        std::vector<std::uint8_t> chunk(1u << 20u);
        std::uint64_t remaining = expected_signal_size;
        while (remaining > 0u) {
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, chunk.size()));
            in.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(want));
            if (static_cast<std::size_t>(in.gcount()) != want) {
                return CrsdReadResult{
                    .success = false,
                    .diagnostic = "malformed_crsd:signal_read_failed:" + path.generic_string(),
                };
            }
            payload_hash = Fnv1a64(chunk.data(), want, payload_hash);
            remaining -= static_cast<std::uint64_t>(want);
        }
    }

    auto hash_record = [](const CrsdVectorRecord& record) {
        std::uint64_t hash = 14695981039346656037ull;
        hash = HashUInt64(record.vector_index, hash);
        hash = HashDouble(record.rcv_time_s, hash);
        for (const auto value : record.platform_position_m) {
            hash = HashDouble(value, hash);
        }
        for (const auto value : record.platform_velocity_mps) {
            hash = HashDouble(value, hash);
        }
        for (const auto& sample : record.signal) {
            hash = HashFloat(sample.real(), hash);
            hash = HashFloat(sample.imag(), hash);
        }
        return hash;
    };

    CrsdSegmentRecord segment{};
    segment.crsd_path = path;
    segment.segment_index = DeriveSegmentIndexFromPath(path).value_or(0u);
    segment.global_vector_start = 0u;
    segment.vector_count = *num_vectors;
    segment.samples_per_vector = *num_samples;
    segment.carrier_hz = 0.0;
    segment.sample_rate_hz = 0.0;
    segment.payload_hash = payload_hash;
    segment.vectors = std::move(vectors);
    segment.first_vector = segment.vectors.front();
    segment.last_vector = segment.vectors.back();
    segment.first_vector_hash = hash_record(segment.first_vector);
    segment.last_vector_hash = hash_record(segment.last_vector);

    OrderedCrsdSetReadResult out{};
    out.segments.push_back(std::move(segment));
    out.total_vector_count = out.segments.front().vector_count;
    out.ordered_set_payload_hash = out.segments.front().payload_hash;

    return CrsdReadResult{
        .success = true,
        .diagnostic = "ok:binary_crsd",
        .value = std::move(out),
    };
}

[[nodiscard]] CrsdReadResult ParseOneCrsd(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "crsd_open_failed:" + path.generic_string(),
        };
    }

    std::array<char, 16> probe{};
    in.read(probe.data(), static_cast<std::streamsize>(probe.size()));
    const auto count = static_cast<std::size_t>(in.gcount());
    const std::string prefix(probe.data(), count);

    if (StartsWith(prefix, "CRSD/")) {
        return ParseBinaryCrsd(path);
    }

    return ParseJsonHelperCrsd(path);
}

} // namespace

CrsdReadResult CrsdReader::ReadOrderedSet(const CrsdReadOptions& options) const {
    std::vector<std::filesystem::path> paths;
    std::size_t mode_count = 0u;

    if (!options.ordered_crsd_paths.empty()) {
        paths = options.ordered_crsd_paths;
        ++mode_count;
    }
    if (!options.crsd_directory.empty()) {
        paths = ResolveDirectoryPaths(options.crsd_directory);
        ++mode_count;
    }
    if (!options.manifest_path.empty()) {
        paths = ResolveManifestPaths(options.manifest_path);
        ++mode_count;
    }

    if (mode_count != 1u) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "invalid_config:exactly_one_mode_required",
        };
    }

    if (paths.empty()) {
        return CrsdReadResult{
            .success = false,
            .diagnostic = "missing_product_crsd",
        };
    }

    OrderedCrsdSetReadResult out{};
    std::unordered_set<std::uint64_t> seen_segment_indices;
    std::uint64_t expected_segment_index = 0u;
    std::uint64_t global_vector_start = 0u;

    for (std::size_t path_index = 0u; path_index < paths.size(); ++path_index) {
        const auto& path = paths[path_index];
        const auto parsed = ParseOneCrsd(path);
        if (!parsed.success) {
            return parsed;
        }

        auto segment = parsed.value.segments.front();

        if (!DeriveSegmentIndexFromPath(path).has_value()) {
            segment.segment_index = static_cast<std::uint64_t>(path_index);
        }

        if (options.require_contiguous_segment_indices &&
            expected_segment_index == 0u &&
            segment.segment_index != 0u) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "out_of_order_segment_index:" + std::to_string(segment.segment_index),
            };
        }

        if (!seen_segment_indices.insert(segment.segment_index).second) {
            return CrsdReadResult{
                .success = false,
                .diagnostic = "duplicate_segment_index:" + std::to_string(segment.segment_index),
            };
        }

        if (options.require_contiguous_segment_indices) {
            if (segment.segment_index < expected_segment_index) {
                return CrsdReadResult{
                    .success = false,
                    .diagnostic = "out_of_order_segment_index:" + std::to_string(segment.segment_index),
                };
            }
            if (segment.segment_index > expected_segment_index) {
                return CrsdReadResult{
                    .success = false,
                    .diagnostic = "missing_segment_index:" + std::to_string(expected_segment_index),
                };
            }
            ++expected_segment_index;
        }

        segment.global_vector_start = global_vector_start;
        global_vector_start += segment.vector_count;

        out.ordered_set_payload_hash = Fnv1a64(
            &segment.payload_hash,
            sizeof(segment.payload_hash),
            out.ordered_set_payload_hash);
        out.total_vector_count += segment.vector_count;
        out.segments.push_back(std::move(segment));
    }

    return CrsdReadResult{
        .success = true,
        .diagnostic = "ok",
        .value = std::move(out),
    };
}

} // namespace graphx::sar
