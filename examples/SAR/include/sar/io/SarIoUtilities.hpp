#pragma once

#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

struct SarIndexEntry {
    std::size_t channel_index{0};
    std::size_t pulse_index{0};
    std::uint64_t byte_offset{0};
    std::size_t sample_count{0};
};

struct SarOutputSummary {
    std::string output_name{};
    std::string checksum_fnv1a64{};
    std::size_t pulse_start{0};
    std::size_t pulse_end{0};
    std::size_t pulse_count{0};
    std::size_t channel_count{0};
    std::size_t max_sample_count{0};
};

struct GotchaOutputIndexBuildInput {
    std::string schema{};
    std::string collection_id{};
    std::vector<std::string> source_files{};
    std::string source_ordering{};
    std::string provenance{};
    std::vector<SarOutputSummary> outputs{};
    std::vector<double> frequency_axis_hz{};
    std::vector<std::string> assumptions{};
    std::vector<std::string> warnings{};
};

struct ConversionReportBuildInput {
    std::string format{};
    std::string label{};
    std::string selected_mode{};
    std::string validation_status{"ok"};
    std::string provenance{};
    std::string source_ordering{};
    std::vector<std::string> assumptions{};
    std::vector<std::string> warnings{};
    std::vector<SarOutputSummary> outputs{};
    std::string metadata_file{};
    std::string index_file{};
};

class SarIoUtilities {
public:
    static constexpr std::uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
    static constexpr std::uint64_t kFNVPrime = 1099511628211ULL;

    [[nodiscard]] static nlohmann::json BuildSarPackageIndexJson(
        const std::string& schema,
        const std::string& format,
        const std::string& label,
        const std::string& signal_file,
        const std::string& checksum_hex,
        const std::vector<SarIndexEntry>& entries) {
        nlohmann::json entries_json = nlohmann::json::array();
        for (const auto& entry : entries) {
            entries_json.push_back(nlohmann::json{
                {"channel_index", entry.channel_index},
                {"pulse_index", entry.pulse_index},
                {"byte_offset", entry.byte_offset},
                {"sample_count", entry.sample_count},
            });
        }

        return nlohmann::json{
            {"schema", schema},
            {"format", format},
            {"label", label},
            {"signal_file", signal_file},
            {"signal_checksum_fnv1a64", checksum_hex},
            {"entries", std::move(entries_json)},
        };
    }

    [[nodiscard]] static nlohmann::json BuildGotchaOutputIndexJson(
        const GotchaOutputIndexBuildInput& input) {
        nlohmann::json outputs_json = nlohmann::json::array();
        for (const auto& output : input.outputs) {
            outputs_json.push_back(nlohmann::json{
                {"output_name", output.output_name},
                {"checksum_fnv1a64", output.checksum_fnv1a64},
                {"pulse_range", nlohmann::json{{"start", output.pulse_start}, {"end", output.pulse_end}}},
                {"sample_shape", nlohmann::json{
                                     {"pulse_count", output.pulse_count},
                                     {"channel_count", output.channel_count},
                                     {"max_sample_count", output.max_sample_count},
                                 }},
            });
        }

        return nlohmann::json{
            {"schema", input.schema},
            {"collection_id", input.collection_id},
            {"source_files", input.source_files},
            {"source_ordering", input.source_ordering},
            {"provenance", input.provenance},
            {"outputs", std::move(outputs_json)},
            {"frequency_metadata", nlohmann::json{{"frequency_axis_hz", input.frequency_axis_hz}}},
            {"assumptions", input.assumptions},
            {"warnings", input.warnings},
        };
    }

    [[nodiscard]] static nlohmann::json BuildConversionReportJson(
        const ConversionReportBuildInput& input) {
        nlohmann::json outputs_json = nlohmann::json::array();
        for (const auto& output : input.outputs) {
            outputs_json.push_back(nlohmann::json{
                {"output_name", output.output_name},
                {"checksum_fnv1a64", output.checksum_fnv1a64},
                {"pulse_range", nlohmann::json{{"start", output.pulse_start}, {"end", output.pulse_end}}},
                {"sample_shape", nlohmann::json{
                                     {"pulse_count", output.pulse_count},
                                     {"channel_count", output.channel_count},
                                     {"max_sample_count", output.max_sample_count},
                                 }},
            });
        }

        return nlohmann::json{
            {"schema", "graphx.sar.conversion_report.v1"},
            {"format", input.format},
            {"label", input.label},
            {"selected_mode", input.selected_mode},
            {"validation_status", input.validation_status},
            {"provenance", input.provenance},
            {"source_ordering", input.source_ordering},
            {"assumptions", input.assumptions},
            {"warnings", input.warnings},
            {"outputs", std::move(outputs_json)},
            {"metadata_file", input.metadata_file},
            {"index_file", input.index_file},
        };
    }

    [[nodiscard]] static bool WriteJson(
        const std::filesystem::path& path,
        const nlohmann::json& value) {
        std::ofstream stream{path, std::ios::trunc};
        if (!stream) {
            return false;
        }
        stream << value.dump(2) << '\n';
        return stream.good();
    }

    [[nodiscard]] static bool WriteWarningsLog(
        const std::filesystem::path& path,
        const std::vector<std::string>& warnings) {
        std::ofstream stream{path, std::ios::trunc};
        if (!stream) {
            return false;
        }

        if (warnings.empty()) {
            stream << "none\n";
            return stream.good();
        }

        for (const auto& warning : warnings) {
            stream << warning << '\n';
        }
        return stream.good();
    }

    template <typename T>
    [[nodiscard]] static std::uint64_t UpdateFNV1a(std::uint64_t state, const T& value) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            state ^= bytes[i];
            state *= kFNVPrime;
        }
        return state;
    }

    [[nodiscard]] static std::string ToHex(std::uint64_t value) {
        std::ostringstream oss;
        oss << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
        return oss.str();
    }

    [[nodiscard]] static std::string ComputeFileChecksumFNV1a64(
        const std::filesystem::path& file_path) {
        std::ifstream stream{file_path, std::ios::binary};
        if (!stream) {
            return {};
        }

        std::uint64_t state = kFNVOffsetBasis;
        std::array<char, 4096> buffer{};
        while (stream.good()) {
            stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = stream.gcount();
            if (count <= 0) {
                break;
            }
            for (std::streamsize i = 0; i < count; ++i) {
                state ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
                state *= kFNVPrime;
            }
        }

        return ToHex(state);
    }
};

} // namespace graphx::sar
