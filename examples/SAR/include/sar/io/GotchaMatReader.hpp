// SPDX-License-Identifier: MIT

/**
 * @file GotchaMatReader.hpp
 * @brief GraphX source file.
 */

#pragma once

#include "sar/io/GotchaHdf5PhdataReader.hpp"
#include "sar/io/GotchaInputOrdering.hpp"
#include "sar/io/GotchaMatInspector.hpp"
#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphx::sar {

enum class GotchaMatReaderOrderingMode {
    Lexical,
    Manifest,
};

struct GotchaMatReaderOptions {
    GotchaMatReaderOrderingMode ordering_mode{GotchaMatReaderOrderingMode::Lexical};
    std::filesystem::path manifest_path{};
    std::string extension{".mat"};
    std::string collection_id{"gotcha_collection"};
    std::string product_id{"gotcha_normalized_product"};
    std::string collector_name{"GOTCHA"};
    std::string coordinate_frame{"ecef"};
    std::string time_basis{"seconds"};
    bool emit_progress{false};
};

struct GotchaMatReaderIssue {
    std::string code{};
    std::filesystem::path path{};
    std::string message{};
};

struct GotchaMatReaderDiagnostics {
    std::vector<std::string> source_files{};
    std::vector<GotchaMatReaderIssue> issues{};
};

struct GotchaMatReaderResult {
    bool success{false};
    std::string message{};
    NormalizedSarProduct product{};
    GotchaMatReaderDiagnostics diagnostics{};
};

/**
 * @class GotchaMatReader
 * @brief GotchaMatReader class.
 */
class GotchaMatReader final : public ISarReader {
public:
    explicit GotchaMatReader(GotchaMatReaderOptions options = {})
        : options_(std::move(options)) {}

    [[nodiscard]] SarReadResult Read(const std::filesystem::path& path) const override {
        const auto detailed = ReadDetailed(path);
        return SarReadResult{
            .success = detailed.success,
            .message = detailed.message,
            .product = detailed.product,
        };
    }

    [[nodiscard]] GotchaMatReaderResult ReadDetailed(const std::filesystem::path& input_path) const {
        GotchaMatReaderResult result{};

        const auto ordering = DiscoverOrderedInputs(input_path);
        if (!ordering.ok()) {
            for (const auto& error : ordering.errors) {
                result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                    .code = error.code,
                    .path = error.path,
                    .message = error.message,
                });
            }
            result.message = BuildFailureMessage(result.diagnostics.issues, "input ordering failed");
            return result;
        }

        auto product = BuildProductSkeleton(ordering.files);
        if (options_.emit_progress) {
            std::cerr << "info: gotcha_reader: discovered " << ordering.files.size()
                      << " ordered MAT file(s)\n";
        }
        ChannelSignal channel{};
        channel.channel_id = "channel_0";
        channel.waveform.waveform_id = "gotcha_phase_history_channel_0";
        channel.waveform.sample_type = "complex_f32";
        channel.waveform.polarization = "unknown";

        std::uint64_t global_pulse_index = 0;

        // Full-aperture mode: iterate through each file and read all Np pulses per file
        for (std::size_t file_index = 0; file_index < ordering.files.size(); ++file_index) {
            const auto& source_file = ordering.files[file_index];
            if (options_.emit_progress) {
                std::cerr << "info: gotcha_reader: reading file "
                          << (file_index + 1) << "/" << ordering.files.size()
                          << ": " << source_file.generic_string() << '\n';
            }

            if (!GotchaHdf5PhdataReader::IsAvailable()) {
                result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                    .code = "hdf5_support_not_compiled",
                    .path = source_file,
                    .message = "HDF5 support is required for GOTCHA MAT ingestion",
                });
                continue;
            }

            if (!GotchaMatInspector::HasHdf5Signature(source_file)) {
                result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                    .code = "unsupported_mat_format",
                    .path = source_file,
                    .message = "only HDF5-backed MAT v7.3 files are supported",
                });
                continue;
            }

            const auto hdf5 = GotchaHdf5PhdataReader::Read(
                source_file,
                GotchaHdf5ReadOptions{.emit_progress = options_.emit_progress});
            if (!hdf5.success) {
                result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                    .code    = "hdf5_phdata_read_failed",
                    .path    = source_file,
                    .message = hdf5.message,
                });
                continue;
            }
            if (options_.emit_progress) {
                const auto sample_count =
                    hdf5.pulses.empty() ? 0U : hdf5.pulses.front().samples.size();
                std::cerr << "info: gotcha_reader: loaded file "
                          << (file_index + 1) << "/" << ordering.files.size()
                          << ": pulses=" << hdf5.pulses.size()
                          << ", samples_per_pulse=" << sample_count << '\n';
            }

            // Set waveform metadata from the first file that yields data.
            if (channel.waveform.carrier_hz == 0.0 && hdf5.waveform.carrier_hz != 0.0) {
                channel.waveform.carrier_hz     = hdf5.waveform.carrier_hz;
                channel.waveform.bandwidth_hz   = hdf5.waveform.bandwidth_hz;
                channel.waveform.sample_rate_hz = hdf5.waveform.sample_rate_hz;
                if (channel.waveform.frequency_axis_hz.empty() &&
                    hdf5.waveform.k_samples > 0) {
                    channel.waveform.frequency_axis_hz = {
                        hdf5.waveform.min_f,
                        hdf5.waveform.min_f +
                            hdf5.waveform.delta_f *
                                static_cast<double>(hdf5.waveform.k_samples - 1),
                    };
                }
            }

            for (std::size_t pi = 0; pi < hdf5.pulses.size(); ++pi) {
                const auto& hp = hdf5.pulses[pi];
                PulseVector pulse{};
                pulse.parameters.vector_index        = global_pulse_index;
                pulse.parameters.time_seconds        = hp.pulse_time;
                pulse.parameters.range_sample_start  = 0;
                pulse.parameters.source_file_index   =
                    static_cast<std::uint64_t>(file_index);
                pulse.parameters.source_pulse_index  =
                    static_cast<std::uint64_t>(pi);
                pulse.parameters.reference_range_m   = hp.reference_range_m;
                pulse.parameters.platform.position_m = hp.antenna_position_m;
                pulse.samples = hp.samples;
                channel.pulses.push_back(std::move(pulse));
                ++global_pulse_index;
            }
        }

        if (!result.diagnostics.issues.empty()) {
            result.message = BuildFailureMessage(result.diagnostics.issues, "gotcha_mat_read_failed");
            return result;
        }

        if (channel.pulses.empty()) {
            result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                .code = "no_pulses_loaded",
                .path = input_path,
                .message = "no pulses were produced from ordered MAT inputs",
            });
            result.message = BuildFailureMessage(result.diagnostics.issues, "gotcha_mat_read_failed");
            return result;
        }

        if (channel.waveform.sample_rate_hz <= 0.0) {
            channel.waveform.sample_rate_hz = 1.0;
        }

        product.collection.expected_pulse_count = static_cast<std::uint64_t>(channel.pulses.size());
        product.channels.push_back(std::move(channel));
        if (options_.emit_progress) {
            const auto shape = product.Shape();
            std::cerr << "info: gotcha_reader: normalized product ready: pulses="
                      << shape.pulse_count << ", channels=" << shape.channel_count
                      << ", max_sample_count=" << shape.max_sample_count << '\n';
        }
        result.product = std::move(product);
        result.success = true;
        result.message = "ok";
        return result;
    }

private:
    [[nodiscard]] GotchaInputOrderingResult DiscoverOrderedInputs(const std::filesystem::path& input_path) const {
        if (options_.ordering_mode == GotchaMatReaderOrderingMode::Manifest) {
            return GotchaInputOrdering::DiscoverManifest(input_path, options_.manifest_path);
        }
        return GotchaInputOrdering::DiscoverLexical(input_path, options_.extension);
    }

    [[nodiscard]] NormalizedSarProduct BuildProductSkeleton(
        const std::vector<std::filesystem::path>& ordered_files) const {
        NormalizedSarProduct product{};
        product.collection.product_id = options_.product_id;
        product.collection.collector_name = options_.collector_name;
        product.collection.collection_id = options_.collection_id;
        product.collection.coordinate_frame = options_.coordinate_frame;
        product.collection.time_basis = options_.time_basis;
        product.collection.provenance_label = "derived_from_gotcha_phase_history";
        product.collection.source_ordering =
            options_.ordering_mode == GotchaMatReaderOrderingMode::Manifest ? "manifest" : "lexical";

        product.collection.source_files.reserve(ordered_files.size());
        for (const auto& file : ordered_files) {
            product.collection.source_files.push_back(file.generic_string());
        }

        return product;
    }

    [[nodiscard]] static std::string BuildFailureMessage(
        const std::vector<GotchaMatReaderIssue>& issues,
        const std::string& prefix) {
        if (issues.empty()) {
            return prefix;
        }
        return prefix + ": " + issues.front().code;
    }

    GotchaMatReaderOptions options_{};
};

} // namespace graphx::sar
