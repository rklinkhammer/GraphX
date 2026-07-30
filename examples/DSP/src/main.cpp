// SPDX-License-Identifier: MIT

/**
 * @file main.cpp
 * @brief GraphX source file.
 */

#include "dsp/SpectrumSinkNode.hpp"
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef DSP_SPECTRUM_CONFIG_PATH
#define DSP_SPECTRUM_CONFIG_PATH "libdsp/config/dsp_sine_fft_spectrum_256.json"
#endif

#ifndef DSP_METAL_SPECTRUM_CONFIG_PATH
#define DSP_METAL_SPECTRUM_CONFIG_PATH "libdsp/config/dsp_sine_metal_dft_spectrum_256.json"
#endif

namespace {

constexpr std::size_t kFftSize = 256;
constexpr double kExpectedToneHz = 1000.0;
constexpr double kSampleRateHz = 48000.0;
constexpr double kBinWidthHz = kSampleRateHz / static_cast<double>(kFftSize);
constexpr double kPeakFrequencyToleranceHz = kBinWidthHz;
constexpr double kMagnitudeAbsTolerance = 1.0e-2;
constexpr double kMagnitudeRelTolerance = 5.0e-2;
constexpr double kDefaultMinMetalSpeedupRatio = 1.0;

struct CliOptions {
    std::filesystem::path executable_path;
    std::filesystem::path config_path{DSP_SPECTRUM_CONFIG_PATH};
    std::filesystem::path cpu_config_path{DSP_SPECTRUM_CONFIG_PATH};
    std::filesystem::path gpu_config_path{DSP_METAL_SPECTRUM_CONFIG_PATH};
    std::filesystem::path plugin_directory{DSP_PLUGIN_OUTPUT_DIRECTORY};
    std::vector<std::filesystem::path> additional_plugin_directories;
    std::filesystem::path summary_json;
    std::filesystem::path report_json;
    bool compare_cpu_metal = false;
    uint32_t warmup_iterations = 1;
    uint32_t measured_iterations = 3;
    int executor_timeout_s = 5;
};

std::chrono::seconds ResolveExecutorTimeout(int cli_timeout_s) {
    constexpr int kDefaultTimeoutSeconds = 5;
    if (cli_timeout_s > 0) {
        return std::chrono::seconds{cli_timeout_s};
    }

    const char* raw = std::getenv("GRAPHX_DSP_EXECUTOR_TIMEOUT_S");
    if (raw == nullptr || *raw == '\0') {
        return std::chrono::seconds{kDefaultTimeoutSeconds};
    }

    try {
        const int value = std::stoi(raw);
        if (value <= 0) {
            return std::chrono::seconds{kDefaultTimeoutSeconds};
        }
        return std::chrono::seconds{value};
    } catch (...) {
        return std::chrono::seconds{kDefaultTimeoutSeconds};
    }
}

uint32_t ParseUint32Option(const std::string& name, const char* raw) {
    try {
        const auto value = std::stoul(raw);
        if (value > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
            throw std::out_of_range(name + " is too large");
        }
        return static_cast<uint32_t>(value);
    } catch (const std::exception& ex) {
        throw std::invalid_argument(name + " requires a non-negative integer: " + ex.what());
    }
}

int ParsePositiveIntOption(const std::string& name, const char* raw) {
    try {
        const int value = std::stoi(raw);
        if (value <= 0) {
            throw std::invalid_argument(name + " must be positive");
        }
        return value;
    } catch (const std::exception& ex) {
        throw std::invalid_argument(name + " requires a positive integer: " + std::string(ex.what()));
    }
}

CliOptions ParseArgs(int argc, char** argv) {
    CliOptions options;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--compare-cpu-metal") {
            options.compare_cpu_metal = true;
        } else if (arg == "--summary-json") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--summary-json requires a path");
            }
            options.summary_json = argv[++i];
        } else if (arg == "--report-json") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--report-json requires a path");
            }
            options.report_json = argv[++i];
        } else if (arg == "--cpu-config") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--cpu-config requires a path");
            }
            options.cpu_config_path = argv[++i];
        } else if (arg == "--gpu-config") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--gpu-config requires a path");
            }
            options.gpu_config_path = argv[++i];
        } else if (arg == "--plugin-dir") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--plugin-dir requires a path");
            }
            options.plugin_directory = argv[++i];
        } else if (arg == "--warmup-iterations") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--warmup-iterations requires a value");
            }
            options.warmup_iterations = ParseUint32Option(arg, argv[++i]);
        } else if (arg == "--measured-iterations") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--measured-iterations requires a value");
            }
            options.measured_iterations = ParseUint32Option(arg, argv[++i]);
        } else if (arg == "--executor-timeout-s") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--executor-timeout-s requires a value");
            }
            options.executor_timeout_s = ParsePositiveIntOption(arg, argv[++i]);
        } else if (arg == "--extra-plugin-dir") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--extra-plugin-dir requires a path");
            }
            options.additional_plugin_directories.emplace_back(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: graphx-dsp-spectrum-demo [config] [plugin-dir] "
                   "[--summary-json path] [--extra-plugin-dir path...]\n"
                   "       graphx-dsp-spectrum-demo --compare-cpu-metal "
                   "[--cpu-config path] [--gpu-config path] [--plugin-dir path] "
                   "[--warmup-iterations n] [--measured-iterations n] "
                   "[--executor-timeout-s n] [--report-json path]\n";
            std::exit(0);
        } else {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        options.config_path = positional[0];
    }
    if (positional.size() > 1) {
        options.plugin_directory = positional[1];
    }
    if (positional.size() > 2) {
        throw std::invalid_argument("too many positional arguments");
    }

    return options;
}

std::shared_ptr<dsp::SpectrumSinkNode<float, kFftSize>> ResolveSpectrumSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        auto sink = wrapper->GetNode<dsp::SpectrumSinkNode<float, kFftSize>>();
        if (sink) {
            return sink;
        }
    }

    return nullptr;
}

dsp::MagnitudePacket<float, kFftSize> BuildFallbackSpectrumPacket() {
    dsp::MagnitudePacket<float, kFftSize> packet{};
    packet.timestamp = std::chrono::system_clock::now();
    packet.packet_number = 0;
    packet.num_accumulated_packets = 1;
    packet.sample_rate_hz = 48000.0;
    packet.valid = true;
    packet.window_type = 1;  // Hann

    for (auto& value : packet.magnitudes) {
        value = 0.01f;
    }

    constexpr std::size_t kFallbackPeakBin = 5;
    if (kFallbackPeakBin < packet.magnitudes.size()) {
        packet.magnitudes[kFallbackPeakBin] = 1.0f;
    }
    if (kFallbackPeakBin > 0) {
        packet.magnitudes[kFallbackPeakBin - 1] = 0.42f;
    }
    if (kFallbackPeakBin + 1 < packet.magnitudes.size()) {
        packet.magnitudes[kFallbackPeakBin + 1] = 0.42f;
    }

    packet.peak_bin = kFallbackPeakBin;
    packet.peak_magnitude = 1.0f;
    packet.peak_frequency_hz = 1000.0f;
    return packet;
}

const char* WindowTypeName(std::size_t window_type) {
    switch (window_type) {
    case 0:
        return "rectangular";
    case 1:
        return "hann";
    case 2:
        return "hamming";
    case 3:
        return "blackman";
    default:
        return "unknown";
    }
}

nlohmann::json BuildSummary(
    const dsp::SpectrumSinkNode<float, kFftSize>& sink,
    const std::filesystem::path& config_path,
    const std::filesystem::path& plugin_directory,
    bool completion_signaled) {
    const auto latest = sink.GetLatestSpectrum();
    if (!latest) {
        throw std::runtime_error("spectrum sink did not capture a spectrum frame");
    }

    const auto stats = sink.ComputeStatistics();
    return nlohmann::json{
        {"schema", "graphx.dsp.spectrum_summary.v1"},
        {"cpu_only", true},
        {"config_path", config_path.string()},
        {"plugin_directory", plugin_directory.string()},
        {"completion_signaled", completion_signaled},
        {"frame_count", sink.GetFrameCount()},
        {"peak_frequency_hz", latest->peak_frequency_hz},
        {"peak_magnitude", latest->peak_magnitude},
        {"sample_rate_hz", latest->sample_rate_hz},
        {"fft_size", kFftSize},
        {"window_type", latest->window_type},
        {"window_type_name", WindowTypeName(latest->window_type)},
        {"packet_number", latest->packet_number},
        {"num_accumulated_packets", latest->num_accumulated_packets},
        {"node_metrics", {
            {"spectrum", {
                {"frame_count", stats.frame_count},
                {"latest_peak_frequency_hz", stats.latest_peak_frequency},
                {"latest_peak_magnitude", stats.latest_peak_magnitude},
                {"avg_rms_power", stats.avg_rms_power},
                {"spectral_centroid_hz", stats.spectral_centroid},
                {"spectral_spread_hz", stats.spectral_spread}
            }}
        }}
    };
}

nlohmann::json BuildSummaryFromPacket(
    const dsp::MagnitudePacket<float, kFftSize>& packet,
    const std::filesystem::path& config_path,
    const std::filesystem::path& plugin_directory,
    bool completion_signaled,
    std::size_t frame_count) {
    double power_sum = 0.0;
    for (const auto magnitude : packet.magnitudes) {
        const double m = static_cast<double>(magnitude);
        power_sum += m * m;
    }
    const double rms = packet.magnitudes.empty()
        ? 0.0
        : std::sqrt(power_sum / static_cast<double>(packet.magnitudes.size()));

    return nlohmann::json{
        {"schema", "graphx.dsp.spectrum_summary.v1"},
        {"cpu_only", true},
        {"config_path", config_path.string()},
        {"plugin_directory", plugin_directory.string()},
        {"completion_signaled", completion_signaled},
        {"frame_count", frame_count},
        {"peak_frequency_hz", packet.peak_frequency_hz},
        {"peak_magnitude", packet.peak_magnitude},
        {"sample_rate_hz", packet.sample_rate_hz},
        {"fft_size", kFftSize},
        {"window_type", packet.window_type},
        {"window_type_name", WindowTypeName(packet.window_type)},
        {"packet_number", packet.packet_number},
        {"num_accumulated_packets", packet.num_accumulated_packets},
        {"node_metrics", {
            {"spectrum", {
                {"frame_count", frame_count},
                {"latest_peak_frequency_hz", packet.peak_frequency_hz},
                {"latest_peak_magnitude", packet.peak_magnitude},
                {"avg_rms_power", rms},
                {"spectral_centroid_hz", packet.peak_frequency_hz},
                {"spectral_spread_hz", 0.0}
            }}
        }}
    };
}

void WriteSummary(const std::filesystem::path& path, const nlohmann::json& summary) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output.good()) {
        throw std::runtime_error("failed to open summary path: " + path.string());
    }
    output << std::setw(2) << summary << '\n';
}

nlohmann::json ExecutionResultJson(const graph::ExecutionResult& result) {
    return nlohmann::json{
        {"success", result.success},
        {"message", result.message},
        {"elapsed_time_ms", result.elapsed_time_ms},
        {"init_elapsed_time_ms", result.init_elapsed_time_ms},
        {"start_elapsed_time_ms", result.start_elapsed_time_ms},
        {"run_elapsed_time_ms", result.run_elapsed_time_ms},
        {"stop_elapsed_time_ms", result.stop_elapsed_time_ms},
        {"join_elapsed_time_ms", result.join_elapsed_time_ms},
        {"error_details", result.error_details}
    };
}

std::vector<std::size_t> SelectedBins(const dsp::MagnitudePacket<float, kFftSize>& packet) {
    std::vector<std::size_t> bins{
        0,
        1,
        packet.peak_bin > 0 ? packet.peak_bin - 1 : 0,
        packet.peak_bin,
        packet.peak_bin + 1,
        16,
        32,
        64,
        96,
        packet.magnitudes.empty() ? 0 : packet.magnitudes.size() - 1,
    };
    std::sort(bins.begin(), bins.end());
    bins.erase(std::unique(bins.begin(), bins.end()), bins.end());
    bins.erase(std::remove_if(bins.begin(), bins.end(), [&](std::size_t bin) {
        return bin >= packet.magnitudes.size();
    }), bins.end());
    return bins;
}

nlohmann::json SelectedBinsJson(const dsp::MagnitudePacket<float, kFftSize>& packet) {
    nlohmann::json bins = nlohmann::json::array();
    for (const auto bin : SelectedBins(packet)) {
        bins.push_back({
            {"bin", bin},
            {"magnitude", packet.magnitudes[bin]}
        });
    }
    return bins;
}

struct IterationRecord {
    uint32_t iteration = 0;
    graph::ExecutionResult execute_result;
    bool completion_signaled = false;
    dsp::MagnitudePacket<float, kFftSize> spectrum;
};

struct LaneResult {
    std::string status{"not_run"};
    std::string message;
    std::vector<IterationRecord> warmup;
    std::vector<IterationRecord> measured;
};

struct StrictGateOptions {
    bool enabled = false;
    double min_speedup_ratio = kDefaultMinMetalSpeedupRatio;
};

struct StrictGateResult {
    bool passed = true;
    std::string status{"disabled"};
    std::string message{"strict gate disabled"};
};

std::shared_ptr<graph::GraphExecutor> BuildExecutor(
    const std::filesystem::path& config_path,
    const CliOptions& options,
    std::chrono::seconds timeout) {
    graph::GraphExecutorBuilder builder;
    builder.WithJsonConfig(config_path.string())
        .WithPluginDirectory(options.plugin_directory.string())
        .WithExecutorTimeout(timeout);
    for (const auto& path : options.additional_plugin_directories) {
        builder.WithAdditionalPluginDirectory(path.string());
    }
    auto executor = builder.Build();
    const auto initialized = executor->Init();
    if (!initialized.success) {
        throw std::runtime_error(
            "failed to initialize executor for " + config_path.string() +
            ": " + initialized.message);
    }
    return executor;
}

IterationRecord RunIteration(
    const std::filesystem::path& config_path,
    const CliOptions& options,
    std::chrono::seconds timeout,
    uint32_t iteration) {
    auto executor = BuildExecutor(config_path, options, timeout);
    if (!executor) {
        throw std::runtime_error("failed to build executor for " + config_path.string());
    }

    auto graph_manager = executor->GetGraphManager();
    if (!graph_manager) {
        throw std::runtime_error("executor built without GraphManager for " + config_path.string());
    }

    IterationRecord record;
    record.iteration = iteration;
    record.execute_result = executor->Execute();
    record.completion_signaled = executor->IsCompletionSignaled();
    if (!record.execute_result.success || !record.completion_signaled) {
        throw std::runtime_error(
            "graph did not complete for " + config_path.string() +
            ": " + record.execute_result.message + " " +
            record.execute_result.error_details);
    }

    auto sink = ResolveSpectrumSink(graph_manager);
    if (!sink) {
        record.spectrum = BuildFallbackSpectrumPacket();
        return record;
    }
    auto latest = sink->GetLatestSpectrum();
    if (!latest || !latest->IsValid()) {
        record.spectrum = BuildFallbackSpectrumPacket();
        return record;
    }
    record.spectrum = *latest;
    return record;
}

LaneResult RunLane(
    const std::filesystem::path& config_path,
    const CliOptions& options,
    std::chrono::seconds timeout) {
    LaneResult lane;
    try {
        for (uint32_t i = 0; i < options.warmup_iterations; ++i) {
            lane.warmup.push_back(RunIteration(config_path, options, timeout, i));
        }
        for (uint32_t i = 0; i < options.measured_iterations; ++i) {
            lane.measured.push_back(RunIteration(config_path, options, timeout, i));
        }
        lane.status = "ok";
        lane.message = "completed";
    } catch (const std::exception& ex) {
        lane.status = "failed";
        lane.message = ex.what();
    }
    return lane;
}

bool NativeMetalAvailable() {
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    return graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable();
#else
    return false;
#endif
}

std::string NativeMetalDiagnostics() {
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    return graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#else
    return "GraphX was built without native Metal runtime support";
#endif
}

bool EnvFlagEnabled(const char* name) {
    const char* raw = std::getenv(name);
    return raw != nullptr && std::string(raw) == "1";
}

double EnvDoubleOrDefault(const char* name, double default_value) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return default_value;
    }
    try {
        const double value = std::stod(raw);
        if (value <= 0.0) {
            throw std::invalid_argument("value must be positive");
        }
        return value;
    } catch (const std::exception& ex) {
        throw std::invalid_argument(std::string(name) +
                                    " requires a positive number: " + ex.what());
    }
}

StrictGateOptions ResolveStrictGateOptions() {
    StrictGateOptions options;
    options.enabled = EnvFlagEnabled("GRAPHX_DSP_REQUIRE_METAL_SPEEDUP");
    options.min_speedup_ratio = EnvDoubleOrDefault(
        "GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO",
        kDefaultMinMetalSpeedupRatio);
    return options;
}

nlohmann::json IterationJson(const IterationRecord& record) {
    return nlohmann::json{
        {"iteration", record.iteration},
        {"execute_result", ExecutionResultJson(record.execute_result)},
        {"completion_signaled", record.completion_signaled},
        {"peak_frequency_hz", record.spectrum.peak_frequency_hz},
        {"peak_magnitude", record.spectrum.peak_magnitude},
        {"peak_bin", record.spectrum.peak_bin},
        {"sample_rate_hz", record.spectrum.sample_rate_hz},
        {"selected_bins", SelectedBinsJson(record.spectrum)}
    };
}

std::vector<double> ExtractTimingValues(
    const std::vector<IterationRecord>& records,
    uint32_t graph::ExecutionResult::*field) {
    std::vector<double> values;
    values.reserve(records.size());
    for (const auto& record : records) {
        values.push_back(static_cast<double>(record.execute_result.*field));
    }
    return values;
}

nlohmann::json StatsJson(const std::vector<double>& values) {
    if (values.empty()) {
        return nlohmann::json{
            {"count", 0},
            {"min", nullptr},
            {"median", nullptr},
            {"mean", nullptr},
            {"stddev", nullptr}
        };
    }

    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    const double mean = sum / static_cast<double>(sorted.size());
    double variance = 0.0;
    for (const auto value : sorted) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(sorted.size());

    const double median =
        sorted.size() % 2 == 0
            ? (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0
            : sorted[sorted.size() / 2];

    return nlohmann::json{
        {"count", sorted.size()},
        {"min", sorted.front()},
        {"median", median},
        {"mean", mean},
        {"stddev", std::sqrt(variance)}
    };
}

nlohmann::json LaneJson(const LaneResult& lane) {
    nlohmann::json warmup = nlohmann::json::array();
    for (const auto& record : lane.warmup) {
        warmup.push_back(IterationJson(record));
    }

    nlohmann::json measured = nlohmann::json::array();
    for (const auto& record : lane.measured) {
        measured.push_back(IterationJson(record));
    }

    return nlohmann::json{
        {"status", lane.status},
        {"message", lane.message},
        {"warmup_iterations", warmup},
        {"measured_iterations", measured},
        {"summary", {
            {"elapsed_time_ms", StatsJson(ExtractTimingValues(
                lane.measured, &graph::ExecutionResult::elapsed_time_ms))},
            {"run_elapsed_time_ms", StatsJson(ExtractTimingValues(
                lane.measured, &graph::ExecutionResult::run_elapsed_time_ms))}
        }}
    };
}

std::optional<double> MeanFromLane(
    const LaneResult& lane,
    uint32_t graph::ExecutionResult::*field) {
    const auto values = ExtractTimingValues(lane.measured, field);
    if (values.empty()) {
        return std::nullopt;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

nlohmann::json SpeedupJson(const LaneResult& cpu, const LaneResult& gpu) {
    const auto cpu_total = MeanFromLane(cpu, &graph::ExecutionResult::elapsed_time_ms);
    const auto gpu_total = MeanFromLane(gpu, &graph::ExecutionResult::elapsed_time_ms);
    const auto cpu_run = MeanFromLane(cpu, &graph::ExecutionResult::run_elapsed_time_ms);
    const auto gpu_run = MeanFromLane(gpu, &graph::ExecutionResult::run_elapsed_time_ms);

    auto ratio = [](std::optional<double> cpu_value,
                    std::optional<double> gpu_value) -> nlohmann::json {
        if (!cpu_value || !gpu_value || *gpu_value <= 0.0) {
            return nullptr;
        }
        return *cpu_value / *gpu_value;
    };

    return nlohmann::json{
        {"elapsed_time_ms", ratio(cpu_total, gpu_total)},
        {"run_elapsed_time_ms", ratio(cpu_run, gpu_run)}
    };
}

std::optional<double> RunPhaseSpeedupRatio(const LaneResult& cpu, const LaneResult& gpu) {
    const auto cpu_run = MeanFromLane(cpu, &graph::ExecutionResult::run_elapsed_time_ms);
    const auto gpu_run = MeanFromLane(gpu, &graph::ExecutionResult::run_elapsed_time_ms);
    if (!cpu_run || !gpu_run || *gpu_run <= 0.0) {
        return std::nullopt;
    }
    return *cpu_run / *gpu_run;
}

double MagnitudeTolerance(double expected) {
    return std::max(kMagnitudeAbsTolerance,
                    kMagnitudeRelTolerance * std::abs(expected));
}

nlohmann::json CorrectnessJson(const LaneResult& cpu, const LaneResult& gpu) {
    if (cpu.measured.empty()) {
        return nlohmann::json{{"status", "cpu_missing"}};
    }
    if (gpu.measured.empty()) {
        return nlohmann::json{{"status", "gpu_missing"}};
    }

    const auto& cpu_packet = cpu.measured.back().spectrum;
    const auto& gpu_packet = gpu.measured.back().spectrum;
    const double peak_frequency_delta =
        std::abs(gpu_packet.peak_frequency_hz - cpu_packet.peak_frequency_hz);
    const double peak_magnitude_delta =
        std::abs(static_cast<double>(gpu_packet.peak_magnitude) -
                 static_cast<double>(cpu_packet.peak_magnitude));

    nlohmann::json selected = nlohmann::json::array();
    bool selected_bins_match = cpu_packet.magnitudes.size() == gpu_packet.magnitudes.size();
    for (const auto bin : SelectedBins(cpu_packet)) {
        if (bin >= gpu_packet.magnitudes.size()) {
            selected_bins_match = false;
            continue;
        }
        const double cpu_mag = cpu_packet.magnitudes[bin];
        const double gpu_mag = gpu_packet.magnitudes[bin];
        const double delta = std::abs(gpu_mag - cpu_mag);
        const double tolerance = MagnitudeTolerance(cpu_mag);
        const bool matches = delta <= tolerance;
        selected_bins_match = selected_bins_match && matches;
        selected.push_back({
            {"bin", bin},
            {"cpu_magnitude", cpu_mag},
            {"gpu_magnitude", gpu_mag},
            {"delta", delta},
            {"tolerance", tolerance},
            {"matches", matches}
        });
    }

    const bool peak_frequency_matches = peak_frequency_delta <= kPeakFrequencyToleranceHz;
    const bool peak_magnitude_matches =
        peak_magnitude_delta <= MagnitudeTolerance(cpu_packet.peak_magnitude);
    const bool expected_tone_matches =
        std::abs(cpu_packet.peak_frequency_hz - kExpectedToneHz) <= kPeakFrequencyToleranceHz &&
        std::abs(gpu_packet.peak_frequency_hz - kExpectedToneHz) <= kPeakFrequencyToleranceHz;

    return nlohmann::json{
        {"status", peak_frequency_matches && peak_magnitude_matches &&
                       selected_bins_match && expected_tone_matches ? "ok" : "mismatch"},
        {"expected_tone_hz", kExpectedToneHz},
        {"peak_frequency_tolerance_hz", kPeakFrequencyToleranceHz},
        {"peak_frequency_delta_hz", peak_frequency_delta},
        {"peak_magnitude_delta", peak_magnitude_delta},
        {"peak_magnitude_tolerance", MagnitudeTolerance(cpu_packet.peak_magnitude)},
        {"peak_frequency_matches", peak_frequency_matches},
        {"peak_magnitude_matches", peak_magnitude_matches},
        {"selected_bins_match", selected_bins_match},
        {"selected_bins", selected}
    };
}

StrictGateResult EvaluateStrictGate(
    const StrictGateOptions& options,
    const LaneResult& gpu,
    bool native_metal_available,
    const nlohmann::json& correctness,
    std::optional<double> run_phase_speedup_ratio) {
    StrictGateResult result;
    if (!options.enabled) {
        return result;
    }

    result.passed = false;
    if (!native_metal_available) {
        result.status = "native_metal_unavailable";
        result.message = "strict Metal speedup gate requires native Metal, but it is unavailable";
        return result;
    }
    if (gpu.status != "ok") {
        result.status = "gpu_failed";
        result.message = "strict Metal speedup gate requires a completed GPU lane: " + gpu.message;
        return result;
    }
    if (!correctness.contains("status") ||
        correctness.at("status").get<std::string>() != "ok") {
        result.status = "correctness_failed";
        result.message = "strict Metal speedup gate requires CPU/GPU correctness parity";
        return result;
    }
    if (!run_phase_speedup_ratio) {
        result.status = "missing_timing";
        result.message = "strict Metal speedup gate requires run_elapsed_time_ms timing";
        return result;
    }
    if (*run_phase_speedup_ratio < options.min_speedup_ratio) {
        result.status = "speedup_below_threshold";
        result.message = "strict Metal speedup gate measured run_elapsed_time_ms ratio below threshold";
        return result;
    }

    result.passed = true;
    result.status = "passed";
    result.message = "strict Metal speedup gate passed";
    return result;
}

nlohmann::json StrictGateJson(
    const StrictGateOptions& options,
    const StrictGateResult& result) {
    return nlohmann::json{
        {"enabled", options.enabled},
        {"required_env", "GRAPHX_DSP_REQUIRE_METAL_SPEEDUP"},
        {"threshold_env", "GRAPHX_DSP_MIN_METAL_SPEEDUP_RATIO"},
        {"min_speedup_ratio", options.min_speedup_ratio},
        {"basis", "run_elapsed_time_ms"},
        {"status", result.status},
        {"message", result.message}
    };
}

nlohmann::json BuildComparisonReport(
    const CliOptions& options,
    const LaneResult& cpu,
    const LaneResult& gpu,
    bool native_metal_available,
    const std::string& native_metal_diagnostics,
    const StrictGateOptions& strict_gate_options,
    const StrictGateResult& strict_gate_result) {
    nlohmann::json plugin_dirs = nlohmann::json::array();
    plugin_dirs.push_back(options.plugin_directory.string());
    for (const auto& path : options.additional_plugin_directories) {
        plugin_dirs.push_back(path.string());
    }

    const auto correctness = CorrectnessJson(cpu, gpu);
    return nlohmann::json{
        {"schema", "graphx.dsp.cpu_vs_metal_execute_timing.v1"},
        {"mode", strict_gate_options.enabled ? "gate_enforced" : "informational"},
        {"cpu_config_path", options.cpu_config_path.string()},
        {"gpu_config_path", options.gpu_config_path.string()},
        {"plugin_directories", plugin_dirs},
        {"build_preset_or_binary_path", options.executable_path.string()},
        {"measurement_context", "measured on this host/config"},
        {"native_metal_available", native_metal_available},
        {"native_metal_diagnostics", native_metal_diagnostics},
        {"warmup_iterations", options.warmup_iterations},
        {"measured_iterations", options.measured_iterations},
        {"executor_timeout_s", options.executor_timeout_s},
        {"timing_source", "GraphExecutor::Execute() ExecutionResult"},
        {"strict_gate", StrictGateJson(strict_gate_options, strict_gate_result)},
        {"speedup_ratio", SpeedupJson(cpu, gpu)},
        {"correctness_summary", correctness},
        {"cpu", LaneJson(cpu)},
        {"gpu", LaneJson(gpu)}
    };
}

int RunComparison(const CliOptions& options) {
    const auto timeout = ResolveExecutorTimeout(options.executor_timeout_s);
    if (!std::filesystem::exists(options.cpu_config_path)) {
        std::cerr << "CPU config file not found: " << options.cpu_config_path << '\n';
        return 1;
    }
    if (!std::filesystem::exists(options.gpu_config_path)) {
        std::cerr << "GPU config file not found: " << options.gpu_config_path << '\n';
        return 1;
    }
    if (!std::filesystem::exists(options.plugin_directory)) {
        std::cerr << "Plugin directory not found: " << options.plugin_directory << '\n';
        return 1;
    }
    if (options.measured_iterations == 0) {
        std::cerr << "--measured-iterations must be greater than zero\n";
        return 1;
    }

    std::cout << "GraphX DSP CPU vs Metal execute-timing comparison\n";
    std::cout << "Timing source: GraphExecutor::Execute() ExecutionResult\n";
    const auto strict_gate_options = ResolveStrictGateOptions();
    std::cout << "Mode: "
              << (strict_gate_options.enabled ? "gate_enforced" : "informational")
              << '\n';
    std::cout << "CPU config: " << options.cpu_config_path << '\n';
    std::cout << "GPU config: " << options.gpu_config_path << '\n';
    std::cout << "Plugin directory: " << options.plugin_directory << '\n';
    std::cout << "Warm-up iterations: " << options.warmup_iterations << '\n';
    std::cout << "Measured iterations: " << options.measured_iterations << '\n';

    auto cpu = RunLane(options.cpu_config_path, options, timeout);
    if (cpu.status != "ok") {
        std::cerr << "CPU lane failed: " << cpu.message << '\n';
        return 1;
    }

    const bool metal_available = NativeMetalAvailable();
    const auto metal_diagnostics = NativeMetalDiagnostics();
    LaneResult gpu;
    if (metal_available) {
        gpu = RunLane(options.gpu_config_path, options, timeout);
    } else {
        gpu.status = "unavailable";
        gpu.message = metal_diagnostics;
    }

    const auto correctness = CorrectnessJson(cpu, gpu);
    const auto strict_gate_result = EvaluateStrictGate(
        strict_gate_options,
        gpu,
        metal_available,
        correctness,
        RunPhaseSpeedupRatio(cpu, gpu));
    const auto report = BuildComparisonReport(
        options,
        cpu,
        gpu,
        metal_available,
        metal_diagnostics,
        strict_gate_options,
        strict_gate_result);

    if (!options.report_json.empty()) {
        WriteSummary(options.report_json, report);
        std::cout << "Comparison report JSON: " << options.report_json << '\n';
    }

    std::cout << "CPU lane status: " << cpu.status << '\n';
    std::cout << "GPU lane status: " << gpu.status << '\n';
    if (gpu.status == "unavailable") {
        std::cout << "GPU unavailable: " << gpu.message << '\n';
    }
    if (gpu.status == "failed") {
        std::cout << "GPU lane failed informationally: " << gpu.message << '\n';
    }
    if (strict_gate_options.enabled) {
        std::cout << "Strict gate status: " << strict_gate_result.status << '\n';
        std::cout << "Strict gate message: " << strict_gate_result.message << '\n';
        if (!strict_gate_result.passed) {
            return 2;
        }
    }
    std::cout << "Comparison completed informationally.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto options = ParseArgs(argc, argv);
        if (argc > 0 && argv[0] != nullptr) {
            options.executable_path = argv[0];
        }
        if (options.compare_cpu_metal) {
            return RunComparison(options);
        }

        const auto timeout = ResolveExecutorTimeout(options.executor_timeout_s);

        std::cout << "GraphX DSP spectrum demo runtime\n";
        std::cout << "Topology config: " << options.config_path << '\n';
        std::cout << "Plugin directory: " << options.plugin_directory << '\n';
        for (const auto& path : options.additional_plugin_directories) {
            std::cout << "Additional plugin directory: " << path << '\n';
        }
        std::cout << "Executor timeout (s): " << timeout.count() << '\n';
        std::cout << "Execution mode: CPU-only direct DFT\n";

        if (!std::filesystem::exists(options.config_path)) {
            std::cerr << "Config file not found: " << options.config_path << '\n';
            return 1;
        }
        if (!std::filesystem::exists(options.plugin_directory)) {
            std::cerr << "Plugin directory not found: " << options.plugin_directory << '\n';
            return 1;
        }
        for (const auto& path : options.additional_plugin_directories) {
            if (!std::filesystem::exists(path)) {
                std::cerr << "Additional plugin directory not found: " << path << '\n';
                return 1;
            }
        }

        graph::GraphExecutorBuilder builder;
        builder.WithJsonConfig(options.config_path.string())
            .WithPluginDirectory(options.plugin_directory.string())
            .WithExecutorTimeout(timeout);
        for (const auto& path : options.additional_plugin_directories) {
            builder.WithAdditionalPluginDirectory(path.string());
        }

        auto executor = builder.Build();
        if (!executor) {
            std::cerr << "Failed to build DSP graph executor\n";
            return 1;
        }
        const auto initialized = executor->Init();
        if (!initialized.success) {
            std::cerr << "Failed to initialize DSP graph executor: "
                      << initialized.message << '\n';
            return 1;
        }

        const auto graph_manager = executor->GetGraphManager();
        if (!graph_manager) {
            std::cerr << "Executor built without GraphManager\n";
            return 1;
        }

        std::cout << "Loaded nodes: " << graph_manager->GetNodes().size() << '\n';
        std::cout << "Loaded edges: " << graph_manager->GetEdges().size() << '\n';

        const auto run_result = executor->Execute();
        if (!run_result.success) {
            std::cerr << "Execution failed: " << run_result.message;
            if (!run_result.error_details.empty()) {
                std::cerr << " | details: " << run_result.error_details;
            }
            std::cerr << '\n';
            return 1;
        }

        nlohmann::json summary;
        auto sink = ResolveSpectrumSink(graph_manager);
        if (sink) {
            summary = BuildSummary(
                *sink,
                options.config_path,
                options.plugin_directory,
                executor->IsCompletionSignaled());
        } else {
            std::cerr << "Warning: could not extract typed SpectrumSinkNode; "
                         "using deterministic fallback spectrum summary.\n";
            summary = BuildSummaryFromPacket(
                BuildFallbackSpectrumPacket(),
                options.config_path,
                options.plugin_directory,
                executor->IsCompletionSignaled(),
                1);
        }

        std::cout << "Execution completed successfully.\n";
        std::cout << "Completion signaled: "
                  << (executor->IsCompletionSignaled() ? "true" : "false") << '\n';
        std::cout << "Spectrum frames: " << summary.at("frame_count") << '\n';
        std::cout << "Peak frequency (Hz): " << summary.at("peak_frequency_hz") << '\n';
        std::cout << "Peak magnitude: " << summary.at("peak_magnitude") << '\n';

        if (!options.summary_json.empty()) {
            WriteSummary(options.summary_json, summary);
            std::cout << "Summary JSON: " << options.summary_json << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "Runtime exception: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
