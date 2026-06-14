#pragma once

#include "sar/io/GotchaInputOrdering.hpp"
#include "sar/io/GotchaToCrsdMetadataMapper.hpp"
#include "sar/io/NormalizedSarProduct.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

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
};

struct GotchaMatReaderIssue {
    std::string code{};
    std::filesystem::path path{};
    std::string message{};
};

struct GotchaFieldNameDiagnostic {
    std::string normalized_field{};
    std::string original_field_name{};
    std::filesystem::path source_file{};
};

struct GotchaMatReaderDiagnostics {
    std::vector<std::string> source_files{};
    std::vector<GotchaFieldNameDiagnostic> original_field_names{};
    std::vector<GotchaMatReaderIssue> issues{};
};

struct GotchaMatReaderResult {
    bool success{false};
    std::string message{};
    NormalizedSarProduct product{};
    GotchaMatReaderDiagnostics diagnostics{};
};

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
        ChannelSignal channel{};
        channel.channel_id = "channel_0";
        channel.waveform.waveform_id = "gotcha_phase_history_channel_0";
        channel.waveform.sample_type = "complex_f32";
        channel.waveform.polarization = "unknown";

        std::uint64_t global_pulse_index = 0;

        // Full-aperture mode: iterate through each file and read all Np pulses per file
        for (std::size_t file_index = 0; file_index < ordering.files.size(); ++file_index) {
            const auto& source_file = ordering.files[file_index];
            const auto sidecar_path = std::filesystem::path{source_file.string() + ".json"};
            const auto sidecar = LoadSidecar(sidecar_path, result.diagnostics.issues);
            if (!sidecar.has_value()) {
                continue;
            }

            // Read Np (number of pulses) from sidecar; default to 1 for backward compatibility
            const auto np = ParseOptionalUnsigned(*sidecar, "Np").value_or(1u);
            const auto mapped_metadata = GotchaToCrsdMetadataMapper::Map(*sidecar);
            if (mapped_metadata.has_value()) {
                GotchaToCrsdMetadataMapper::ApplyToProductCollection(*mapped_metadata, product.collection);
                GotchaToCrsdMetadataMapper::ApplyToWaveform(*mapped_metadata, channel.waveform);
            }

            // Process each pulse within this file
            for (std::uint64_t pulse_within_file = 0; pulse_within_file < np; ++pulse_within_file) {
                const auto samples = ParseSamples(*sidecar, sidecar_path, result.diagnostics.issues);
                if (samples.empty()) {
                    // On first pulse failure per file, skip remaining pulses from this file
                    break;
                }

                if (!mapped_metadata.has_value() && channel.waveform.frequency_axis_hz.empty()) {
                    channel.waveform.frequency_axis_hz = ParseOptionalDoubleArray(*sidecar, "frequency_axis_hz");
                }

                if (!mapped_metadata.has_value()) {
                    const auto carrier = ParseOptionalFiniteDouble(*sidecar, "carrier_hz");
                    if (carrier.has_value()) {
                        channel.waveform.carrier_hz = *carrier;
                    }
                    const auto bandwidth = ParseOptionalFiniteDouble(*sidecar, "bandwidth_hz");
                    if (bandwidth.has_value()) {
                        channel.waveform.bandwidth_hz = *bandwidth;
                    }
                }
                const auto sample_rate = ParseOptionalFiniteDouble(*sidecar, "sample_rate_hz");
                if (sample_rate.has_value()) {
                    channel.waveform.sample_rate_hz = *sample_rate;
                }
                const auto polarization = ParseOptionalNonEmptyString(*sidecar, "polarization");
                if (polarization.has_value()) {
                    channel.waveform.polarization = *polarization;
                }

                std::array<double, 3> position{};
                if (mapped_metadata.has_value()) {
                    position = mapped_metadata->antenna_xyz_m;
                } else {
                    const auto parsed_position = ParseVector3(
                        *sidecar,
                        "platform_position_m",
                        sidecar_path,
                        result.diagnostics.issues);
                    if (!parsed_position.has_value()) {
                        break;
                    }
                    position = *parsed_position;
                }
                const auto velocity = ParseOptionalVector3(*sidecar, "platform_velocity_mps");

                const auto pulse_time = ParseOptionalFiniteDouble(*sidecar, "pulse_time_seconds")
                                            .value_or(static_cast<double>(global_pulse_index));
                const auto range_start = ParseOptionalUnsigned(*sidecar, "range_sample_start").value_or(0u);

                PulseVector pulse{};
                pulse.parameters.vector_index = global_pulse_index;
                pulse.parameters.time_seconds = pulse_time;
                pulse.parameters.range_sample_start = range_start;
                pulse.parameters.source_file_index = static_cast<std::uint64_t>(file_index);
                pulse.parameters.source_pulse_index = pulse_within_file;
                pulse.parameters.platform.position_m = position;
                pulse.parameters.platform.velocity_mps = velocity.value_or(std::array<double, 3>{0.0, 0.0, 0.0});
                if (mapped_metadata.has_value()) {
                    GotchaToCrsdMetadataMapper::ApplyToPulse(*mapped_metadata, pulse.parameters);
                }
                pulse.samples = samples;
                channel.pulses.push_back(std::move(pulse));

                global_pulse_index++;
            }

            // Collect field diagnostics only once per file
            CollectFieldDiagnostics(*sidecar, source_file, result.diagnostics.original_field_names);
        }

        if (!result.diagnostics.issues.empty()) {
            result.message = BuildFailureMessage(result.diagnostics.issues, "mat sidecar parsing failed");
            return result;
        }

        if (channel.pulses.empty()) {
            result.diagnostics.issues.push_back(GotchaMatReaderIssue{
                .code = "no_pulses_loaded",
                .path = input_path,
                .message = "no pulses were produced from ordered MAT inputs",
            });
            result.message = BuildFailureMessage(result.diagnostics.issues, "mat sidecar parsing failed");
            return result;
        }

        if (channel.waveform.sample_rate_hz <= 0.0) {
            channel.waveform.sample_rate_hz = 1.0;
        }

        product.collection.expected_pulse_count = static_cast<std::uint64_t>(channel.pulses.size());
        product.channels.push_back(std::move(channel));
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

    [[nodiscard]] static std::optional<nlohmann::json> LoadSidecar(
        const std::filesystem::path& sidecar_path,
        std::vector<GotchaMatReaderIssue>& issues) {
        std::ifstream stream{sidecar_path};
        if (!stream) {
            issues.push_back(GotchaMatReaderIssue{
                .code = "sidecar_missing",
                .path = sidecar_path,
                .message = "expected sidecar JSON beside MAT file",
            });
            return std::nullopt;
        }

        try {
            nlohmann::json value{};
            stream >> value;
            if (!value.is_object()) {
                issues.push_back(GotchaMatReaderIssue{
                    .code = "sidecar_schema_error",
                    .path = sidecar_path,
                    .message = "sidecar root must be a JSON object",
                });
                return std::nullopt;
            }
            return value;
        } catch (const nlohmann::json::exception& ex) {
            issues.push_back(GotchaMatReaderIssue{
                .code = "sidecar_json_error",
                .path = sidecar_path,
                .message = ex.what(),
            });
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::vector<ComplexSample> ParseSamples(
        const nlohmann::json& sidecar,
        const std::filesystem::path& path,
        std::vector<GotchaMatReaderIssue>& issues) {
        std::vector<ComplexSample> samples{};
        const auto it = sidecar.find("iq_samples");
        if (it == sidecar.end() || !it->is_array() || it->empty()) {
            issues.push_back(GotchaMatReaderIssue{
                .code = "missing_iq_samples",
                .path = path,
                .message = "iq_samples must be a non-empty array",
            });
            return samples;
        }

        samples.reserve(it->size());
        for (std::size_t i = 0; i < it->size(); ++i) {
            const auto& value = (*it)[i];
            if (!value.is_object() || !value.contains("real") || !value.contains("imag") ||
                !value.at("real").is_number() || !value.at("imag").is_number()) {
                issues.push_back(GotchaMatReaderIssue{
                    .code = "invalid_iq_sample",
                    .path = path,
                    .message = "iq_samples entries must have numeric real and imag",
                });
                samples.clear();
                return samples;
            }
            samples.push_back(ComplexSample{
                .real = value.at("real").get<float>(),
                .imag = value.at("imag").get<float>(),
            });
        }

        return samples;
    }

    [[nodiscard]] static std::optional<std::array<double, 3>> ParseVector3(
        const nlohmann::json& sidecar,
        const char* key,
        const std::filesystem::path& path,
        std::vector<GotchaMatReaderIssue>& issues) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_array() || it->size() != 3 ||
            !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) {
            issues.push_back(GotchaMatReaderIssue{
                .code = std::string{"invalid_"} + key,
                .path = path,
                .message = std::string{key} + " must be an array of three numbers",
            });
            return std::nullopt;
        }

        return std::array<double, 3>{
            (*it)[0].get<double>(),
            (*it)[1].get<double>(),
            (*it)[2].get<double>(),
        };
    }

    [[nodiscard]] static std::optional<std::array<double, 3>> ParseOptionalVector3(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end()) {
            return std::nullopt;
        }
        if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
            !(*it)[2].is_number()) {
            return std::nullopt;
        }
        return std::array<double, 3>{
            (*it)[0].get<double>(),
            (*it)[1].get<double>(),
            (*it)[2].get<double>(),
        };
    }

    [[nodiscard]] static std::optional<double> ParseOptionalFiniteDouble(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_number()) {
            return std::nullopt;
        }
        return it->get<double>();
    }

    [[nodiscard]] static std::optional<std::uint64_t> ParseOptionalUnsigned(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_number_unsigned()) {
            return std::nullopt;
        }
        return it->get<std::uint64_t>();
    }

    [[nodiscard]] static std::optional<std::string> ParseOptionalNonEmptyString(
        const nlohmann::json& sidecar,
        const char* key) {
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_string()) {
            return std::nullopt;
        }
        const auto value = it->get<std::string>();
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] static std::vector<double> ParseOptionalDoubleArray(
        const nlohmann::json& sidecar,
        const char* key) {
        std::vector<double> values{};
        const auto it = sidecar.find(key);
        if (it == sidecar.end() || !it->is_array()) {
            return values;
        }
        values.reserve(it->size());
        for (const auto& entry : *it) {
            if (!entry.is_number()) {
                values.clear();
                return values;
            }
            values.push_back(entry.get<double>());
        }
        return values;
    }

    static void CollectFieldDiagnostics(
        const nlohmann::json& sidecar,
        const std::filesystem::path& source_file,
        std::vector<GotchaFieldNameDiagnostic>& output) {
        const auto fields_it = sidecar.find("source_field_names");
        if (fields_it == sidecar.end() || !fields_it->is_object()) {
            return;
        }

        for (const auto& [normalized_field, original_field] : fields_it->items()) {
            if (!original_field.is_string()) {
                continue;
            }
            output.push_back(GotchaFieldNameDiagnostic{
                .normalized_field = normalized_field,
                .original_field_name = original_field.get<std::string>(),
                .source_file = source_file,
            });
        }
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
