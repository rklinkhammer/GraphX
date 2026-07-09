// SPDX-License-Identifier: MIT

#include <chrono>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "config/JsonView.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/IConfigurable.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkConfig {
    std::string name;
    std::string host_class;
    std::string execution_mode;
    std::filesystem::path topology_path;
    std::string backend;
    std::size_t packet_size{256};
    std::size_t frame_count{24};
    std::size_t warmup_frame_count{4};
    double sample_rate_hz{48000.0};
    double tone_frequency_hz{1500.0};
    float amplitude{1.0F};
    bool strict_fallback{true};
    std::string correctness_family{"phase6"};
    std::filesystem::path imported_artifact;
    std::string note;
};

struct RunMetrics {
    double total_elapsed_ms{0.0};
    double steady_elapsed_ms{0.0};
    double frames_per_second{0.0};
    double samples_per_second{0.0};
    double latency_ms{0.0};
    double cold_frame_ms{0.0};
    double warm_frame_ms{0.0};
    double transfer_inclusive_gpu_time_ms{0.0};
    std::optional<double> compute_only_gpu_time_ms;
    std::optional<std::uint64_t> allocation_count;
    std::optional<std::uint64_t> allocation_bytes;
    double graph_overhead_ms{0.0};
    std::optional<double> cpu_gpu_speed_ratio;
    std::optional<double> legacy_reference_baseline_ms;
    std::string correctness_parity_status{"unknown"};
    std::string selected_backend{"unknown"};
    bool used_fallback{false};
    std::string fallback_diagnostic;
};

std::string ToLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string BackendToString(accelgraph::AcceleratorBackend backend) {
    switch (backend) {
        case accelgraph::AcceleratorBackend::Cpu:
            return "cpu";
        case accelgraph::AcceleratorBackend::Metal:
            return "metal";
        case accelgraph::AcceleratorBackend::Cuda:
            return "cuda";
    }
    return "unknown";
}

accelgraph::DeterministicIqPacket GeneratePacket(const BenchmarkConfig& cfg,
                                                 std::uint64_t packet_number) {
    accelgraph::DeterministicIqPacket packet;
    packet.sample_count = cfg.packet_size;
    packet.packet_number = packet_number;
    packet.sample_rate_hz = cfg.sample_rate_hz;
    packet.tone_frequency_hz = cfg.tone_frequency_hz;
    packet.amplitude = cfg.amplitude;
    packet.phase_radians = 0.0F;
    packet.i_samples.resize(cfg.packet_size);
    packet.q_samples.resize(cfg.packet_size);

    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
    for (std::size_t n = 0; n < cfg.packet_size; ++n) {
        const double phase = kTwoPi * cfg.tone_frequency_hz * static_cast<double>(n) /
                             cfg.sample_rate_hz;
        packet.i_samples[n] = cfg.amplitude * static_cast<float>(std::cos(phase));
        packet.q_samples[n] = cfg.amplitude * static_cast<float>(std::sin(phase));
    }

    return packet;
}

std::optional<accelgraph::MagnitudeSpectrumPacket>
RunCpuReferenceSpectrum(const accelgraph::DeterministicIqPacket& input) {
    if (input.sample_count < 2 || input.i_samples.size() != input.q_samples.size() ||
        input.sample_count != input.i_samples.size()) {
        return std::nullopt;
    }

    accelgraph::MagnitudeSpectrumPacket output;
    output.requested_backend = accelgraph::AcceleratorBackend::Cpu;
    output.selected_backend = accelgraph::AcceleratorBackend::Cpu;
    output.used_fallback = false;
    output.fft_size = input.sample_count;
    output.sample_rate_hz = input.sample_rate_hz;
    output.packet_number = input.packet_number;

    const std::size_t fft_size = input.sample_count;
    const std::size_t bins = fft_size / 2;
    output.magnitudes.assign(bins, 0.0F);

    double best_mag = -1.0;
    std::size_t best_bin = 0;
    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;

    for (std::size_t bin = 0; bin < bins; ++bin) {
        std::complex<double> accum{0.0, 0.0};
        for (std::size_t n = 0; n < fft_size; ++n) {
            const std::complex<double> sample(
                static_cast<double>(input.i_samples[n]),
                static_cast<double>(input.q_samples[n]));
            const double angle = -kTwoPi * static_cast<double>(bin) * static_cast<double>(n) /
                                 static_cast<double>(fft_size);
            accum += sample * std::complex<double>(std::cos(angle), std::sin(angle));
        }

        const double mag = std::abs(accum) / static_cast<double>(fft_size);
        output.magnitudes[bin] = static_cast<float>(mag);
        if (mag > best_mag) {
            best_mag = mag;
            best_bin = bin;
        }
    }

    output.peak_bin = best_bin;
    output.peak_magnitude = output.magnitudes[best_bin];
    output.peak_frequency_hz = static_cast<double>(best_bin) * output.sample_rate_hz /
                               static_cast<double>(fft_size);
    return output;
}

template <typename NodeT>
std::shared_ptr<NodeT> ResolveNode(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        auto typed = wrapper->GetNode<NodeT>();
        if (typed) {
            return typed;
        }
    }

    return nullptr;
}

void ConfigureNode(const std::shared_ptr<graph::INode>& node, const nlohmann::json& cfg) {
    if (!node) {
        throw std::runtime_error("node is null during ConfigureNode");
    }
    auto* configurable = dynamic_cast<graph::IConfigurable*>(node.get());
    if (!configurable) {
        throw std::runtime_error("node is not configurable");
    }
    configurable->Configure(graph::JsonView(cfg));
}

BenchmarkConfig LoadConfig(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("failed to open benchmark config: " + path.string());
    }

    nlohmann::json j;
    in >> j;

    BenchmarkConfig cfg;
    cfg.name = j.value("name", path.stem().string());
    cfg.host_class = j.value("host_class", "cpu-only");
    cfg.execution_mode = j.value("execution_mode", "local");
    cfg.topology_path = j.value("topology_path", "");
    cfg.backend = ToLower(j.value("backend", "cpu"));
    cfg.packet_size = static_cast<std::size_t>(j.value("packet_size", 256));
    cfg.frame_count = static_cast<std::size_t>(j.value("frame_count", 24));
    cfg.warmup_frame_count = static_cast<std::size_t>(j.value("warmup_frame_count", 4));
    cfg.sample_rate_hz = j.value("sample_rate_hz", 48000.0);
    cfg.tone_frequency_hz = j.value("tone_frequency_hz", 1500.0);
    cfg.amplitude = j.value("amplitude", 1.0F);
    cfg.strict_fallback = j.value("strict_fallback", true);
    cfg.correctness_family = j.value("correctness_family", "phase6");
    cfg.imported_artifact = j.value("imported_artifact", "");
    cfg.note = j.value("note", "");
    return cfg;
}

double MeanMs(const std::vector<double>& samples) {
    if (samples.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return sum / static_cast<double>(samples.size());
}

std::optional<double> MeasureLegacyReferenceBaselineMs(const std::filesystem::path& repo_root,
                                                       std::size_t frame_count,
                                                       std::size_t warmup_frame_count,
                                                       const std::filesystem::path& plugin_dir) {
    const auto legacy_config = repo_root / "libdsp/config/dsp_sine_fft_spectrum_256.json";
    if (!std::filesystem::exists(legacy_config)) {
        return std::nullopt;
    }

    std::vector<double> frame_samples;
    const std::size_t total = frame_count + warmup_frame_count;
    for (std::size_t i = 0; i < total; ++i) {
        auto executor = graph::GraphExecutorBuilder()
                            .WithJsonConfig(legacy_config.string())
                            .WithPluginDirectory(plugin_dir.string())
                            .WithExecutorTimeout(std::chrono::seconds(20))
                            .Build();
        if (!executor) {
            return std::nullopt;
        }

        const auto start = Clock::now();
        const auto run = executor->Execute();
        const auto end = Clock::now();
        if (!run.success) {
            return std::nullopt;
        }

        if (i >= warmup_frame_count) {
            frame_samples.push_back(
                std::chrono::duration<double, std::milli>(end - start).count());
        }
    }

    return MeanMs(frame_samples);
}

RunMetrics RunLocalBenchmark(const BenchmarkConfig& cfg,
                             const std::filesystem::path& repo_root,
                             const std::filesystem::path& plugin_dir) {
    RunMetrics metrics;

    const auto topology = repo_root / cfg.topology_path;
    if (!std::filesystem::exists(topology)) {
        throw std::runtime_error("topology path not found: " + topology.string());
    }

    accelgraph::SpectrumAnalysisNode direct_analysis;
    direct_analysis.Configure(graph::JsonView(nlohmann::json{{"backend", cfg.backend},
                                                             {"strict_fallback", cfg.strict_fallback}}));

    std::vector<double> measured_frame_ms;
    std::vector<double> measured_direct_ms;

    const std::size_t total_frames = cfg.warmup_frame_count + cfg.frame_count;
    const auto total_start = Clock::now();

    std::optional<accelgraph::MagnitudeSpectrumPacket> last_output;

    for (std::size_t frame = 0; frame < total_frames; ++frame) {
        auto executor = graph::GraphExecutorBuilder()
                            .WithJsonConfig(topology.string())
                            .WithPluginDirectory(plugin_dir.string())
                            .WithExecutorTimeout(std::chrono::seconds(20))
                            .Build();
        if (!executor) {
            throw std::runtime_error("failed to build GraphExecutor for " + cfg.name);
        }

        auto graph_manager = executor->GetGraphManager();
        if (!graph_manager) {
            throw std::runtime_error("missing GraphManager for " + cfg.name);
        }

        auto source = ResolveNode<accelgraph::SineWaveSourceNode>(graph_manager);
        auto analysis = ResolveNode<accelgraph::SpectrumAnalysisNode>(graph_manager);
        auto sink = ResolveNode<accelgraph::SpectrumSinkNode>(graph_manager);
        if (!source || !analysis || !sink) {
            throw std::runtime_error("failed to resolve source/analysis/sink nodes for " + cfg.name);
        }

        ConfigureNode(analysis, nlohmann::json{{"backend", cfg.backend},
                                               {"strict_fallback", cfg.strict_fallback}});
        ConfigureNode(sink, nlohmann::json::object());

        ConfigureNode(source, nlohmann::json{{"sample_count", static_cast<int>(cfg.packet_size)},
                                             {"sample_rate_hz", cfg.sample_rate_hz},
                                             {"tone_frequency_hz", cfg.tone_frequency_hz},
                                             {"amplitude", cfg.amplitude},
                                             {"phase_radians", 0.0},
                                             {"packet_number", static_cast<long long>(frame + 1)}});

        const auto start = Clock::now();
        const auto run = executor->Execute();
        const auto end = Clock::now();
        if (!run.success) {
            throw std::runtime_error("graph execute failed for " + cfg.name + ": " +
                                     run.message + " " + run.error_details);
        }

        auto output = sink->LastSpectrum();
        if (!output.has_value()) {
            throw std::runtime_error("sink produced no output spectrum for " + cfg.name);
        }
        last_output = output;

        if (frame >= cfg.warmup_frame_count) {
            const double elapsed_ms =
                std::chrono::duration<double, std::milli>(end - start).count();
            measured_frame_ms.push_back(elapsed_ms);

            auto packet = GeneratePacket(cfg, static_cast<std::uint64_t>(frame + 1));
            const auto direct_start = Clock::now();
            auto direct = direct_analysis.Execute(packet);
            const auto direct_end = Clock::now();
            if (direct.has_value()) {
                measured_direct_ms.push_back(
                    std::chrono::duration<double, std::milli>(direct_end - direct_start).count());
            }
        }
    }

    const auto total_end = Clock::now();

    metrics.total_elapsed_ms =
        std::chrono::duration<double, std::milli>(total_end - total_start).count();
    metrics.steady_elapsed_ms = std::accumulate(measured_frame_ms.begin(),
                                                measured_frame_ms.end(),
                                                0.0);
    metrics.latency_ms = MeanMs(measured_frame_ms);
    metrics.cold_frame_ms = measured_frame_ms.empty() ? 0.0 : measured_frame_ms.front();
    if (measured_frame_ms.size() > 1) {
        std::vector<double> warm_only(measured_frame_ms.begin() + 1, measured_frame_ms.end());
        metrics.warm_frame_ms = MeanMs(warm_only);
    } else {
        metrics.warm_frame_ms = metrics.latency_ms;
    }

    if (metrics.steady_elapsed_ms > 0.0) {
        metrics.frames_per_second =
            static_cast<double>(cfg.frame_count) * 1000.0 / metrics.steady_elapsed_ms;
        metrics.samples_per_second =
            static_cast<double>(cfg.frame_count * cfg.packet_size) * 1000.0 /
            metrics.steady_elapsed_ms;
    }

    const double direct_avg = MeanMs(measured_direct_ms);
    metrics.transfer_inclusive_gpu_time_ms = (cfg.backend == "metal" || cfg.backend == "cuda")
                                                 ? metrics.latency_ms
                                                 : 0.0;
    metrics.graph_overhead_ms = std::max(0.0, metrics.latency_ms - direct_avg);

    auto reference_packet = GeneratePacket(cfg, 999);
    auto reference = RunCpuReferenceSpectrum(reference_packet);
    if (!reference.has_value() || !last_output.has_value()) {
        metrics.correctness_parity_status = "fail:no-reference";
    } else {
        const auto& expected = reference.value();
        const auto& actual = last_output.value();
        const bool peak_match = expected.peak_bin == actual.peak_bin;
        bool bins_match = true;
        const std::vector<std::size_t> bins{expected.peak_bin,
                                            expected.peak_bin > 0 ? expected.peak_bin - 1 : 0,
                                            expected.peak_bin + 1};
        for (const auto bin : bins) {
            if (bin >= expected.magnitudes.size() || bin >= actual.magnitudes.size()) {
                continue;
            }
            const float diff = std::abs(expected.magnitudes[bin] - actual.magnitudes[bin]);
            if (diff > 1.0e-3F) {
                bins_match = false;
                break;
            }
        }

        metrics.correctness_parity_status = (peak_match && bins_match) ? "pass" : "fail:parity";
        metrics.selected_backend = BackendToString(actual.selected_backend);
        metrics.used_fallback = actual.used_fallback;
        metrics.fallback_diagnostic = actual.fallback_diagnostic;
    }

    metrics.legacy_reference_baseline_ms =
        MeasureLegacyReferenceBaselineMs(repo_root,
                                         cfg.frame_count,
                                         cfg.warmup_frame_count,
                                         plugin_dir);

    return metrics;
}

nlohmann::json ImportedResultFromArtifact(const BenchmarkConfig& cfg,
                                          const std::filesystem::path& repo_root) {
    const auto pending_row = [&](const std::string& correctness_status,
                                 const std::string& pending_note) {
        return nlohmann::json{
            {"graph_configuration_name", cfg.name},
            {"backend", cfg.backend},
            {"execution_mode", cfg.execution_mode},
            {"host_class", cfg.host_class},
            {"packet_size", cfg.packet_size},
            {"frame_count", cfg.frame_count},
            {"warmup_frame_count", cfg.warmup_frame_count},
            {"total_elapsed_time_ms", nullptr},
            {"steady_state_elapsed_time_ms", nullptr},
            {"frames_per_second", nullptr},
            {"samples_per_second", nullptr},
            {"latency_ms", nullptr},
            {"cold_frame_ms", nullptr},
            {"warm_frame_ms", nullptr},
            {"transfer_inclusive_gpu_time_ms", nullptr},
            {"compute_only_gpu_time_ms", nullptr},
            {"allocation_count", nullptr},
            {"allocation_bytes", nullptr},
            {"graph_overhead_ms", nullptr},
            {"cpu_gpu_speed_ratio", nullptr},
            {"legacy_reference_baseline_ms", nullptr},
            {"correctness_family", cfg.correctness_family},
            {"correctness_parity_status", correctness_status},
            {"selected_backend", nullptr},
            {"used_fallback", nullptr},
            {"fallback_diagnostic", nullptr},
            {"measurement_origin",
             {
                 {"measured_locally", false},
                 {"imported", true},
                 {"source_artifact", (repo_root / cfg.imported_artifact).string()},
                 {"note", pending_note.empty() ? nullptr : nlohmann::json(pending_note)},
             }},
        };
    };

    const auto artifact = repo_root / cfg.imported_artifact;
    std::string correctness_status = "pending:imported-correctness-only";
    std::string note = cfg.note;

    if (!std::filesystem::exists(artifact)) {
        if (!note.empty()) {
            note += " ";
        }
        note += "Imported artifact not found at configured path.";
        return pending_row(correctness_status, note);
    }

    std::ifstream in(artifact);
    nlohmann::json imported;
    in >> imported;

    const std::string imported_phase = imported.value("phase", "");
    const std::string imported_schema = imported.value("schema", "");

    if (imported_phase == "7" &&
        imported_schema == "graphx.accelgraph.phase7.spectrum.benchmark.v1") {
        const auto results_it = imported.find("results");
        if (results_it == imported.end() || !results_it->is_array()) {
            throw std::runtime_error("phase-7 imported artifact missing results array: " +
                                     artifact.string());
        }

        const nlohmann::json* matched = nullptr;
        for (const auto& row : *results_it) {
            if (!row.is_object()) {
                continue;
            }
            if (!row.contains("backend") || !row["backend"].is_string()) {
                continue;
            }
            if (ToLower(row["backend"].get<std::string>()) != ToLower(cfg.backend)) {
                continue;
            }

            if (row.contains("host_class") && row["host_class"].is_string() &&
                ToLower(row["host_class"].get<std::string>()) == ToLower(cfg.host_class)) {
                matched = &row;
                break;
            }
            if (!matched) {
                matched = &row;
            }
        }

        if (!matched) {
            if (!note.empty()) {
                note += " ";
            }
            note += "Phase-7 artifact present but no matching backend row found.";
            return pending_row("pending:phase7-artifact-missing-backend-row", note);
        }

        static const std::vector<std::string> kRequiredImportKeys = {
            "backend",
            "host_class",
            "packet_size",
            "frame_count",
            "warmup_frame_count",
            "correctness_parity_status",
            "measurement_origin",
        };
        for (const auto& key : kRequiredImportKeys) {
            if (!matched->contains(key)) {
                throw std::runtime_error("phase-7 imported row missing key '" + key + "' in " +
                                         artifact.string());
            }
        }

        nlohmann::json out = *matched;
        out["graph_configuration_name"] = cfg.name;
        out["execution_mode"] = "imported";
        if (!out.contains("correctness_family") || !out["correctness_family"].is_string()) {
            out["correctness_family"] = cfg.correctness_family;
        }

        std::string import_note = cfg.note;
        if (!import_note.empty()) {
            import_note += " ";
        }
        import_note += "Imported metrics from phase-7 artifact " + artifact.filename().string();

        out["measurement_origin"] = {
            {"measured_locally", false},
            {"imported", true},
            {"source_artifact", artifact.string()},
            {"note", import_note},
        };
        return out;
    }

    {
        const auto tests = imported.value("tests", nlohmann::json::object());
        const auto passed = tests.value("passed", nlohmann::json::array());
        bool phase6b_ok = false;
        for (const auto& t : passed) {
            if (!t.is_string()) {
                continue;
            }
            if (t.get<std::string>() ==
                "AccelGraphPhase6BCudaSpectrumTest.CpuCudaParityAndStrictNativeExecutionViaGraphExecutor") {
                phase6b_ok = true;
                break;
            }
        }
        if (phase6b_ok) {
            correctness_status = "pass:imported-correctness";
        }
    }
    if (!note.empty()) {
        note += " ";
    }
    note += "Imported correctness-only artifact from " + artifact.filename().string();

    return pending_row(correctness_status, note);
}

void ApplyOverrides(BenchmarkConfig& cfg,
                    const std::optional<std::size_t>& frames_override,
                    const std::optional<std::size_t>& warmup_override) {
    if (frames_override.has_value()) {
        cfg.frame_count = frames_override.value();
    }
    if (warmup_override.has_value()) {
        cfg.warmup_frame_count = warmup_override.value();
    }
}

std::string UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path repo_root =
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        const std::filesystem::path plugin_dir = std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);

        std::vector<std::filesystem::path> config_paths;
        std::optional<std::size_t> frames_override;
        std::optional<std::size_t> warmup_override;
        std::filesystem::path output_path =
            repo_root / "verification/accelgraph/phase-7/macos-local-latest.json";

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.rfind("--config=", 0) == 0) {
                config_paths.emplace_back(arg.substr(std::string("--config=").size()));
                continue;
            }
            if (arg.rfind("--output=", 0) == 0) {
                output_path = arg.substr(std::string("--output=").size());
                continue;
            }
            if (arg.rfind("--frames=", 0) == 0) {
                frames_override = static_cast<std::size_t>(
                    std::stoul(arg.substr(std::string("--frames=").size())));
                continue;
            }
            if (arg.rfind("--warmup=", 0) == 0) {
                warmup_override = static_cast<std::size_t>(
                    std::stoul(arg.substr(std::string("--warmup=").size())));
                continue;
            }
            if (arg == "--all-default-configs") {
                config_paths.emplace_back("libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cpu_macos.json");
                config_paths.emplace_back("libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_metal_macos.json");
                config_paths.emplace_back("libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cuda_jetson.json");
                continue;
            }
            if (arg == "--help") {
                std::cout << "Usage: accelgraph_phase7_benchmark [--config=<path>]... [--all-default-configs] "
                          << "[--frames=<n>] [--warmup=<n>] [--output=<path>]\n";
                return 0;
            }
            throw std::runtime_error("unknown argument: " + arg);
        }

        if (config_paths.empty()) {
            config_paths.emplace_back("libaccelgraph/test/config/benchmarks/accelgraph_phase7_spectrum_cpu_macos.json");
        }

        nlohmann::json report;
        report["schema"] = "graphx.accelgraph.phase7.spectrum.benchmark.v1";
        report["phase"] = "7";
        report["generated_at_utc"] = UtcNow();
        report["host"] = {
            {"hostname", std::getenv("HOSTNAME") ? std::getenv("HOSTNAME") : "unknown"},
#if defined(__APPLE__)
            {"operating_system", "macOS"},
#elif defined(__linux__)
            {"operating_system", "Linux"},
#else
            {"operating_system", "unknown"},
#endif
#if defined(__aarch64__)
            {"architecture", "aarch64"},
#elif defined(__x86_64__)
            {"architecture", "x86_64"},
#else
            {"architecture", "unknown"},
#endif
        };

        std::vector<nlohmann::json> results;
        std::optional<double> cpu_latency_ms;

        for (const auto& rel_path : config_paths) {
            const auto cfg_path = rel_path.is_absolute() ? rel_path : (repo_root / rel_path);
            BenchmarkConfig cfg = LoadConfig(cfg_path);
            ApplyOverrides(cfg, frames_override, warmup_override);

            if (ToLower(cfg.execution_mode) == "imported") {
                results.push_back(ImportedResultFromArtifact(cfg, repo_root));
                continue;
            }

            const auto metrics = RunLocalBenchmark(cfg, repo_root, plugin_dir);
            if (cfg.backend == "cpu") {
                cpu_latency_ms = metrics.latency_ms;
            }

            nlohmann::json row = {
                {"graph_configuration_name", cfg.name},
                {"backend", cfg.backend},
                {"execution_mode", cfg.execution_mode},
                {"host_class", cfg.host_class},
                {"packet_size", cfg.packet_size},
                {"frame_count", cfg.frame_count},
                {"warmup_frame_count", cfg.warmup_frame_count},
                {"total_elapsed_time_ms", metrics.total_elapsed_ms},
                {"steady_state_elapsed_time_ms", metrics.steady_elapsed_ms},
                {"frames_per_second", metrics.frames_per_second},
                {"samples_per_second", metrics.samples_per_second},
                {"latency_ms", metrics.latency_ms},
                {"cold_frame_ms", metrics.cold_frame_ms},
                {"warm_frame_ms", metrics.warm_frame_ms},
                {"transfer_inclusive_gpu_time_ms", metrics.transfer_inclusive_gpu_time_ms},
                {"compute_only_gpu_time_ms", metrics.compute_only_gpu_time_ms.has_value() ? nlohmann::json(metrics.compute_only_gpu_time_ms.value()) : nlohmann::json(nullptr)},
                {"allocation_count", metrics.allocation_count.has_value() ? nlohmann::json(metrics.allocation_count.value()) : nlohmann::json(nullptr)},
                {"allocation_bytes", metrics.allocation_bytes.has_value() ? nlohmann::json(metrics.allocation_bytes.value()) : nlohmann::json(nullptr)},
                {"graph_overhead_ms", metrics.graph_overhead_ms},
                {"cpu_gpu_speed_ratio", nullptr},
                {"legacy_reference_baseline_ms", metrics.legacy_reference_baseline_ms.has_value() ? nlohmann::json(metrics.legacy_reference_baseline_ms.value()) : nlohmann::json(nullptr)},
                {"correctness_family", cfg.correctness_family},
                {"correctness_parity_status", metrics.correctness_parity_status},
                {"selected_backend", metrics.selected_backend},
                {"used_fallback", metrics.used_fallback},
                {"fallback_diagnostic", metrics.fallback_diagnostic.empty() ? nlohmann::json(nullptr) : nlohmann::json(metrics.fallback_diagnostic)},
                {"measurement_origin",
                 {
                     {"measured_locally", true},
                     {"imported", false},
                     {"source_artifact", nullptr},
                     {"note", nullptr},
                 }},
            };

            if (cpu_latency_ms.has_value() && cfg.backend != "cpu" && metrics.latency_ms > 0.0) {
                row["cpu_gpu_speed_ratio"] = cpu_latency_ms.value() / metrics.latency_ms;
            }

            results.push_back(std::move(row));
        }

        report["results"] = results;

        std::filesystem::create_directories(output_path.parent_path());
        std::ofstream out(output_path);
        out << std::setw(2) << report << "\n";

        std::cout << "Wrote Phase 7 benchmark report: " << output_path << "\n";
        for (const auto& row : report["results"]) {
            std::cout << "- " << row.at("graph_configuration_name").get<std::string>()
                      << " backend=" << row.at("backend").get<std::string>()
                      << " origin="
                      << (row.at("measurement_origin").at("imported").get<bool>() ? "imported"
                                                                           : "local")
                      << " parity=" << row.at("correctness_parity_status").get<std::string>()
                      << "\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Phase 7 benchmark failed: " << ex.what() << "\n";
        return 1;
    }
}
