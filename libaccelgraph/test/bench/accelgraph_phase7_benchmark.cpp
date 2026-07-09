// SPDX-License-Identifier: MIT

#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
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
    std::optional<accelgraph::MagnitudeSpectrumPacket> last_spectrum;
};

struct RepoIdentity {
    std::string branch;
    std::string commit_sha;
    nlohmann::json diff_identity;
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

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string CaptureCommand(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }
    (void)pclose(pipe);
    return Trim(output);
}

bool SpectrumParityPasses(const accelgraph::MagnitudeSpectrumPacket& cpu,
                         const accelgraph::MagnitudeSpectrumPacket& candidate) {
    if (cpu.magnitudes.size() != candidate.magnitudes.size()) {
        return false;
    }
    if (cpu.peak_bin != candidate.peak_bin) {
        return false;
    }

    const std::vector<std::size_t> bins{
        cpu.peak_bin,
        cpu.peak_bin > 0 ? cpu.peak_bin - 1 : cpu.peak_bin,
        cpu.peak_bin + 1 < cpu.magnitudes.size() ? cpu.peak_bin + 1 : cpu.peak_bin,
    };
    for (const auto bin : bins) {
        if (bin >= cpu.magnitudes.size() || bin >= candidate.magnitudes.size()) {
            continue;
        }
        const float diff = std::abs(cpu.magnitudes[bin] - candidate.magnitudes[bin]);
        if (diff > 1.0e-3F) {
            return false;
        }
    }
    return true;
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
                             const std::filesystem::path& plugin_dir,
                             const std::optional<accelgraph::MagnitudeSpectrumPacket>& cpu_reference) {
    RunMetrics metrics;

    const auto topology = repo_root / cfg.topology_path;
    if (!std::filesystem::exists(topology)) {
        throw std::runtime_error("topology path not found: " + topology.string());
    }

    std::vector<double> measured_frame_ms;
    std::vector<double> measured_run_phase_ms;

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
            measured_run_phase_ms.push_back(static_cast<double>(run.run_elapsed_time_ms));
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

    const double run_phase_avg = MeanMs(measured_run_phase_ms);
    metrics.transfer_inclusive_gpu_time_ms = (cfg.backend == "metal" || cfg.backend == "cuda")
                                                 ? run_phase_avg
                                                 : 0.0;
    metrics.graph_overhead_ms = std::max(0.0, metrics.latency_ms - run_phase_avg);

    if (!last_output.has_value()) {
        metrics.correctness_parity_status = "fail:no-reference";
    } else {
        const auto& actual = last_output.value();
        metrics.selected_backend = BackendToString(actual.selected_backend);
        metrics.used_fallback = actual.used_fallback;
        metrics.fallback_diagnostic = actual.fallback_diagnostic;

        if (cfg.backend == "cpu") {
            metrics.correctness_parity_status = "pass:cpu-benchmark-family-baseline";
        } else if (cpu_reference.has_value()) {
            metrics.correctness_parity_status = SpectrumParityPasses(cpu_reference.value(), actual)
                                                    ? "pass"
                                                    : "fail:parity";
        } else {
            metrics.correctness_parity_status = "pending:missing-cpu-benchmark-family-baseline";
        }
    }

    metrics.last_spectrum = last_output;

    metrics.legacy_reference_baseline_ms =
        MeasureLegacyReferenceBaselineMs(repo_root,
                                         cfg.frame_count,
                                         cfg.warmup_frame_count,
                                         plugin_dir);

    return metrics;
}

nlohmann::json ImportedResultFromArtifact(const BenchmarkConfig& cfg,
                                          const std::filesystem::path& repo_root,
                                          const RepoIdentity& current_identity) {
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

    if (imported_phase == "7" && imported_schema == "graphx.accelgraph.phase7.spectrum.benchmark.v1") {
        const std::string imported_branch = imported.value("branch", "");
        const std::string imported_commit = imported.value("commit_sha", "");
        const bool identity_present = !imported_branch.empty() && !imported_commit.empty();
        if (!identity_present || imported_branch != current_identity.branch ||
            imported_commit != current_identity.commit_sha) {
            if (!note.empty()) {
                note += " ";
            }
            note += "Phase-7 artifact identity mismatch or missing identity fields."
                    " Expected branch=" + current_identity.branch +
                    " commit=" + current_identity.commit_sha + ".";
            return pending_row("pending:phase7-import-identity-mismatch", note);
        }

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

RepoIdentity ReadRepoIdentity() {
    RepoIdentity identity;
    identity.branch = CaptureCommand("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    identity.commit_sha = CaptureCommand("git rev-parse HEAD 2>/dev/null");
    const std::string status = CaptureCommand("git status --porcelain 2>/dev/null | head -n 1");
    const bool dirty = !status.empty();
    identity.diff_identity = {
        {"type", "working_tree"},
        {"value", dirty ? "uncommitted_changes_present" : "clean"},
        {"working_tree_dirty", dirty},
    };
    return identity;
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

        const RepoIdentity repo_identity = ReadRepoIdentity();

        std::vector<nlohmann::json> results;
        std::optional<double> cpu_latency_ms;
        std::optional<accelgraph::MagnitudeSpectrumPacket> cpu_reference_spectrum;

        for (const auto& rel_path : config_paths) {
            const auto cfg_path = rel_path.is_absolute() ? rel_path : (repo_root / rel_path);
            BenchmarkConfig cfg = LoadConfig(cfg_path);
            ApplyOverrides(cfg, frames_override, warmup_override);

            if (ToLower(cfg.execution_mode) == "imported") {
                results.push_back(ImportedResultFromArtifact(cfg, repo_root, repo_identity));
                continue;
            }

            const auto metrics = RunLocalBenchmark(cfg, repo_root, plugin_dir, cpu_reference_spectrum);
            if (cfg.backend == "cpu") {
                cpu_latency_ms = metrics.latency_ms;
                cpu_reference_spectrum = metrics.last_spectrum;
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
        report["branch"] = repo_identity.branch;
        report["commit_sha"] = repo_identity.commit_sha;
        report["diff_identity"] = repo_identity.diff_identity;

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
