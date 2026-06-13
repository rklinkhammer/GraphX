#pragma once

#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarIoUtilities.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace graphx::sar {

struct CrsdWriterOptions {
    std::vector<std::string> assumptions{
        "standards_targeted_crsd_metadata",
        "standards_targeted_signal_array",
        "standards_targeted_pvp",
    };
    std::vector<std::string> warnings{};
};

class CrsdWriter final : public ISarWriter {
public:
    explicit CrsdWriter(CrsdWriterOptions options = {})
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
        const auto pvp_path = output_directory / kPvpFile;
        const auto provenance_path = output_directory / kProvenanceFile;
        const auto chunk_index_path = output_directory / kChunkIndexFile;

        std::ofstream signal_stream{signal_path, std::ios::binary | std::ios::trunc};
        if (!signal_stream) {
            return SarWriteResult{
                .success = false,
                .message = "signal_open_failed",
            };
        }

        std::vector<SarIndexEntry> signal_entries{};
        signal_entries.reserve(product.Shape().pulse_count * product.Shape().channel_count);

        nlohmann::json channels_metadata = nlohmann::json::array();
        nlohmann::json pvp_channels = nlohmann::json::array();

        std::uint64_t checksum_state = SarIoUtilities::kFNVOffsetBasis;
        std::uint64_t byte_offset = 0;

        for (std::size_t channel_index = 0; channel_index < product.channels.size(); ++channel_index) {
            const auto& channel = product.channels[channel_index];

            nlohmann::json channel_json{
                {"channel_index", channel_index},
                {"channel_id", channel.channel_id},
                {"waveform", WaveformToJson(channel.waveform)},
                {"pulse_count", channel.pulses.size()},
            };

            nlohmann::json channel_pvp{
                {"channel_index", channel_index},
                {"channel_id", channel.channel_id},
                {"vectors", nlohmann::json::array()},
            };

            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                const auto& pulse = channel.pulses[pulse_index];
                const auto sample_count = pulse.samples.size();

                for (const auto& sample : pulse.samples) {
                    signal_stream.write(reinterpret_cast<const char*>(&sample.real), sizeof(sample.real));
                    signal_stream.write(reinterpret_cast<const char*>(&sample.imag), sizeof(sample.imag));
                    checksum_state = SarIoUtilities::UpdateFNV1a(checksum_state, sample.real);
                    checksum_state = SarIoUtilities::UpdateFNV1a(checksum_state, sample.imag);
                }

                signal_entries.push_back(SarIndexEntry{
                    .channel_index = channel_index,
                    .pulse_index = pulse_index,
                    .byte_offset = byte_offset,
                    .sample_count = sample_count,
                });

                const auto bytes_written = static_cast<std::uint64_t>(sample_count * 2U * sizeof(float));
                byte_offset += bytes_written;

                channel_pvp["vectors"].push_back(nlohmann::json{
                    {"vector_index", pulse.parameters.vector_index},
                    {"time_seconds", pulse.parameters.time_seconds},
                    {"range_sample_start", pulse.parameters.range_sample_start},
                    {"platform_position_m", pulse.parameters.platform.position_m},
                    {"platform_velocity_mps", pulse.parameters.platform.velocity_mps},
                    {"sample_count", sample_count},
                });
            }

            channels_metadata.push_back(std::move(channel_json));
            pvp_channels.push_back(std::move(channel_pvp));
        }

        signal_stream.flush();
        if (!signal_stream.good()) {
            return SarWriteResult{
                .success = false,
                .message = "signal_write_failed",
            };
        }

        const auto shape = product.Shape();
        const auto checksum_hex = SarIoUtilities::ToHex(checksum_state);

        const auto metadata = nlohmann::json{
            {"schema", kMetadataSchema},
            {"format", kFormatName},
            {"label", kStandardsTargetedLabel},
            {"signal_file", kSignalFile},
            {"shape", nlohmann::json{
                          {"channel_count", shape.channel_count},
                          {"pulse_count", shape.pulse_count},
                          {"max_sample_count", shape.max_sample_count},
                      }},
            {"signal_array", nlohmann::json{
                                 {"sample_type", "complex_f32"},
                                 {"endianness", "little"},
                                 {"layout", "pulse-major/channel-major/interleaved-ri"},
                             }},
            {"collection", CollectionToJson(product.collection)},
            {"channels", channels_metadata},
        };

        const auto pvp = nlohmann::json{
            {"schema", kPvpSchema},
            {"format", kFormatName},
            {"channels", pvp_channels},
        };

        const auto provenance = nlohmann::json{
            {"schema", kProvenanceSchema},
            {"collection_id", product.collection.collection_id},
            {"provenance_label", product.collection.provenance_label},
            {"source_ordering", product.collection.source_ordering},
            {"source_files", product.collection.source_files},
        };

        const auto chunk_index = nlohmann::json{
            {"schema", kChunkIndexSchema},
            {"format", kFormatName},
            {"signal_file", kSignalFile},
            {"signal_checksum_fnv1a64", checksum_hex},
            {"pulse_range", nlohmann::json{
                                {"start", shape.pulse_count == 0 ? 0 : 0},
                                {"end", shape.pulse_count == 0 ? 0 : shape.pulse_count - 1},
                            }},
            {"entries", BuildEntriesJson(signal_entries)},
        };

        if (!SarIoUtilities::WriteJson(metadata_path, metadata) ||
            !SarIoUtilities::WriteJson(pvp_path, pvp) ||
            !SarIoUtilities::WriteJson(provenance_path, provenance) ||
            !SarIoUtilities::WriteJson(chunk_index_path, chunk_index)) {
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

    static constexpr const char* kFormatName = "crsd";
    static constexpr const char* kStandardsTargetedLabel = "STANDARDS-TARGETED";
    static constexpr const char* kSignalFile = "signal.bin";
    static constexpr const char* kMetadataFile = "metadata.json";
    static constexpr const char* kPvpFile = "pvp.json";
    static constexpr const char* kProvenanceFile = "provenance.json";
    static constexpr const char* kChunkIndexFile = "chunk_index.json";
    static constexpr const char* kMetadataSchema = "graphx.sar.crsd.metadata.v1";
    static constexpr const char* kPvpSchema = "graphx.sar.crsd.pvp.v1";
    static constexpr const char* kProvenanceSchema = "graphx.sar.crsd.provenance.v1";
    static constexpr const char* kChunkIndexSchema = "graphx.sar.crsd.chunk_index.v1";

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

    [[nodiscard]] static nlohmann::json BuildEntriesJson(const std::vector<SarIndexEntry>& entries) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& entry : entries) {
            result.push_back(nlohmann::json{
                {"channel_index", entry.channel_index},
                {"pulse_index", entry.pulse_index},
                {"byte_offset", entry.byte_offset},
                {"sample_count", entry.sample_count},
            });
        }
        return result;
    }

    CrsdWriterOptions options_{};
};

} // namespace graphx::sar
