#include "sar/io/GotchaInputOrdering.hpp"
#include "sar/io/GotchaMatInspector.hpp"
#include "sar/io/GotchaMatReader.hpp"
#include "sar/io/CrsdIO.hpp"
#include "sar/io/GraphxSarNormalizedIO.hpp"
#include "sar/io/SarIoUtilities.hpp"
#include "sar/io/SarProductChunker.hpp"
#include "sar/io/SarProductValidator.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

struct CliOptions {
    std::filesystem::path input_dir{};
    std::filesystem::path output_dir{};
    std::string collection_id{};
    double max_output_size_mb{0.0};
    std::string sort{"lexical"};
    std::filesystem::path manifest{};
    std::string mode{"graphx-sar-normalized"};
    bool validate{false};
    bool emit_index{false};
    bool allow_classic_mat_with_sidecar{false};
    bool help{false};
};

void PrintHelp() {
    std::cout
        << "graphx-gotcha-to-crsd\n"
        << "Usage:\n"
        << "  graphx-gotcha-to-crsd --input-dir <dir> --output-dir <dir> --collection-id <id> \\\n"
        << "      --max-output-size-mb <mb> --sort <lexical|manifest> [--manifest <path>] \\\n"
        << "      --mode <graphx-sar-normalized|crsd> [--validate] [--emit-index]\n\n"
        << "Options:\n"
        << "  --help                 Show this help message\n"
        << "  --input-dir            Input GOTCHA MAT directory\n"
        << "  --output-dir           Output directory\n"
        << "  --collection-id        Collection identifier\n"
        << "  --max-output-size-mb   Max output chunk size in MB (0 disables split)\n"
        << "  --sort                 Ordering mode: lexical or manifest\n"
        << "  --manifest             Manifest path when --sort manifest\n"
        << "  --mode                 Output mode: graphx-sar-normalized or crsd\n"
        << "                         graphx-sar-normalized is NON-STANDARD and is not CRSD\n"
        << "  --validate             Run normalized product validation before export\n"
        << "  --allow-classic-mat-with-sidecar  Allow classic MAT input when sidecar JSON is present\n"
        << "  --emit-index           Emit mode-specific index JSON and conversion_report.json\n";
}

[[nodiscard]] bool IsFlag(const std::string& arg, const std::string& key) {
    return arg == key;
}

[[nodiscard]] std::string RequireValue(
    int argc,
    char** argv,
    int& i,
    const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::invalid_argument("missing_value:" + flag);
    }
    return argv[++i];
}

[[nodiscard]] CliOptions ParseArgs(int argc, char** argv) {
    CliOptions options{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (IsFlag(arg, "--help")) {
            options.help = true;
            return options;
        }
        if (IsFlag(arg, "--input-dir")) {
            options.input_dir = RequireValue(argc, argv, i, "--input-dir");
            continue;
        }
        if (IsFlag(arg, "--output-dir")) {
            options.output_dir = RequireValue(argc, argv, i, "--output-dir");
            continue;
        }
        if (IsFlag(arg, "--collection-id")) {
            options.collection_id = RequireValue(argc, argv, i, "--collection-id");
            continue;
        }
        if (IsFlag(arg, "--max-output-size-mb")) {
            options.max_output_size_mb = std::stod(RequireValue(argc, argv, i, "--max-output-size-mb"));
            continue;
        }
        if (IsFlag(arg, "--sort")) {
            options.sort = RequireValue(argc, argv, i, "--sort");
            continue;
        }
        if (IsFlag(arg, "--manifest")) {
            options.manifest = RequireValue(argc, argv, i, "--manifest");
            continue;
        }
        if (IsFlag(arg, "--mode")) {
            options.mode = RequireValue(argc, argv, i, "--mode");
            continue;
        }
        if (IsFlag(arg, "--validate")) {
            options.validate = true;
            continue;
        }
        if (IsFlag(arg, "--allow-classic-mat-with-sidecar")) {
            options.allow_classic_mat_with_sidecar = true;
            continue;
        }
        if (IsFlag(arg, "--emit-index")) {
            options.emit_index = true;
            continue;
        }
        throw std::invalid_argument("unknown_option:" + arg);
    }

    if (options.input_dir.empty()) {
        throw std::invalid_argument("missing_required:--input-dir");
    }
    if (options.output_dir.empty()) {
        throw std::invalid_argument("missing_required:--output-dir");
    }
    if (options.collection_id.empty()) {
        throw std::invalid_argument("missing_required:--collection-id");
    }
    if (options.max_output_size_mb < 0.0) {
        throw std::invalid_argument("invalid_value:--max-output-size-mb");
    }
    if (options.sort != "lexical" && options.sort != "manifest") {
        throw std::invalid_argument("invalid_value:--sort");
    }
    if (options.sort == "manifest" && options.manifest.empty()) {
        throw std::invalid_argument("missing_required:--manifest");
    }
    if (options.mode != "graphx-sar-normalized" && options.mode != "crsd") {
        throw std::invalid_argument("invalid_value:--mode");
    }

    return options;
}

[[nodiscard]] std::optional<std::string> FindOrderingErrorMessage(
    const graphx::sar::GotchaInputOrderingResult& ordering) {
    if (ordering.ok()) {
        return std::nullopt;
    }
    if (ordering.errors.empty()) {
        return std::string{"input_ordering_failed"};
    }
    const auto& first = ordering.errors.front();
    return first.code + ":" + first.path.generic_string();
}

[[nodiscard]] graphx::sar::GotchaInputOrderingResult DiscoverInputs(const CliOptions& options) {
    graphx::sar::GotchaInputOrderingOptions ordering_options{};
    ordering_options.input_directory = options.input_dir;
    ordering_options.mode = options.sort == "manifest"
        ? graphx::sar::GotchaInputOrderingMode::Manifest
        : graphx::sar::GotchaInputOrderingMode::Lexical;
    ordering_options.manifest_path = options.manifest;
    return graphx::sar::GotchaInputOrdering::Discover(ordering_options);
}

[[nodiscard]] bool EnsureSupportedMatFormats(
    const std::vector<std::filesystem::path>& files,
    bool allow_classic_mat_with_sidecar,
    std::string& failure_message) {
    for (const auto& path : files) {
        if (!graphx::sar::GotchaMatInspector::HasHdf5Signature(path)) {
            if (!allow_classic_mat_with_sidecar) {
                failure_message = "unsupported_mat_format:" + path.generic_string();
                return false;
            }
            const auto sidecar_path = std::filesystem::path{path.string() + ".json"};
            if (!std::filesystem::exists(sidecar_path)) {
                failure_message = "classic_mat_requires_sidecar:" + sidecar_path.generic_string();
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] graphx::sar::NormalizedSarProduct SliceProduct(
    const graphx::sar::NormalizedSarProduct& product,
    std::size_t pulse_start,
    std::size_t pulse_end) {
    auto sliced = product;
    sliced.channels.clear();
    sliced.channels.reserve(product.channels.size());

    for (const auto& channel : product.channels) {
        graphx::sar::ChannelSignal new_channel{};
        new_channel.channel_id = channel.channel_id;
        new_channel.waveform = channel.waveform;

        if (!channel.pulses.empty()) {
            const auto begin = std::min(pulse_start, channel.pulses.size());
            const auto end_exclusive = std::min(pulse_end + 1, channel.pulses.size());
            for (std::size_t i = begin; i < end_exclusive; ++i) {
                new_channel.pulses.push_back(channel.pulses[i]);
            }
        }

        sliced.channels.push_back(std::move(new_channel));
    }

    return sliced;
}

[[nodiscard]] std::string ShellQuote(const std::filesystem::path& path) {
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

[[nodiscard]] bool CommandExists(const std::string& name) {
    const std::string command = "command -v " + name + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

[[nodiscard]] std::optional<nlohmann::json> LoadJson(const std::filesystem::path& path) {
    std::ifstream stream{path};
    if (!stream) {
        return std::nullopt;
    }
    try {
        nlohmann::json value{};
        stream >> value;
        return value;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

struct SarpyValidationOutcome {
    bool ran{false};
    bool ok{false};
    std::string message{"skipped"};
};

[[nodiscard]] SarpyValidationOutcome ValidateCrsdWithSarpyIfAvailable(
    const std::filesystem::path& crsd_directory,
    const std::filesystem::path& scratch_directory) {
    SarpyValidationOutcome outcome{};

    if (!CommandExists("python3")) {
        outcome.message = "sarpy_validation_skipped:python3_not_found";
        return outcome;
    }

    const auto tool_path = std::filesystem::path{"tools/sarpy/validate_crsd.py"};
    if (!std::filesystem::exists(tool_path)) {
        outcome.message = "sarpy_validation_skipped:validate_crsd_tool_missing";
        return outcome;
    }

    const auto signal_path = crsd_directory / graphx::sar::CrsdWriter::kSignalFile;
    if (!std::filesystem::exists(signal_path)) {
        outcome.message = "sarpy_validation_error:crsd_signal_missing";
        return outcome;
    }

    std::error_code fs_error{};
    std::filesystem::create_directories(scratch_directory, fs_error);
    if (fs_error) {
        outcome.message = "sarpy_validation_error:scratch_directory_error";
        return outcome;
    }

    const auto probe_json = scratch_directory / "sarpy_probe.json";
    const auto probe_command =
        "python3 " + ShellQuote(tool_path) +
        " probe-environment --output-json " + ShellQuote(probe_json) +
        " >/dev/null 2>&1";
    if (std::system(probe_command.c_str()) != 0) {
        outcome.message = "sarpy_validation_skipped:probe_failed";
        return outcome;
    }

    const auto probe = LoadJson(probe_json);
    if (!probe.has_value()) {
        outcome.message = "sarpy_validation_skipped:probe_json_invalid";
        return outcome;
    }

    const bool has_sarpy =
        probe->contains("packages") &&
        (*probe)["packages"].contains("sarpy") &&
        (*probe)["packages"]["sarpy"].contains("installed") &&
        (*probe)["packages"]["sarpy"]["installed"].is_boolean() &&
        (*probe)["packages"]["sarpy"]["installed"].get<bool>();
    if (!has_sarpy) {
        outcome.message = "sarpy_validation_skipped:sarpy_not_installed";
        return outcome;
    }

    const auto report_json = scratch_directory / "sarpy_crsd_validation_report.json";
    const auto validate_command =
        "python3 " + ShellQuote(tool_path) +
        " validate --input-crsd " + ShellQuote(signal_path) +
        " --output-json " + ShellQuote(report_json) +
        " >/dev/null 2>&1";

    outcome.ran = true;
    const int validate_exit = std::system(validate_command.c_str());
    const auto report = LoadJson(report_json);
    if (validate_exit != 0 || !report.has_value()) {
        outcome.ok = false;
        outcome.message = "sarpy_validation_failed:validation_command_failed";
        return outcome;
    }

    if (report->contains("validation") &&
        (*report)["validation"].contains("status") &&
        (*report)["validation"]["status"].is_string() &&
        (*report)["validation"]["status"].get<std::string>() == "ok") {
        outcome.ok = true;
        outcome.message = "sarpy_validation_ok";
        return outcome;
    }

    outcome.ok = false;
    outcome.message = "sarpy_validation_failed:report_status_not_ok";
    return outcome;
}

[[nodiscard]] bool ValidateGotchaFieldsPreFlight(
    const std::vector<std::filesystem::path>& files,
    std::string& failure_message) {
    for (const auto& mat_file : files) {
        const auto sidecar_path = std::filesystem::path{mat_file.string() + ".json"};
        if (!std::filesystem::exists(sidecar_path)) {
            // Only validate if sidecar exists; inspector will handle missing sidecars
            continue;
        }

        const auto validation = graphx::sar::GotchaMatInspector::ValidateRequiredFields(sidecar_path);
        if (!validation.is_valid()) {
            // Report the first validation error
            if (!validation.missing_fields.empty()) {
                const auto& error = validation.missing_fields.front();
                failure_message = "missing_required_field:" + error.field_name + ":" + sidecar_path.generic_string();
                return false;
            }
            if (!validation.type_errors.empty()) {
                const auto& error = validation.type_errors.front();
                failure_message = "invalid_field_type:" + error.field_name + ":" + sidecar_path.generic_string() +
                                 ":" + error.expected_type;
                return false;
            }
        }
    }
    return true;
}

int Run(const CliOptions& options) {
    const auto ordering = DiscoverInputs(options);
    if (const auto error = FindOrderingErrorMessage(ordering); error.has_value()) {
        std::cerr << *error << '\n';
        return 1;
    }

    std::string mat_failure{};
    if (!EnsureSupportedMatFormats(
            ordering.files,
            options.allow_classic_mat_with_sidecar,
            mat_failure)) {
        std::cerr << mat_failure << '\n';
        return 1;
    }

    std::string field_failure{};
    if (!ValidateGotchaFieldsPreFlight(ordering.files, field_failure)) {
        std::cerr << field_failure << '\n';
        return 1;
    }

    graphx::sar::GotchaMatReader reader(graphx::sar::GotchaMatReaderOptions{
        .ordering_mode = options.sort == "manifest"
            ? graphx::sar::GotchaMatReaderOrderingMode::Manifest
            : graphx::sar::GotchaMatReaderOrderingMode::Lexical,
        .manifest_path = options.manifest,
        .collection_id = options.collection_id,
        .product_id = "gotcha_normalized_product",
        .collector_name = "GOTCHA",
        .coordinate_frame = "ecef",
        .time_basis = "seconds",
    });

    const auto read = reader.ReadDetailed(options.input_dir);
    if (!read.success) {
        std::cerr << read.message << '\n';
        return 1;
    }

    if (options.validate) {
        const auto validation = graphx::sar::SarProductValidator::Validate(read.product);
        if (!validation.ok()) {
            const auto& first = validation.errors.front();
            std::cerr << "validation_failed:" << first.code << ":" << first.path << '\n';
            return 1;
        }
    }

    const auto max_chunk_bytes =
        static_cast<std::uint64_t>(options.max_output_size_mb * 1024.0 * 1024.0);
    const auto output_prefix = options.mode == "crsd" ? "gotcha_crsd_chunk" : "gotcha_sar_normalized_chunk";
    const auto chunk_plan = graphx::sar::SarProductChunker::BuildPlan(
        read.product,
        graphx::sar::SarChunkerOptions{
            .max_chunk_bytes = max_chunk_bytes,
            .output_prefix = output_prefix,
        });

    std::error_code fs_error{};
    std::filesystem::create_directories(options.output_dir, fs_error);
    if (fs_error) {
        std::cerr << "output_directory_error:" << options.output_dir.generic_string() << '\n';
        return 1;
    }

    std::vector<graphx::sar::SarOutputSummary> output_summaries{};
    output_summaries.reserve(chunk_plan.chunks.size());

    std::string selected_format{};
    std::string label{};
    std::vector<std::string> assumptions{};
    std::vector<std::string> warnings = chunk_plan.warnings;
    std::string metadata_file{};
    std::string index_file{};
    std::string root_index_file{};

    if (options.mode == "graphx-sar-normalized") {
        selected_format = "graphx-sar-normalized";
        label = "NON-STANDARD";
        assumptions = {"non_standard_intermediate_format", "derived_from_normalized_sar_product"};
        metadata_file = "metadata.json";
        index_file = "gotcha_sar_normalized_index.json";
        root_index_file = "gotcha_sar_normalized_index.json";

        graphx::sar::GraphxSarNormalizedWriter writer(graphx::sar::GraphxSarNormalizedOptions{
            .assumptions = assumptions,
            .warnings = warnings,
        });

        for (const auto& chunk : chunk_plan.chunks) {
            const auto chunk_dir = options.output_dir / (chunk.output_stem + ".graphx-sar-normalized");
            const auto chunk_product = SliceProduct(read.product, chunk.pulse_start, chunk.pulse_end);
            const auto write = writer.Write(chunk_dir, chunk_product);
            if (!write.success) {
                std::cerr << "sar_normalized_write_failed:" << chunk_dir.generic_string() << ":" << write.message << '\n';
                return 1;
            }

            const auto signal_path = chunk_dir / graphx::sar::GraphxSarNormalizedWriter::kSignalFile;
            output_summaries.push_back(graphx::sar::SarOutputSummary{
                .output_name = chunk_dir.filename().generic_string(),
                .checksum_fnv1a64 = graphx::sar::GraphxSarNormalizedWriter::ComputeSignalChecksum(signal_path),
                .pulse_start = chunk.pulse_start,
                .pulse_end = chunk.pulse_end,
                .pulse_count = chunk.pulse_end >= chunk.pulse_start ? (chunk.pulse_end - chunk.pulse_start + 1) : 0,
                .channel_count = read.product.Shape().channel_count,
                .max_sample_count = read.product.Shape().max_sample_count,
            });
        }
    } else if (options.mode == "crsd") {
        selected_format = "crsd";
        label = "STANDARDS-TARGETED";
        assumptions = {
            "standards_targeted_crsd_metadata",
            "standards_targeted_signal_array",
            "standards_targeted_pvp",
        };
        metadata_file = graphx::sar::CrsdWriter::kMetadataFile;
        index_file = graphx::sar::CrsdWriter::kChunkIndexFile;
        root_index_file = "gotcha_crsd_index.json";

        graphx::sar::CrsdWriter writer;
        for (const auto& chunk : chunk_plan.chunks) {
            const auto chunk_dir = options.output_dir / (chunk.output_stem + ".crsd");
            const auto chunk_product = SliceProduct(read.product, chunk.pulse_start, chunk.pulse_end);
            const auto write = writer.Write(chunk_dir, chunk_product);
            if (!write.success) {
                std::cerr << "crsd_write_failed:" << chunk_dir.generic_string() << ":" << write.message << '\n';
                return 1;
            }

            const auto sarpy_validation = ValidateCrsdWithSarpyIfAvailable(
                chunk_dir,
                chunk_dir / "sarpy_validation");
            warnings.push_back(sarpy_validation.message);
            if (!sarpy_validation.ran || !sarpy_validation.ok) {
                std::cerr << "crsd_validation_failed:" << chunk_dir.generic_string() << ":" <<
                    sarpy_validation.message << '\n';
                return 1;
            }

            const auto signal_path = chunk_dir / graphx::sar::CrsdWriter::kSignalFile;
            output_summaries.push_back(graphx::sar::SarOutputSummary{
                .output_name = chunk_dir.filename().generic_string(),
                .checksum_fnv1a64 = graphx::sar::CrsdWriter::ComputeSignalChecksum(signal_path),
                .pulse_start = chunk.pulse_start,
                .pulse_end = chunk.pulse_end,
                .pulse_count = chunk.pulse_end >= chunk.pulse_start ? (chunk.pulse_end - chunk.pulse_start + 1) : 0,
                .channel_count = read.product.Shape().channel_count,
                .max_sample_count = read.product.Shape().max_sample_count,
            });
        }
    }

    if (options.emit_index) {
        std::vector<double> frequency_axis{};
        if (!read.product.channels.empty()) {
            frequency_axis = read.product.channels.front().waveform.frequency_axis_hz;
        }

        const auto index_schema = options.mode == "crsd"
            ? "graphx.sar.gotcha_crsd_index.v1"
            : "graphx.sar.gotcha_sar_normalized_index.v1";
        const auto index_json = graphx::sar::SarIoUtilities::BuildGotchaOutputIndexJson(
            graphx::sar::GotchaOutputIndexBuildInput{
                .schema = index_schema,
                .collection_id = read.product.collection.collection_id,
                .source_files = read.product.collection.source_files,
                .source_ordering = read.product.collection.source_ordering,
                .provenance = read.product.collection.provenance_label,
                .outputs = output_summaries,
                .frequency_axis_hz = frequency_axis,
                .assumptions = assumptions,
                .warnings = warnings,
            });

        std::string validation_status = options.validate ? "ok" : "skipped";
        if (options.mode == "crsd") {
            bool has_sarpy_skip = false;
            bool has_sarpy_error = false;
            for (const auto& warning : warnings) {
                if (warning.rfind("sarpy_validation_failed:", 0) == 0) {
                    has_sarpy_error = true;
                }
                if (warning.rfind("sarpy_validation_skipped:", 0) == 0) {
                    has_sarpy_skip = true;
                }
            }
            if (has_sarpy_error) {
                validation_status = "error";
            } else if (has_sarpy_skip) {
                validation_status = "unavailable";
            }
        }

        const auto report_json = graphx::sar::SarIoUtilities::BuildConversionReportJson(
            graphx::sar::ConversionReportBuildInput{
                .format = selected_format,
                .label = label,
                .selected_mode = options.mode,
                .validation_status = validation_status,
                .provenance = read.product.collection.provenance_label,
                .source_ordering = read.product.collection.source_ordering,
                .assumptions = assumptions,
                .warnings = warnings,
                .outputs = output_summaries,
                .metadata_file = metadata_file,
                .index_file = index_file,
                .total_files_read = read.product.collection.source_files.size(),
                .total_pulses_read = read.product.Shape().pulse_count,
                .pulses_per_file = graphx::sar::SarIoUtilities::ComputePulsesPerFile(read.product),
                .aperture_mode = "full_aperture",
            });

        if (!graphx::sar::SarIoUtilities::WriteJson(options.output_dir / root_index_file, index_json) ||
            !graphx::sar::SarIoUtilities::WriteJson(options.output_dir / "conversion_report.json", report_json) ||
            !graphx::sar::SarIoUtilities::WriteWarningsLog(options.output_dir / "conversion_warnings.log", warnings)) {
            std::cerr << "report_write_failed:" << options.output_dir.generic_string() << '\n';
            return 1;
        }
    }

    std::cout << "conversion_successful: mode=" << options.mode << " chunks=" << output_summaries.size() << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = ParseArgs(argc, argv);
        if (options.help) {
            PrintHelp();
            return 0;
        }
        return Run(options);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
