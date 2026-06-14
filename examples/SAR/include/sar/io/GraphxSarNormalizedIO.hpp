#pragma once

#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarIoUtilities.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

struct GraphxSarNormalizedOptions {
    std::vector<std::string> assumptions{
        "non_standard_intermediate_format",
        "derived_from_normalized_sar_product",
    };
    std::vector<std::string> warnings{};
};

class GraphxSarNormalizedWriter final : public ISarWriter {
public:
    explicit GraphxSarNormalizedWriter(GraphxSarNormalizedOptions options = {})
        : options_(std::move(options)) {}

    [[nodiscard]] SarWriteResult Write(
        const std::filesystem::path& output_directory,
        const NormalizedSarProduct& product) const override {
        if (!product.HasRequiredFields()) {
            return SarWriteResult{
                .success = false,
                .message = "missing_required_fields",
            };
        }

        std::error_code fs_error{};
        std::filesystem::create_directories(output_directory, fs_error);
        if (fs_error) {
            return SarWriteResult{
                .success = false,
                .message = "output_directory_error",
            };
        }

        const auto signal_path = output_directory / kSignalFile;
        const auto metadata_path = output_directory / kMetadataFile;
        const auto index_path = output_directory / kIndexFile;
        const auto report_path = output_directory / kConversionReportFile;

        std::ofstream signal_stream{signal_path, std::ios::binary | std::ios::trunc};
        if (!signal_stream) {
            return SarWriteResult{
                .success = false,
                .message = "signal_open_failed",
            };
        }

        std::uint64_t checksum_state = SarIoUtilities::kFNVOffsetBasis;
        std::uint64_t offset_bytes = 0;

        std::vector<SarIndexEntry> entries{};
        nlohmann::json channels = nlohmann::json::array();

        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];
            nlohmann::json channel_json{
                {"channel_index", channel_index},
                {"channel_id", channel.channel_id},
                {"waveform", WaveformToJson(channel.waveform)},
                {"pulse_count", channel.pulses.size()},
                {"pulses", nlohmann::json::array()},
            };

            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                const auto& pulse = channel.pulses[pulse_index];
                const auto sample_count = pulse.samples.size();
                const auto bytes_written = sample_count * 2U * sizeof(float);

                for (const auto& sample : pulse.samples) {
                    signal_stream.write(reinterpret_cast<const char*>(&sample.real), sizeof(sample.real));
                    signal_stream.write(reinterpret_cast<const char*>(&sample.imag), sizeof(sample.imag));
                    checksum_state = SarIoUtilities::UpdateFNV1a(checksum_state, sample.real);
                    checksum_state = SarIoUtilities::UpdateFNV1a(checksum_state, sample.imag);
                }

                entries.push_back(SarIndexEntry{
                    .channel_index = channel_index,
                    .pulse_index = pulse_index,
                    .byte_offset = offset_bytes,
                    .sample_count = sample_count,
                });

                channel_json["pulses"].push_back(nlohmann::json{
                    {"pulse_index", pulse_index},
                    {"vector_index", pulse.parameters.vector_index},
                    {"time_seconds", pulse.parameters.time_seconds},
                    {"range_sample_start", pulse.parameters.range_sample_start},
                    {"platform_position_m", pulse.parameters.platform.position_m},
                    {"platform_velocity_mps", pulse.parameters.platform.velocity_mps},
                    {"sample_count", sample_count},
                });

                offset_bytes += static_cast<std::uint64_t>(bytes_written);
            }

            channels.push_back(std::move(channel_json));
        }

        signal_stream.flush();
        if (!signal_stream.good()) {
            return SarWriteResult{
                .success = false,
                .message = "signal_write_failed",
            };
        }

        const auto checksum_hex = SarIoUtilities::ToHex(checksum_state);
        const auto shape = product.Shape();

        const nlohmann::json metadata{
            {"schema", kMetadataSchema},
            {"format", kFormatName},
            {"label", kNonStandardLabel},
            {"signal_file", kSignalFile},
            {"shape", nlohmann::json{
                          {"channel_count", shape.channel_count},
                          {"pulse_count", shape.pulse_count},
                          {"max_sample_count", shape.max_sample_count},
                      }},
            {"collection", CollectionToJson(product.collection)},
            {"channels", channels},
        };

        const auto index = SarIoUtilities::BuildSarPackageIndexJson(
            kIndexSchema,
            kFormatName,
            kNonStandardLabel,
            kSignalFile,
            checksum_hex,
            entries);

        const auto report = SarIoUtilities::BuildConversionReportJson(ConversionReportBuildInput{
            .format = kFormatName,
            .label = kNonStandardLabel,
            .selected_mode = kFormatName,
            .validation_status = "ok",
            .provenance = product.collection.provenance_label,
            .source_ordering = product.collection.source_ordering,
            .assumptions = options_.assumptions,
            .warnings = options_.warnings,
            .outputs = {
                SarOutputSummary{
                    .output_name = kSignalFile,
                    .checksum_fnv1a64 = checksum_hex,
                    .pulse_start = 0U,
                    .pulse_end = shape.pulse_count == 0 ? 0U : shape.pulse_count - 1,
                    .pulse_count = shape.pulse_count,
                    .channel_count = shape.channel_count,
                    .max_sample_count = shape.max_sample_count,
                },
            },
            .metadata_file = kMetadataFile,
            .index_file = kIndexFile,
        });

        const auto warnings_path = output_directory / kWarningsLogFile;
        if (!SarIoUtilities::WriteJson(metadata_path, metadata) ||
            !SarIoUtilities::WriteJson(index_path, index) ||
            !SarIoUtilities::WriteJson(report_path, report) ||
            !SarIoUtilities::WriteWarningsLog(warnings_path, options_.warnings)) {
            return SarWriteResult{
                .success = false,
                .message = "metadata_write_failed",
            };
        }

        return SarWriteResult{
            .success = true,
            .message = "ok",
        };
    }

    [[nodiscard]] static std::string ComputeSignalChecksum(
        const std::filesystem::path& signal_path) {
        return SarIoUtilities::ComputeFileChecksumFNV1a64(signal_path);
    }

    static constexpr const char* kFormatName = "graphx-sar-normalized";
    static constexpr const char* kNonStandardLabel = "NON-STANDARD";
    static constexpr const char* kSignalFile = "signal.bin";
    static constexpr const char* kMetadataFile = "metadata.json";
    static constexpr const char* kIndexFile = "index.json";
    static constexpr const char* kConversionReportFile = "conversion_report.json";
    static constexpr const char* kWarningsLogFile = "conversion_warnings.log";
    static constexpr const char* kMetadataSchema = "graphx.sar.graphx_sar_normalized.metadata.v1";
    static constexpr const char* kIndexSchema = "graphx.sar.graphx_sar_normalized.index.v1";

private:
    [[nodiscard]] static nlohmann::json WaveformToJson(const WaveformMetadata& waveform) {
        return nlohmann::json{
            {"waveform_id", waveform.waveform_id},
            {"carrier_hz", waveform.carrier_hz},
            {"bandwidth_hz", waveform.bandwidth_hz},
            {"sample_rate_hz", waveform.sample_rate_hz},
            {"sample_type", waveform.sample_type},
            {"polarization", waveform.polarization},
            {"frequency_axis_hz", waveform.frequency_axis_hz},
        };
    }

    [[nodiscard]] static nlohmann::json CollectionToJson(const CollectionMetadata& collection) {
        return nlohmann::json{
            {"product_id", collection.product_id},
            {"collector_name", collection.collector_name},
            {"collection_id", collection.collection_id},
            {"coordinate_frame", collection.coordinate_frame},
            {"time_basis", collection.time_basis},
            {"source_files", collection.source_files},
            {"provenance_label", collection.provenance_label},
            {"source_ordering", collection.source_ordering},
        };
    }

    GraphxSarNormalizedOptions options_{};
};

class GraphxSarNormalizedReader final : public ISarReader {
public:
    [[nodiscard]] SarReadResult Read(const std::filesystem::path& input_directory) const override {
        const auto metadata_path = input_directory / GraphxSarNormalizedWriter::kMetadataFile;
        const auto index_path = input_directory / GraphxSarNormalizedWriter::kIndexFile;
        const auto report_path = input_directory / GraphxSarNormalizedWriter::kConversionReportFile;
        const auto signal_path = input_directory / GraphxSarNormalizedWriter::kSignalFile;

        const auto metadata = ReadJson(metadata_path);
        const auto index = ReadJson(index_path);
        const auto report = ReadJson(report_path);
        if (!metadata.has_value() || !index.has_value() || !report.has_value()) {
            return SarReadResult{
                .success = false,
                .message = "sar_normalized_metadata_missing_or_invalid",
            };
        }

        if (!HasRequiredLabel(*metadata) || !HasRequiredLabel(*index) || !HasRequiredLabel(*report)) {
            return SarReadResult{
                .success = false,
                .message = "sar_normalized_non_standard_label_missing",
            };
        }

        const auto checksum = GraphxSarNormalizedWriter::ComputeSignalChecksum(signal_path);
        if (checksum.empty()) {
            return SarReadResult{
                .success = false,
                .message = "signal_missing",
            };
        }
        if (!index->contains("signal_checksum_fnv1a64") ||
            index->at("signal_checksum_fnv1a64").get<std::string>() != checksum) {
            return SarReadResult{
                .success = false,
                .message = "signal_checksum_mismatch",
            };
        }

        auto product = NormalizedSarProduct{};
        if (!ParseCollection(*metadata, product.collection)) {
            return SarReadResult{
                .success = false,
                .message = "collection_metadata_invalid",
            };
        }

        if (!metadata->contains("channels") || !metadata->at("channels").is_array()) {
            return SarReadResult{
                .success = false,
                .message = "channels_metadata_invalid",
            };
        }

        if (!index->contains("entries") || !index->at("entries").is_array()) {
            return SarReadResult{
                .success = false,
                .message = "index_entries_invalid",
            };
        }

        std::unordered_map<std::string, nlohmann::json> entry_by_key{};
        for (const auto& entry : index->at("entries")) {
            if (!entry.contains("channel_index") || !entry.contains("pulse_index")) {
                return SarReadResult{.success = false, .message = "index_entries_invalid"};
            }
            const auto key = MakeEntryKey(
                entry.at("channel_index").get<std::size_t>(),
                entry.at("pulse_index").get<std::size_t>());
            entry_by_key.emplace(key, entry);
        }

        std::ifstream signal_stream{signal_path, std::ios::binary};
        if (!signal_stream) {
            return SarReadResult{
                .success = false,
                .message = "signal_open_failed",
            };
        }

        for (const auto& channel_json : metadata->at("channels")) {
            ChannelSignal channel{};
            std::size_t channel_index = 0;
            if (!ParseChannel(channel_json, channel, channel_index)) {
                return SarReadResult{.success = false, .message = "channels_metadata_invalid"};
            }

            for (const auto& pulse_json : channel_json.at("pulses")) {
                const auto pulse_index = pulse_json.at("pulse_index").get<std::size_t>();
                const auto key = MakeEntryKey(channel_index, pulse_index);
                const auto entry_it = entry_by_key.find(key);
                if (entry_it == entry_by_key.end()) {
                    return SarReadResult{.success = false, .message = "index_entry_missing"};
                }

                PulseVector pulse{};
                if (!ParsePulseMetadata(pulse_json, pulse.parameters)) {
                    return SarReadResult{.success = false, .message = "pulse_metadata_invalid"};
                }

                if (!LoadSamples(signal_stream, entry_it->second, pulse.samples)) {
                    return SarReadResult{.success = false, .message = "signal_read_failed"};
                }

                channel.pulses.push_back(std::move(pulse));
            }

            product.channels.push_back(std::move(channel));
        }

        return SarReadResult{
            .success = true,
            .message = "ok",
            .product = std::move(product),
        };
    }

private:
    [[nodiscard]] static std::optional<nlohmann::json> ReadJson(const std::filesystem::path& path) {
        std::ifstream stream{path};
        if (!stream) {
            return std::nullopt;
        }
        try {
            nlohmann::json value{};
            stream >> value;
            if (!value.is_object()) {
                return std::nullopt;
            }
            return value;
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
    }

    [[nodiscard]] static bool HasRequiredLabel(const nlohmann::json& json) {
        return json.contains("format") && json.at("format").is_string() &&
               json.at("format").get<std::string>() == GraphxSarNormalizedWriter::kFormatName &&
               json.contains("label") && json.at("label").is_string() &&
               json.at("label").get<std::string>() == GraphxSarNormalizedWriter::kNonStandardLabel;
    }

    [[nodiscard]] static bool ParseCollection(
        const nlohmann::json& metadata,
        CollectionMetadata& collection) {
        if (!metadata.contains("collection") || !metadata.at("collection").is_object()) {
            return false;
        }
        const auto& in = metadata.at("collection");
        if (!RequireString(in, "product_id", collection.product_id) ||
            !RequireString(in, "collector_name", collection.collector_name) ||
            !RequireString(in, "collection_id", collection.collection_id) ||
            !RequireString(in, "coordinate_frame", collection.coordinate_frame) ||
            !RequireString(in, "time_basis", collection.time_basis) ||
            !RequireString(in, "provenance_label", collection.provenance_label) ||
            !RequireString(in, "source_ordering", collection.source_ordering)) {
            return false;
        }

        if (!in.contains("source_files") || !in.at("source_files").is_array()) {
            return false;
        }
        collection.source_files.clear();
        for (const auto& source : in.at("source_files")) {
            if (!source.is_string()) {
                return false;
            }
            collection.source_files.push_back(source.get<std::string>());
        }

        return true;
    }

    [[nodiscard]] static bool ParseChannel(
        const nlohmann::json& channel_json,
        ChannelSignal& channel,
        std::size_t& channel_index) {
        if (!channel_json.is_object() || !channel_json.contains("channel_index") ||
            !channel_json.contains("channel_id") || !channel_json.contains("waveform") ||
            !channel_json.contains("pulses")) {
            return false;
        }

        channel_index = channel_json.at("channel_index").get<std::size_t>();
        channel.channel_id = channel_json.at("channel_id").get<std::string>();
        if (!ParseWaveform(channel_json.at("waveform"), channel.waveform)) {
            return false;
        }
        if (!channel_json.at("pulses").is_array()) {
            return false;
        }
        return true;
    }

    [[nodiscard]] static bool ParseWaveform(
        const nlohmann::json& waveform_json,
        WaveformMetadata& waveform) {
        if (!waveform_json.is_object()) {
            return false;
        }
        if (!RequireString(waveform_json, "waveform_id", waveform.waveform_id) ||
            !RequireString(waveform_json, "sample_type", waveform.sample_type) ||
            !RequireString(waveform_json, "polarization", waveform.polarization)) {
            return false;
        }
        if (!RequireNumber(waveform_json, "carrier_hz", waveform.carrier_hz) ||
            !RequireNumber(waveform_json, "bandwidth_hz", waveform.bandwidth_hz) ||
            !RequireNumber(waveform_json, "sample_rate_hz", waveform.sample_rate_hz)) {
            return false;
        }

        waveform.frequency_axis_hz.clear();
        if (!waveform_json.contains("frequency_axis_hz") || !waveform_json.at("frequency_axis_hz").is_array()) {
            return false;
        }
        for (const auto& v : waveform_json.at("frequency_axis_hz")) {
            if (!v.is_number()) {
                return false;
            }
            waveform.frequency_axis_hz.push_back(v.get<double>());
        }

        return true;
    }

    [[nodiscard]] static bool ParsePulseMetadata(
        const nlohmann::json& pulse_json,
        PerVectorParameters& params) {
        if (!pulse_json.is_object()) {
            return false;
        }
        if (!pulse_json.contains("vector_index") || !pulse_json.contains("time_seconds") ||
            !pulse_json.contains("range_sample_start") || !pulse_json.contains("platform_position_m") ||
            !pulse_json.contains("platform_velocity_mps")) {
            return false;
        }

        params.vector_index = pulse_json.at("vector_index").get<std::uint64_t>();
        params.time_seconds = pulse_json.at("time_seconds").get<double>();
        params.range_sample_start = pulse_json.at("range_sample_start").get<std::uint64_t>();

        if (!ParseArray3(pulse_json.at("platform_position_m"), params.platform.position_m) ||
            !ParseArray3(pulse_json.at("platform_velocity_mps"), params.platform.velocity_mps)) {
            return false;
        }

        return true;
    }

    [[nodiscard]] static bool LoadSamples(
        std::ifstream& signal_stream,
        const nlohmann::json& entry,
        std::vector<ComplexSample>& out) {
        if (!entry.contains("byte_offset") || !entry.contains("sample_count")) {
            return false;
        }
        const auto offset = entry.at("byte_offset").get<std::uint64_t>();
        const auto sample_count = entry.at("sample_count").get<std::size_t>();

        signal_stream.clear();
        signal_stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!signal_stream.good()) {
            return false;
        }

        out.clear();
        out.reserve(sample_count);
        for (std::size_t i = 0; i < sample_count; ++i) {
            float real = 0.0f;
            float imag = 0.0f;
            signal_stream.read(reinterpret_cast<char*>(&real), sizeof(real));
            signal_stream.read(reinterpret_cast<char*>(&imag), sizeof(imag));
            if (!signal_stream.good()) {
                return false;
            }
            out.push_back(ComplexSample{.real = real, .imag = imag});
        }

        return true;
    }

    [[nodiscard]] static std::string MakeEntryKey(std::size_t channel_index, std::size_t pulse_index) {
        return std::to_string(channel_index) + ":" + std::to_string(pulse_index);
    }

    [[nodiscard]] static bool ParseArray3(
        const nlohmann::json& in,
        std::array<double, 3>& out) {
        if (!in.is_array() || in.size() != 3 || !in.at(0).is_number() || !in.at(1).is_number() ||
            !in.at(2).is_number()) {
            return false;
        }
        out[0] = in.at(0).get<double>();
        out[1] = in.at(1).get<double>();
        out[2] = in.at(2).get<double>();
        return true;
    }

    [[nodiscard]] static bool RequireString(
        const nlohmann::json& in,
        const char* key,
        std::string& out) {
        if (!in.contains(key) || !in.at(key).is_string()) {
            return false;
        }
        out = in.at(key).get<std::string>();
        return true;
    }

    template <typename T>
    [[nodiscard]] static bool RequireNumber(
        const nlohmann::json& in,
        const char* key,
        T& out) {
        if (!in.contains(key) || !in.at(key).is_number()) {
            return false;
        }
        out = in.at(key).get<T>();
        return true;
    }
};

} // namespace graphx::sar
