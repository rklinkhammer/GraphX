#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncNode.hpp"
#include "sar/H2DAsyncNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/RangeCompressionNode.hpp"
#include "sar/RangeWindowNode.hpp"
#include "sar/SarBackprojectionTransformNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SyntheticApertureIqSourceNode.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef SAR_PLUGIN_OUTPUT_DIRECTORY
#define SAR_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkProfile {
    std::string name;
    std::uint32_t pulses;
    std::uint32_t samples_per_pulse;
    std::uint32_t tile_count;
    int warmup_runs;
    int measured_runs;
};

enum class RangeStageKind : std::uint8_t {
    Window,
    Compression,
};

struct BenchmarkOptions {
    BenchmarkProfile profile;
    std::optional<std::filesystem::path> trace_output_path{};
    bool evaluate_device_reduce{false};
    bool native_backend{false};
    RangeStageKind range_stage{RangeStageKind::Window};
};

struct BenchmarkStats {
    double min_ms{0.0};
    double median_ms{0.0};
    double max_ms{0.0};
    double stddev_ms{0.0};
};

struct GraphRunResult {
    double build_ms{0.0};
    double run_ms{0.0};
    double lifecycle_total_ms{0.0};
    double init_ms{0.0};
    double start_ms{0.0};
    double stop_ms{0.0};
    double join_ms{0.0};
    sar::SarDiagnosticsMessage diagnostics{};
    sar::SarMergeStatusMessage last_status{};
    std::uint64_t queue_backpressure_events{0};
    std::uint64_t peak_queue_depth{0};
};

struct BaselineRunResult {
    double execute_ms{0.0};
    sar::SarDiagnosticsMessage diagnostics{};
};

struct DeviceReduceEvaluation {
    bool enabled{false};
    bool diagnostics_match{false};
    BenchmarkStats prototype_exec_stats{};
    BaselineRunResult last_prototype{};
    std::string decision{"not-evaluated"};
    std::string rationale{};
};

sar::SarFrameMarker DecodeMarker(std::uint64_t token) {
    return static_cast<sar::SarFrameMarker>(token & 0x3u);
}

std::uint32_t DecodeTileId(std::uint64_t token) {
    return static_cast<std::uint32_t>((token >> 2u) & 0xFFFu);
}

std::uint64_t DecodeSequenceId(std::uint64_t token) {
    return (token >> 14u) & 0xFFFFFFu;
}

std::size_t DecodeByteCount(std::uint64_t token) {
    return static_cast<std::size_t>((token >> 38u) & 0xFFFFu);
}

std::shared_ptr<sar::SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "SarDiagnosticsSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarDiagnosticsSinkNode>();
    }

    return nullptr;
}

BenchmarkOptions ParseOptions(int argc, char** argv) {
    BenchmarkProfile ci{
        .name = "ci",
        .pulses = 32,
        .samples_per_pulse = 256,
        .tile_count = 4,
        .warmup_runs = 1,
        .measured_runs = 5,
    };

    BenchmarkProfile local{
        .name = "local",
        .pulses = 128,
        .samples_per_pulse = 1024,
        .tile_count = 8,
        .warmup_runs = 2,
        .measured_runs = 10,
    };

    BenchmarkOptions options{.profile = ci};

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile=ci" || arg == "ci") {
            options.profile = ci;
            continue;
        }
        if (arg == "--profile=local" || arg == "local") {
            options.profile = local;
            continue;
        }

        const std::string trace_out_prefix = "--trace-out=";
        const std::string trace_prefix = "--trace=";
        if (arg.rfind(trace_out_prefix, 0) == 0) {
            options.trace_output_path = arg.substr(trace_out_prefix.size());
            continue;
        }
        if (arg.rfind(trace_prefix, 0) == 0) {
            options.trace_output_path = arg.substr(trace_prefix.size());
            continue;
        }
        if (arg == "--trace-out" || arg == "--trace") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--trace-out requires a path argument");
            }
            options.trace_output_path = argv[++i];
            continue;
        }

        if (arg == "--evaluate-device-reduce") {
            options.evaluate_device_reduce = true;
            continue;
        }

        const std::string range_stage_prefix = "--range-stage=";
        if (arg.rfind(range_stage_prefix, 0) == 0) {
            const auto value = arg.substr(range_stage_prefix.size());
            if (value == "window") {
                options.range_stage = RangeStageKind::Window;
                continue;
            }
            if (value == "compression") {
                options.range_stage = RangeStageKind::Compression;
                continue;
            }
            throw std::invalid_argument("--range-stage must be window or compression");
        }

        if (arg == "--range-stage") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--range-stage requires value window|compression");
            }
            const std::string value = argv[++i];
            if (value == "window") {
                options.range_stage = RangeStageKind::Window;
                continue;
            }
            if (value == "compression") {
                options.range_stage = RangeStageKind::Compression;
                continue;
            }
            throw std::invalid_argument("--range-stage must be window or compression");
        }

        if (arg == "--native-backend") {
            options.native_backend = true;
            continue;
        }

        throw std::invalid_argument(
            "Unknown option. Use ci/local, --profile=ci/--profile=local, optional --trace-out=<path>, optional --evaluate-device-reduce, optional --range-stage=window|compression, and optional --native-backend");
    }

    return options;
}

std::filesystem::path WriteProfiledJsonConfig(const BenchmarkOptions& options) {
    const auto& profile = options.profile;
    const bool use_native_backend = options.native_backend;
    const int transfer_backend = use_native_backend ? 2 : 0;
    const int transform_backend = use_native_backend ? 2 : 1;
    const char* execution_backend = use_native_backend ? "metal" : "stub";
    const char* range_stage_type =
        (options.range_stage == RangeStageKind::Compression) ? "RangeCompressionNode" : "RangeWindowNode";

    nlohmann::json config = {
        {"name", "sar_stripmap_pr1_benchmark"},
        {"execution_backend", execution_backend},
        {"backend_fallback_policy", "allow_fallback"},
        {"resolver_diagnostics", true},
        {"edge_contract", "accel-token"},
        {"num_threads", 4},
        {"nodes", nlohmann::json::array({
            {
                {"id", "src"},
                {"type", "SyntheticApertureIqSourceNode"},
                {"node_config", {
                    {"stream_id", 0},
                    {"total_pulses", profile.pulses},
                    {"samples_per_pulse", profile.samples_per_pulse},
                    {"backend_id", 0},
                    {"backend", 0},
                }},
            },
            {
                {"id", "range_stage"},
                {"type", range_stage_type},
                {"node_config", {
                    {"enabled", true},
                    {"gain", 1.0f},
                }},
            },
            {
                {"id", "split"},
                {"type", "AzimuthTileSplitNode"},
                {"node_config", {
                    {"tile_count", profile.tile_count},
                    {"backend_id", 0},
                    {"backend", 0},
                }},
            },
            {
                {"id", "h2d"},
                {"type", "H2DAsyncNode"},
                {"node_config", {
                    {"override_backend", use_native_backend},
                    {"backend_id", 0},
                    {"backend", transfer_backend},
                }},
            },
            {
                {"id", "bp"},
                {"type", "SarBackprojectionTransformNode"},
                {"node_config", {
                    {"image_width", 16},
                    {"backend_id", 0},
                    {"queue_id", 0},
                    {"kernel_id", 3301},
                    {"backend", transform_backend},
                }},
            },
            {
                {"id", "d2h"},
                {"type", "D2HAsyncNode"},
                {"node_config", {
                    {"override_backend", use_native_backend},
                    {"backend_id", 0},
                    {"backend", transfer_backend},
                }},
            },
            {
                {"id", "merge"},
                {"type", "ImageTileMergeNode"},
                {"node_config", {
                    {"expected_tiles", profile.tile_count},
                    {"require_watermark_before_complete", false},
                    {"backend_id", 0},
                    {"backend", transform_backend},
                }},
            },
            {
                {"id", "sink"},
                {"type", "SarDiagnosticsSinkNode"},
                {"node_config", {
                    {"completion_signal_enabled", true},
                }},
            },
        })},
        {"edges", nlohmann::json::array({
            {{"source_node_id", "src"}, {"source_port", 0}, {"target_node_id", "range_stage"}, {"target_port", 0}},
            {{"source_node_id", "range_stage"}, {"source_port", 0}, {"target_node_id", "split"}, {"target_port", 0}},
            {{"source_node_id", "split"}, {"source_port", 0}, {"target_node_id", "h2d"}, {"target_port", 0}},
            {{"source_node_id", "h2d"}, {"source_port", 0}, {"target_node_id", "bp"}, {"target_port", 0}},
            {{"source_node_id", "bp"}, {"source_port", 0}, {"target_node_id", "d2h"}, {"target_port", 0}},
            {{"source_node_id", "d2h"}, {"source_port", 0}, {"target_node_id", "merge"}, {"target_port", 0}},
            {{"source_node_id", "merge"}, {"source_port", 0}, {"target_node_id", "sink"}, {"target_port", 0}},
        })},
    };

    const auto path = std::filesystem::temp_directory_path() /
                      ("sar_benchmark_" + profile.name + ".json");
    std::ofstream out(path);
    out << config.dump(2);
    return path;
}

BenchmarkStats ComputeStats(const std::vector<double>& samples_ms) {
    BenchmarkStats stats{};
    if (samples_ms.empty()) {
        return stats;
    }

    std::vector<double> sorted = samples_ms;
    std::sort(sorted.begin(), sorted.end());
    stats.min_ms = sorted.front();
    stats.max_ms = sorted.back();
    stats.median_ms = sorted[sorted.size() / 2];

    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) /
                        static_cast<double>(sorted.size());
    double variance = 0.0;
    for (const auto sample : sorted) {
        const double delta = sample - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(sorted.size());
    stats.stddev_ms = std::sqrt(variance);
    return stats;
}

GraphRunResult RunGraphOnce(const std::filesystem::path& config_path,
                            const std::filesystem::path& plugin_dir) {
    GraphRunResult out{};

    const auto build_start = Clock::now();
    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
                        .Build();
    const auto build_end = Clock::now();

    if (!executor) {
        throw std::runtime_error("Failed to build graph executor in benchmark");
    }

    out.build_ms = std::chrono::duration<double, std::milli>(build_end - build_start).count();

    const auto lifecycle_start = Clock::now();

    const auto init_start = Clock::now();
    const auto init = executor->Init();
    const auto init_end = Clock::now();
    if (!init.success) {
        throw std::runtime_error("Graph init failed in benchmark: " + init.message + " " + init.error_details);
    }
    out.init_ms = std::chrono::duration<double, std::milli>(init_end - init_start).count();

    const auto start_start = Clock::now();
    const auto start = executor->Start();
    const auto start_end = Clock::now();
    if (!start.success) {
        throw std::runtime_error("Graph start failed in benchmark: " + start.message + " " + start.error_details);
    }
    out.start_ms = std::chrono::duration<double, std::milli>(start_end - start_start).count();

    const auto run_start = Clock::now();
    const auto run = executor->Run();
    const auto run_end = Clock::now();

    if (!run.success) {
        throw std::runtime_error("Graph execution failed in benchmark: " + run.message + " " + run.error_details);
    }
    out.run_ms = std::chrono::duration<double, std::milli>(run_end - run_start).count();

    const auto stop_start = Clock::now();
    const auto stop = executor->Stop();
    const auto stop_end = Clock::now();
    if (!stop.success) {
        throw std::runtime_error("Graph stop failed in benchmark: " + stop.message + " " + stop.error_details);
    }
    out.stop_ms = std::chrono::duration<double, std::milli>(stop_end - stop_start).count();

    const auto join_start = Clock::now();
    const auto join = executor->Join();
    const auto join_end = Clock::now();
    if (!join.success) {
        throw std::runtime_error("Graph join failed in benchmark: " + join.message + " " + join.error_details);
    }

    out.join_ms = std::chrono::duration<double, std::milli>(join_end - join_start).count();
    out.lifecycle_total_ms =
        std::chrono::duration<double, std::milli>(join_end - lifecycle_start).count();

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    if (!sink) {
        throw std::runtime_error("Failed to resolve SarDiagnosticsSinkNode in benchmark");
    }

    const auto& metrics = executor->GetGraphManager()->GetMetrics();
    sink->UpdateFromGraphMetrics(metrics);
    out.diagnostics = sink->last_diagnostics();
    out.last_status = sink->last_status();
    out.queue_backpressure_events = metrics.backpressure_events.load(std::memory_order_relaxed);
    out.peak_queue_depth = metrics.peak_queue_depth.load(std::memory_order_relaxed);
    return out;
}

BaselineRunResult RunBaselineOnce(const BenchmarkOptions& options) {
    BaselineRunResult out{};
    const auto& profile = options.profile;

    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 0;
    source_cfg.total_pulses = profile.pulses;
    source_cfg.samples_per_pulse = profile.samples_per_pulse;
    source_cfg.backend_id = 0;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = profile.tile_count;
    split_cfg.backend_id = 0;
    split_cfg.backend = sar::SarBackendKind::Host;

    sar::SarBackprojectionTransformConfig bp_cfg{};
    bp_cfg.image_width = 16;
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 0;
    bp_cfg.kernel_id = 3301;
    bp_cfg.backend = options.native_backend
                         ? sar::SarBackendKind::NativeDevice
                         : sar::SarBackendKind::SimulatedDevice;

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = profile.tile_count;
    merge_cfg.require_watermark_before_complete = false;
    merge_cfg.backend_id = 0;
    merge_cfg.backend = sar::SarBackendKind::Host;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::RangeWindowNode window;
    sar::RangeCompressionNode compression;
    sar::AzimuthTileSplitNode split(split_cfg);
    sar::H2DAsyncNode h2d;
    sar::SarBackprojectionTransformNode bp(bp_cfg);
    sar::D2HAsyncNode d2h;
    sar::ImageTileMergeNode merge(merge_cfg);
    sar::SarDiagnosticsSinkNode sink;

    const auto start = Clock::now();
    while (true) {
        auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
        if (!pulse.has_value()) {
            break;
        }

        std::optional<sar::SarPulseBlockMessage> windowed_pulse;
        if (options.range_stage == RangeStageKind::Compression) {
            windowed_pulse = compression.Transfer(
                *pulse,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!windowed_pulse) {
                throw std::runtime_error("Baseline range compression failed");
            }
        } else {
            windowed_pulse = window.Transfer(
                *pulse,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!windowed_pulse) {
                throw std::runtime_error("Baseline range window failed");
            }
        }

        auto range_tile = split.Transfer(
            *windowed_pulse,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!range_tile) {
            throw std::runtime_error("Baseline split failed");
        }

        auto h2d_tile = h2d.Transfer(
            *range_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!h2d_tile) {
            throw std::runtime_error("Baseline h2d failed");
        }

        auto image_tile = bp.Transfer(
            *h2d_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!image_tile) {
            throw std::runtime_error("Baseline backprojection failed");
        }

        auto d2h_tile = d2h.Transfer(
            *image_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!d2h_tile) {
            throw std::runtime_error("Baseline d2h failed");
        }

        auto status = merge.Transfer(
            *d2h_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!status) {
            throw std::runtime_error("Baseline merge failed");
        }

        if (!sink.Consume(*status, std::integral_constant<std::size_t, 0>{})) {
            throw std::runtime_error("Baseline sink consume failed");
        }
    }
    const auto end = Clock::now();

    out.execute_ms = std::chrono::duration<double, std::milli>(end - start).count();
    out.diagnostics = sink.last_diagnostics();
    return out;
}

BaselineRunResult RunDeviceReducePrototypeBaselineOnce(const BenchmarkOptions& options) {
    BaselineRunResult out{};
    const auto& profile = options.profile;

    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 0;
    source_cfg.total_pulses = profile.pulses;
    source_cfg.samples_per_pulse = profile.samples_per_pulse;
    source_cfg.backend_id = 0;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = profile.tile_count;
    split_cfg.backend_id = 0;
    split_cfg.backend = sar::SarBackendKind::Host;

    sar::SarBackprojectionTransformConfig bp_cfg{};
    bp_cfg.image_width = 16;
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 0;
    bp_cfg.kernel_id = 3301;
    bp_cfg.backend = options.native_backend
                         ? sar::SarBackendKind::NativeDevice
                         : sar::SarBackendKind::SimulatedDevice;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::RangeWindowNode window;
    sar::RangeCompressionNode compression;
    sar::AzimuthTileSplitNode split(split_cfg);
    sar::H2DAsyncNode h2d;
    sar::SarBackprojectionTransformNode bp(bp_cfg);
    sar::D2HAsyncNode d2h;

    std::unordered_set<std::uint32_t> seen_tiles;
    std::uint32_t received_tiles = 0;
    std::uint32_t duplicate_tiles = 0;
    std::uint32_t out_of_order_tiles = 0;
    std::uint32_t last_tile_id = 0;
    bool has_last_tile = false;
    std::uint64_t bytes_h2d = 0;
    std::uint64_t bytes_d2h = 0;
    std::uint64_t kernel_dispatches = 0;
    std::uint64_t first_sequence = 0;
    bool has_first_sequence = false;
    std::uint64_t eos_sequence = 0;

    const auto start = Clock::now();
    while (true) {
        auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
        if (!pulse.has_value()) {
            break;
        }

        std::optional<sar::SarPulseBlockMessage> windowed_pulse;
        if (options.range_stage == RangeStageKind::Compression) {
            windowed_pulse = compression.Transfer(
                *pulse,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!windowed_pulse) {
                throw std::runtime_error("DeviceReduce prototype range compression failed");
            }
        } else {
            windowed_pulse = window.Transfer(
                *pulse,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!windowed_pulse) {
                throw std::runtime_error("DeviceReduce prototype range window failed");
            }
        }

        auto range_tile = split.Transfer(
            *windowed_pulse,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!range_tile) {
            throw std::runtime_error("DeviceReduce prototype split failed");
        }

        const auto range_token = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(range_tile->host_ptr));
        if (DecodeMarker(range_token) == sar::SarFrameMarker::EndOfStream) {
            eos_sequence = DecodeSequenceId(range_token);
            break;
        }

        auto h2d_tile = h2d.Transfer(
            *range_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!h2d_tile) {
            throw std::runtime_error("DeviceReduce prototype h2d failed");
        }

        auto image_tile = bp.Transfer(
            *h2d_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!image_tile) {
            throw std::runtime_error("DeviceReduce prototype backprojection failed");
        }

        auto d2h_tile = d2h.Transfer(
            *image_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!d2h_tile) {
            throw std::runtime_error("DeviceReduce prototype d2h failed");
        }

        const auto d2h_token = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(d2h_tile->host_ptr));
        const auto sequence_id = DecodeSequenceId(d2h_token);
        const auto tile_id = DecodeTileId(d2h_token);
        const auto byte_count = std::max<std::uint64_t>(
            d2h_tile->bytes,
            static_cast<std::uint64_t>(DecodeByteCount(d2h_token)));

        if (!has_first_sequence) {
            first_sequence = sequence_id;
            has_first_sequence = true;
        }

        bytes_h2d += byte_count;
        bytes_d2h += byte_count;
        ++kernel_dispatches;

        const auto [_, inserted] = seen_tiles.insert(tile_id);
        if (!inserted) {
            ++duplicate_tiles;
        } else {
            ++received_tiles;
            if (has_last_tile && tile_id < last_tile_id) {
                ++out_of_order_tiles;
            }
            last_tile_id = tile_id;
            has_last_tile = true;
        }
    }
    const auto end = Clock::now();

    const auto expected_tiles = profile.tile_count;
    const auto missing_tiles =
        (received_tiles >= expected_tiles) ? 0u : (expected_tiles - received_tiles);

    out.execute_ms = std::chrono::duration<double, std::milli>(end - start).count();
    out.diagnostics.envelope.sequence_id = eos_sequence;
    out.diagnostics.envelope.stream_id = 0;
    out.diagnostics.envelope.marker = sar::SarFrameMarker::EndOfStream;
    out.diagnostics.pulses_processed = eos_sequence;
    out.diagnostics.tiles_processed = received_tiles;
    out.diagnostics.bytes_h2d = bytes_h2d;
    out.diagnostics.bytes_d2h = bytes_d2h;
    out.diagnostics.kernel_dispatches = kernel_dispatches;
    out.diagnostics.fanin_wait_ms = has_first_sequence ? (eos_sequence - first_sequence) : 0;
    out.diagnostics.e2e_latency_ms = out.diagnostics.fanin_wait_ms;
    out.diagnostics.duplicate_tile_count = duplicate_tiles;
    out.diagnostics.missing_tile_count = missing_tiles;
    out.diagnostics.out_of_order_completion_count = out_of_order_tiles;
    return out;
}

bool DiagnosticsEquivalent(const sar::SarDiagnosticsMessage& lhs,
                           const sar::SarDiagnosticsMessage& rhs) {
    return lhs.pulses_processed == rhs.pulses_processed &&
           lhs.tiles_processed == rhs.tiles_processed &&
           lhs.bytes_h2d == rhs.bytes_h2d &&
           lhs.bytes_d2h == rhs.bytes_d2h &&
           lhs.kernel_dispatches == rhs.kernel_dispatches &&
           lhs.fanin_wait_ms == rhs.fanin_wait_ms &&
           lhs.e2e_latency_ms == rhs.e2e_latency_ms &&
           lhs.duplicate_tile_count == rhs.duplicate_tile_count &&
           lhs.missing_tile_count == rhs.missing_tile_count &&
           lhs.out_of_order_completion_count == rhs.out_of_order_completion_count;
}

void VerifyDeterministicParity(const GraphRunResult& graph,
                               const BaselineRunResult& baseline) {
    if (graph.diagnostics.pulses_processed != baseline.diagnostics.pulses_processed ||
        graph.diagnostics.tiles_processed != baseline.diagnostics.tiles_processed ||
        graph.diagnostics.bytes_h2d != baseline.diagnostics.bytes_h2d ||
        graph.diagnostics.bytes_d2h != baseline.diagnostics.bytes_d2h ||
        graph.diagnostics.kernel_dispatches != baseline.diagnostics.kernel_dispatches ||
        graph.diagnostics.duplicate_tile_count != baseline.diagnostics.duplicate_tile_count ||
        graph.diagnostics.missing_tile_count != baseline.diagnostics.missing_tile_count ||
        graph.diagnostics.out_of_order_completion_count !=
            baseline.diagnostics.out_of_order_completion_count) {
        throw std::runtime_error("Graph and baseline deterministic diagnostics parity failed");
    }
}

void PrintSummary(const BenchmarkProfile& profile,
                  const BenchmarkStats& graph_build,
                  const BenchmarkStats& graph_run,
                  const BenchmarkStats& graph_lifecycle,
                  const BenchmarkStats& baseline_exec,
                  const GraphRunResult& last_graph,
                  const DeviceReduceEvaluation& device_reduce_eval,
                  const BenchmarkOptions& options) {
    const double scheduling_overhead_ms =
        std::max(0.0, graph_run.median_ms - baseline_exec.median_ms);

    std::cout << "SAR benchmark profile: " << profile.name << "\n";
    std::cout << "Dataset: pulses=" << profile.pulses
              << ", samples_per_pulse=" << profile.samples_per_pulse
              << ", tile_count=" << profile.tile_count << "\n";
    std::cout << "Runs: warmup=" << profile.warmup_runs
              << ", measured=" << profile.measured_runs << "\n\n";
    std::cout << "Range stage: "
              << (options.range_stage == RangeStageKind::Compression ? "compression" : "window")
              << " | Native backend mode: "
              << (options.native_backend ? "enabled" : "disabled") << "\n\n";

    std::cout << "Graph build time (ms): min=" << graph_build.min_ms
              << ", median=" << graph_build.median_ms
              << ", max=" << graph_build.max_ms
              << ", stddev=" << graph_build.stddev_ms << "\n";

    std::cout << "Graph run time (ms): min=" << graph_run.min_ms
              << ", median=" << graph_run.median_ms
              << ", max=" << graph_run.max_ms
              << ", stddev=" << graph_run.stddev_ms << "\n";

    std::cout << "Graph lifecycle total time (ms): min=" << graph_lifecycle.min_ms
              << ", median=" << graph_lifecycle.median_ms
              << ", max=" << graph_lifecycle.max_ms
              << ", stddev=" << graph_lifecycle.stddev_ms << "\n";

    std::cout << "Baseline execute time (ms): min=" << baseline_exec.min_ms
              << ", median=" << baseline_exec.median_ms
              << ", max=" << baseline_exec.max_ms
              << ", stddev=" << baseline_exec.stddev_ms << "\n\n";

    std::cout << "Overhead attribution (deterministic proxies):\n";
    std::cout << "- graph scheduling/run loop: " << scheduling_overhead_ms
              << " ms (median graph run - median baseline)\n";
    std::cout << "- graph lifecycle teardown: init=" << last_graph.init_ms
              << " ms, start=" << last_graph.start_ms
              << " ms, stop=" << last_graph.stop_ms
              << " ms, join=" << last_graph.join_ms
              << " ms, total=" << last_graph.lifecycle_total_ms << " ms\n";
    std::cout << "- message allocation/copy: bytes_h2d=" << last_graph.diagnostics.bytes_h2d
              << ", bytes_d2h=" << last_graph.diagnostics.bytes_d2h << "\n";
    std::cout << "- transfer/kernel timing (us): h2d=" << last_graph.diagnostics.transfer_h2d_time_us
              << ", kernel=" << last_graph.diagnostics.kernel_exec_time_us
              << ", d2h=" << last_graph.diagnostics.transfer_d2h_time_us << "\n";
    std::cout << "- queue wait/backpressure: fanin_wait_ms=" << last_graph.diagnostics.fanin_wait_ms
              << ", backpressure_events=" << last_graph.queue_backpressure_events
              << ", peak_queue_depth=" << last_graph.peak_queue_depth
              << ", out_of_order_completions="
              << last_graph.diagnostics.out_of_order_completion_count << "\n";
    std::cout << "- provider/plugin lookup: represented by graph build timing above\n";
    std::cout << "- diagnostics collection: deterministic contract emission at sink (consume_count > 0)\n";
    std::cout << "- backend synchronization: proxy e2e_latency_ms=" << last_graph.diagnostics.e2e_latency_ms << "\n";

    if (device_reduce_eval.enabled) {
        std::cout << "\nDeviceReduce prototype evaluation:\n";
        std::cout << "- prototype baseline execute median (ms): "
                  << device_reduce_eval.prototype_exec_stats.median_ms << "\n";
        std::cout << "- diagnostics parity with baseline: "
                  << (device_reduce_eval.diagnostics_match ? "true" : "false") << "\n";
        std::cout << "- decision: " << device_reduce_eval.decision << "\n";
        std::cout << "- rationale: " << device_reduce_eval.rationale << "\n";
    }
}

nlohmann::json StatsToJson(const BenchmarkStats& stats) {
    return {
        {"min_ms", stats.min_ms},
        {"median_ms", stats.median_ms},
        {"max_ms", stats.max_ms},
        {"stddev_ms", stats.stddev_ms},
    };
}

std::string BackendKindToString(graph::gpu::accel::BackendKind backend) {
    switch (backend) {
        case graph::gpu::accel::BackendKind::CUDA:
            return "cuda";
        case graph::gpu::accel::BackendKind::SYCL:
            return "sycl";
        case graph::gpu::accel::BackendKind::Metal:
            return "metal";
        case graph::gpu::accel::BackendKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::uint64_t PointerToken(const void* ptr) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ptr));
}

nlohmann::json GpuMetadataToJson(const sar::SarGpuMetadata& gpu) {
    return {
        {"has_lease", gpu.has_lease},
        {"has_host_view", gpu.has_host_view},
        {"has_device_view", gpu.has_device_view},
        {"has_transfer_ticket", gpu.has_transfer_ticket},
        {"has_kernel_ticket", gpu.has_kernel_ticket},
        {"lease", {
            {"pool_id", gpu.lease.pool_id},
            {"allocation_id", gpu.lease.allocation_id},
            {"release_policy", static_cast<int>(gpu.lease.release_policy)},
        }},
        {"host_view", {
            {"backend", BackendKindToString(gpu.host_view.backend)},
            {"host_ptr_token", PointerToken(gpu.host_view.host_ptr)},
            {"bytes", gpu.host_view.bytes},
            {"allocator_id", gpu.host_view.allocator_id},
        }},
        {"device_view", {
            {"backend", BackendKindToString(gpu.device_view.backend)},
            {"device_ptr_token", PointerToken(gpu.device_view.device_ptr)},
            {"bytes", gpu.device_view.bytes},
            {"device_id", gpu.device_view.device_id},
            {"queue_id", gpu.device_view.execution_queue_id},
            {"ready_event", gpu.device_view.ready_event},
        }},
        {"transfer_ticket", {
            {"backend", BackendKindToString(gpu.transfer_ticket.backend)},
            {"transfer_id", gpu.transfer_ticket.transfer_id},
            {"queue_id", gpu.transfer_ticket.execution_queue_id},
            {"completion_event", gpu.transfer_ticket.completion_event},
        }},
        {"kernel_ticket", {
            {"backend", BackendKindToString(gpu.kernel_ticket.backend)},
            {"kernel_id", gpu.kernel_ticket.kernel_id},
            {"queue_id", gpu.kernel_ticket.execution_queue_id},
            {"completion_event", gpu.kernel_ticket.completion_event},
            {"arg_count", gpu.kernel_ticket.arg_count},
        }},
    };
}

void WriteTraceJson(const std::filesystem::path& path,
                    const BenchmarkOptions& options,
                    const BenchmarkStats& graph_build,
                    const BenchmarkStats& graph_run,
                    const BenchmarkStats& graph_lifecycle,
                    const BenchmarkStats& baseline_exec,
                    const GraphRunResult& last_graph,
                    const DeviceReduceEvaluation& device_reduce_eval) {
    const auto& profile = options.profile;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    nlohmann::json trace = {
        {"schema", "graphx.sar.benchmark.trace.v1"},
        {"profile", {
            {"name", profile.name},
            {"pulses", profile.pulses},
            {"samples_per_pulse", profile.samples_per_pulse},
            {"tile_count", profile.tile_count},
            {"warmup_runs", profile.warmup_runs},
            {"measured_runs", profile.measured_runs},
            {"range_stage", options.range_stage == RangeStageKind::Compression ? "compression" : "window"},
            {"native_backend", options.native_backend},
            {"execution_backend", options.native_backend ? "metal" : "stub"},
            {"backend_fallback_policy", "allow_fallback"},
            {"edge_contract", "accel-token"},
        }},
        {"timing_ms", {
            {"graph_build", StatsToJson(graph_build)},
            {"graph_run", StatsToJson(graph_run)},
            {"graph_lifecycle_total", StatsToJson(graph_lifecycle)},
            {"baseline_execute", StatsToJson(baseline_exec)},
        }},
        {"last_lifecycle_ms", {
            {"init", last_graph.init_ms},
            {"start", last_graph.start_ms},
            {"stop", last_graph.stop_ms},
            {"join", last_graph.join_ms},
            {"total", last_graph.lifecycle_total_ms},
        }},
        {"diagnostics", {
            {"pulses_processed", last_graph.diagnostics.pulses_processed},
            {"tiles_processed", last_graph.diagnostics.tiles_processed},
            {"bytes_h2d", last_graph.diagnostics.bytes_h2d},
            {"bytes_d2h", last_graph.diagnostics.bytes_d2h},
            {"kernel_dispatches", last_graph.diagnostics.kernel_dispatches},
            {"transfer_h2d_time_us", last_graph.diagnostics.transfer_h2d_time_us},
            {"kernel_exec_time_us", last_graph.diagnostics.kernel_exec_time_us},
            {"transfer_d2h_time_us", last_graph.diagnostics.transfer_d2h_time_us},
            {"fanin_wait_ms", last_graph.diagnostics.fanin_wait_ms},
            {"e2e_latency_ms", last_graph.diagnostics.e2e_latency_ms},
            {"duplicate_tile_count", last_graph.diagnostics.duplicate_tile_count},
            {"missing_tile_count", last_graph.diagnostics.missing_tile_count},
            {"out_of_order_completion_count", last_graph.diagnostics.out_of_order_completion_count},
        }},
        {"queue", {
            {"backpressure_events", last_graph.queue_backpressure_events},
            {"peak_queue_depth", last_graph.peak_queue_depth},
        }},
        {"token_lifecycle", GpuMetadataToJson(last_graph.last_status.gpu)},
        {"resolved_nodes", nlohmann::json::array({
            {
                {"intent_type", "H2DAsyncNode"},
                {"concrete_type", "H2DAsyncNode"},
                {"selected_backend", options.native_backend ? "metal" : "stub"},
                {"fallback_reason", options.native_backend ? "resolver-not-yet-bound-native-capability" : "ci-stub-profile"},
                {"input_token_type", "HostPinnedBufferView"},
                {"output_token_type", "DeviceBufferView"},
            },
            {
                {"intent_type", "SarBackprojectionTransformNode"},
                {"concrete_type", "SarBackprojectionTransformNode"},
                {"selected_backend", options.native_backend ? "metal" : "stub"},
                {"fallback_reason", options.native_backend ? "resolver-not-yet-bound-native-capability" : "ci-stub-profile"},
                {"input_token_type", "DeviceBufferView"},
                {"output_token_type", "DeviceBufferView"},
            },
            {
                {"intent_type", "D2HAsyncNode"},
                {"concrete_type", "D2HAsyncNode"},
                {"selected_backend", options.native_backend ? "metal" : "stub"},
                {"fallback_reason", options.native_backend ? "resolver-not-yet-bound-native-capability" : "ci-stub-profile"},
                {"input_token_type", "DeviceBufferView"},
                {"output_token_type", "HostPinnedBufferView"},
            },
        })},
        {"overhead_ms", {
            {"graph_run_minus_baseline_median", std::max(0.0, graph_run.median_ms - baseline_exec.median_ms)},
            {"lifecycle_join_last", last_graph.join_ms},
        }},
    };

    if (device_reduce_eval.enabled) {
        trace["device_reduce_evaluation"] = {
            {"diagnostics_match", device_reduce_eval.diagnostics_match},
            {"prototype_baseline_execute_ms", StatsToJson(device_reduce_eval.prototype_exec_stats)},
            {"decision", device_reduce_eval.decision},
            {"rationale", device_reduce_eval.rationale},
        };
    }

    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open trace output path: " + path.string());
    }
    out << trace.dump(2) << "\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = ParseOptions(argc, argv);
        const auto& profile = options.profile;

        const std::filesystem::path plugin_dir = SAR_PLUGIN_OUTPUT_DIRECTORY;
        if (!std::filesystem::exists(plugin_dir)) {
            std::cerr << "Plugin directory not found: " << plugin_dir << "\n";
            return 1;
        }

        const auto config_path = WriteProfiledJsonConfig(options);

        for (int i = 0; i < profile.warmup_runs; ++i) {
            (void)RunGraphOnce(config_path, plugin_dir);
            (void)RunBaselineOnce(options);
        }

        std::vector<double> graph_build_samples_ms;
        std::vector<double> graph_run_samples_ms;
        std::vector<double> graph_lifecycle_samples_ms;
        std::vector<double> baseline_exec_samples_ms;
        graph_build_samples_ms.reserve(profile.measured_runs);
        graph_run_samples_ms.reserve(profile.measured_runs);
        graph_lifecycle_samples_ms.reserve(profile.measured_runs);
        baseline_exec_samples_ms.reserve(profile.measured_runs);

        GraphRunResult last_graph{};
        BaselineRunResult last_baseline{};
        DeviceReduceEvaluation device_reduce_eval{};

        for (int i = 0; i < profile.measured_runs; ++i) {
            last_graph = RunGraphOnce(config_path, plugin_dir);
            last_baseline = RunBaselineOnce(options);
            VerifyDeterministicParity(last_graph, last_baseline);

            graph_build_samples_ms.push_back(last_graph.build_ms);
            graph_run_samples_ms.push_back(last_graph.run_ms);
            graph_lifecycle_samples_ms.push_back(last_graph.lifecycle_total_ms);
            baseline_exec_samples_ms.push_back(last_baseline.execute_ms);
        }

        const auto graph_build_stats = ComputeStats(graph_build_samples_ms);
        const auto graph_run_stats = ComputeStats(graph_run_samples_ms);
        const auto graph_lifecycle_stats = ComputeStats(graph_lifecycle_samples_ms);
        const auto baseline_exec_stats = ComputeStats(baseline_exec_samples_ms);

        if (options.evaluate_device_reduce) {
            device_reduce_eval.enabled = true;

            std::vector<double> device_reduce_samples_ms;
            device_reduce_samples_ms.reserve(profile.measured_runs);
            for (int i = 0; i < profile.measured_runs; ++i) {
                device_reduce_eval.last_prototype = RunDeviceReducePrototypeBaselineOnce(options);
                device_reduce_samples_ms.push_back(device_reduce_eval.last_prototype.execute_ms);
            }
            device_reduce_eval.prototype_exec_stats = ComputeStats(device_reduce_samples_ms);
            device_reduce_eval.diagnostics_match =
                DiagnosticsEquivalent(device_reduce_eval.last_prototype.diagnostics, last_baseline.diagnostics);

            const double speedup_ratio =
                (baseline_exec_stats.median_ms <= 0.0)
                    ? 1.0
                    : (baseline_exec_stats.median_ms /
                       std::max(0.0001, device_reduce_eval.prototype_exec_stats.median_ms));

            if (device_reduce_eval.diagnostics_match && speedup_ratio > 1.15) {
                device_reduce_eval.decision = "keep-for-pr2";
                device_reduce_eval.rationale =
                    "Prototype path preserved diagnostics and exceeded 15% median baseline speedup.";
            } else {
                device_reduce_eval.decision = "defer-to-pr3";
                device_reduce_eval.rationale =
                    "Prototype did not show sufficient deterministic gain and generic DeviceReduceNode is not yet available in GraphX runtime.";
            }
        }

        PrintSummary(
            profile,
            graph_build_stats,
            graph_run_stats,
            graph_lifecycle_stats,
            baseline_exec_stats,
            last_graph,
            device_reduce_eval,
            options);

        if (options.trace_output_path) {
            WriteTraceJson(
                *options.trace_output_path,
                options,
                graph_build_stats,
                graph_run_stats,
                graph_lifecycle_stats,
                baseline_exec_stats,
                last_graph,
                device_reduce_eval);
            std::cout << "\nTrace written: " << options.trace_output_path->string() << "\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark failed: " << ex.what() << "\n";
        return 1;
    }
}
