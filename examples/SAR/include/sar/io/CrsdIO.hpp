#pragma once

#include "sar/io/NormalizedSarProduct.hpp"
#include "sar/io/SarIoUtilities.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    bool emit_progress{false};
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
        if (product.channels.size() != 1U) {
            return SarWriteResult{
                .success = false,
                .message = "unsupported_product:crsd_writer_supports_single_channel",
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

        const auto handoff_path = output_directory / kHandoffFile;
        const auto crsd_path = output_directory / kSignalFile;
        const auto metadata_path = output_directory / kMetadataFile;
        const auto pvp_path = output_directory / kPvpFile;
        const auto provenance_path = output_directory / kProvenanceFile;
        const auto chunk_index_path = output_directory / kChunkIndexFile;

        if (options_.emit_progress) {
            std::cerr << "info: crsd_writer: writing handoff "
                      << handoff_path.generic_string() << '\n';
        }
        const auto handoff_json = ProductToJson(product, options_.emit_progress);
        if (options_.emit_progress) {
            std::cerr << "info: crsd_writer: flushing handoff "
                      << handoff_path.generic_string() << '\n';
        }
        if (!SarIoUtilities::WriteJson(handoff_path, handoff_json)) {
            return SarWriteResult{
                .success = false,
                .message = "handoff_write_failed",
            };
        }

        const auto writer_path = ResolveWriterPath();
        if (writer_path.empty() || !std::filesystem::exists(writer_path)) {
            return SarWriteResult{
                .success = false,
                .message = kUnavailableMessage,
            };
        }

        const auto command =
            std::string{"python3 "} + ShellQuote(writer_path) +
            " --input-json " + ShellQuote(handoff_path) +
            " --output-crsd " + ShellQuote(crsd_path) +
            " --metadata-json " + ShellQuote(metadata_path) +
            " --pvp-json " + ShellQuote(pvp_path) +
            " --provenance-json " + ShellQuote(provenance_path) +
            " --chunk-index-json " + ShellQuote(chunk_index_path);
        if (options_.emit_progress) {
            std::cerr << "info: crsd_writer: invoking SarPy writer for "
                      << crsd_path.generic_string() << '\n';
        }
        if (std::system(command.c_str()) != 0) {
            std::filesystem::remove(crsd_path, fs_error);
            return SarWriteResult{
                .success = false,
                .message = "crsd_writer_failed",
            };
        }
        if (options_.emit_progress) {
            std::cerr << "info: crsd_writer: SarPy writer finished for "
                      << crsd_path.generic_string() << '\n';
        }

        if (!std::filesystem::exists(crsd_path) ||
            !std::filesystem::exists(metadata_path) ||
            !std::filesystem::exists(pvp_path) ||
            !std::filesystem::exists(provenance_path) ||
            !std::filesystem::exists(chunk_index_path)) {
            return SarWriteResult{
                .success = false,
                .message = "crsd_writer_missing_outputs",
            };
        }

        std::filesystem::remove(handoff_path, fs_error);
        return SarWriteResult{.success = true, .message = "ok"};
    }

    [[nodiscard]] static std::string ComputeSignalChecksum(
        const std::filesystem::path& signal_path) {
        return SarIoUtilities::ComputeFileChecksumFNV1a64(signal_path);
    }

    static constexpr const char* kFormatName = "crsd";
    static constexpr const char* kStandardsTargetedLabel = "STANDARDS-TARGETED";
    static constexpr const char* kSignalFile = "product.crsd";
    static constexpr const char* kHandoffFile = "normalized_product.handoff.json";
    static constexpr const char* kMetadataFile = "metadata.json";
    static constexpr const char* kPvpFile = "pvp.json";
    static constexpr const char* kProvenanceFile = "provenance.json";
    static constexpr const char* kChunkIndexFile = "chunk_index.json";
    static constexpr const char* kMetadataSchema = "graphx.sar.crsd.metadata.v1";
    static constexpr const char* kPvpSchema = "graphx.sar.crsd.pvp.v1";
    static constexpr const char* kProvenanceSchema = "graphx.sar.crsd.provenance.v1";
    static constexpr const char* kChunkIndexSchema = "graphx.sar.crsd.chunk_index.v1";
    static constexpr const char* kUnavailableMessage =
        "crsd_writer_unavailable:tools/sarpy/write_crsd_from_graphx_product.py_not_found";

private:
    [[nodiscard]] static std::filesystem::path ResolveWriterPath() {
        if (const char* override_path = std::getenv("GRAPHX_SAR_CRSD_WRITER")) {
            if (std::string{override_path}.empty()) {
                return {};
            }
            return std::filesystem::path{override_path};
        }
#ifdef GRAPHX_SOURCE_ROOT
        return std::filesystem::path{GRAPHX_SOURCE_ROOT} / "tools/sarpy/write_crsd_from_graphx_product.py";
#else
        return std::filesystem::path{"tools/sarpy/write_crsd_from_graphx_product.py"};
#endif
    }

    [[nodiscard]] static std::string ShellQuote(const std::filesystem::path& path) {
        const std::string raw = path.generic_string();
        std::string quoted{"'"};
        for (const char ch : raw) {
            if (ch == '\'') {
                quoted += "'\\''";
            } else {
                quoted += ch;
            }
        }
        quoted += "'";
        return quoted;
    }

    [[nodiscard]] static nlohmann::json ProductToJson(
        const NormalizedSarProduct& product,
        bool emit_progress) {
        nlohmann::json channels = nlohmann::json::array();
        for (const auto& channel : product.channels) {
            nlohmann::json pulses = nlohmann::json::array();
            const auto progress_interval =
                std::max<std::size_t>(1U, channel.pulses.size() / 20U);
            for (std::size_t pulse_index = 0; pulse_index < channel.pulses.size(); ++pulse_index) {
                const auto& pulse = channel.pulses[pulse_index];
                if (emit_progress &&
                    (pulse_index == 0U ||
                     pulse_index + 1U == channel.pulses.size() ||
                     pulse_index % progress_interval == 0U)) {
                    std::cerr << "info: crsd_writer: serializing pulse "
                              << (pulse_index + 1U) << "/" << channel.pulses.size()
                              << " for channel " << channel.channel_id << '\n';
                }
                nlohmann::json samples = nlohmann::json::array();
                for (const auto& sample : pulse.samples) {
                    samples.push_back(nlohmann::json{
                        {"real", sample.real},
                        {"imag", sample.imag},
                    });
                }
                pulses.push_back(nlohmann::json{
                    {"parameters", nlohmann::json{
                        {"vector_index", pulse.parameters.vector_index},
                        {"time_seconds", pulse.parameters.time_seconds},
                        {"range_sample_start", pulse.parameters.range_sample_start},
                        {"platform", nlohmann::json{
                            {"position_m", pulse.parameters.platform.position_m},
                            {"velocity_mps", pulse.parameters.platform.velocity_mps},
                        }},
                    }},
                    {"samples", samples},
                });
            }

            channels.push_back(nlohmann::json{
                {"channel_id", channel.channel_id},
                {"waveform", nlohmann::json{
                    {"waveform_id", channel.waveform.waveform_id},
                    {"carrier_hz", channel.waveform.carrier_hz},
                    {"bandwidth_hz", channel.waveform.bandwidth_hz},
                    {"sample_rate_hz", channel.waveform.sample_rate_hz},
                    {"sample_type", channel.waveform.sample_type},
                    {"polarization", channel.waveform.polarization},
                    {"frequency_axis_hz", channel.waveform.frequency_axis_hz},
                }},
                {"pulses", pulses},
            });
        }

        return nlohmann::json{
            {"schema", "graphx.sar.normalized_product_handoff.v1"},
            {"collection", nlohmann::json{
                {"product_id", product.collection.product_id},
                {"collector_name", product.collection.collector_name},
                {"collection_id", product.collection.collection_id},
                {"coordinate_frame", product.collection.coordinate_frame},
                {"time_basis", product.collection.time_basis},
                {"source_files", product.collection.source_files},
                {"provenance_label", product.collection.provenance_label},
                {"source_ordering", product.collection.source_ordering},
            }},
            {"reference_geometry", nlohmann::json{
                {"scene_center_m", product.reference_geometry.scene_center_m},
                {"reference_platform", nlohmann::json{
                    {"position_m", product.reference_geometry.reference_platform.position_m},
                    {"velocity_mps", product.reference_geometry.reference_platform.velocity_mps},
                }},
            }},
            {"channels", channels},
        };
    }

    CrsdWriterOptions options_{};
};

} // namespace graphx::sar
