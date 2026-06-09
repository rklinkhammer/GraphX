#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncNode.hpp"
#include "sar/H2DAsyncNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/RangeCompressionNode.hpp"
#include "sar/RangeWindowNode.hpp"
#include "sar/SarBackprojectionTransformNode.hpp"
#include "sar/SarCpuReference.hpp"
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
#include <string_view>
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
    std::string resolved_execution_backend{"unknown"};
    std::string backprojection_concrete_type{"unknown"};
    bool backprojection_native_kernel_bound{false};
    bool backprojection_native_kernel_executed{false};
    graph::gpu::accel::KernelTicket backprojection_last_kernel_ticket{};
    std::uint64_t queue_backpressure_events{0};
    std::uint64_t peak_queue_depth{0};
    bool completion_signaled{false};
    bool run_timeout_proxy{false};
    std::string run_exit_mode{"unknown"};
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

struct Pr5ReferenceMetrics {
    double range_compression_reference_ms{0.0};
    double range_compression_runtime_ms{0.0};
    double image_metric_ms{0.0};
    std::uint32_t matched_filter_vector_length{0};
    std::string runtime_compression_mode{"matched_filter"};
    std::uint32_t matched_filter_peak_bin{0};
    float matched_filter_peak_value{0.0f};
    double runtime_reference_l_inf{0.0};
    double runtime_reference_rms{0.0};
    double runtime_reference_relative_l2{0.0};
    double peak_location_error_pixels{0.0};
    double impulse_response_width_pixels{0.0};
    double peak_sidelobe_ratio_db{0.0};
    double integrated_sidelobe_ratio_db{0.0};
    double dynamic_range_db{0.0};
    std::uint64_t image_hash{0};
    double graph_direct_peak_delta_pixels{0.0};
    double graph_direct_peak_value_delta{0.0};
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

std::string BackendKindToString(graph::gpu::accel::BackendKind backend);

std::string_view TypeToken(std::string_view type_name) {
    const auto stripped = StripNamespace(type_name);
    const auto template_start = stripped.find('<');
    if (template_start == std::string_view::npos) {
        return stripped;
    }
    return stripped.substr(0, template_start);
}

struct BackprojectionResolution {
    std::shared_ptr<sar::SarBackprojectionTransformNode> node;
    std::string concrete_type{"unknown"};
};

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

BackprojectionResolution ResolveBackprojectionNode(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    BackprojectionResolution resolved{};
    if (!graph_manager) {
        return resolved;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }

        const auto runtime_type = wrapper->GetType();
        const auto runtime_token = TypeToken(runtime_type);
        if (runtime_token != "SarBackprojectionTransformNode" &&
            runtime_token != "SarBackprojectionTransformAccelNode") {
            continue;
        }

        auto typed_node = wrapper->GetNode<sar::SarBackprojectionTransformNode>();
        if (!typed_node) {
            continue;
        }

        resolved.node = std::move(typed_node);
        resolved.concrete_type = runtime_type.empty() ? std::string(runtime_token) : runtime_type;
        return resolved;
    }

    return resolved;
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
    const char* execution_backend = use_native_backend ? "metal" : "stub";
    const bool use_range_compression = options.range_stage == RangeStageKind::Compression;
    const char* range_stage_type = use_range_compression ? "RangeCompressionNode" : "RangeWindowNode";

    nlohmann::json range_stage_config = {
        {"enabled", true},
        {"gain", 1.0f},
    };
    if (use_range_compression) {
        range_stage_config["sample_rate_hz"] = 16000000.0f;
        range_stage_config["mode"] = "matched_filter";
        range_stage_config["output"] = "magnitude";
        range_stage_config["bandwidth_hz"] = 4000000.0f;
        range_stage_config["chirp_duration_s"] = 0.000001f;
        range_stage_config["range_origin_m"] = 0.0f;
        range_stage_config["range_spacing_m"] = 0.25f;
    }

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
                {"node_config", range_stage_config},
            },
            {
                {"id", "split"},
                {"type", "AzimuthTileSplitNode"},
                {"node_config", {
                    {"tile_count", profile.tile_count},
                    {"tile_id_offset", 0},
                    {"backend_id", 0},
                }},
            },
            {
                {"id", "h2d"},
                {"type", "H2DAsyncNode"},
                {"node_config", {
                    {"override_backend", false},
                    {"backend_id", 0},
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
                }},
            },
            {
                {"id", "d2h"},
                {"type", "D2HAsyncNode"},
                {"node_config", {
                    {"override_backend", false},
                    {"backend_id", 0},
                }},
            },
            {
                {"id", "merge"},
                {"type", "ImageTileMergeNode"},
                {"node_config", {
                    {"expected_tiles", profile.tile_count},
                    {"require_watermark_before_complete", false},
                    {"backend_id", 0},
                    {"backend", 1},
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

Pr5ReferenceMetrics MeasurePr5ReferenceMetrics() {
    Pr5ReferenceMetrics out{};

    sar::reference::ChirpReferenceConfig cfg{};
    cfg.sample_count = 16;
    cfg.sample_rate_hz = 16.0e6;
    cfg.bandwidth_hz = 4.0e6;
    cfg.chirp_duration_s = 1.0e-6;
    cfg.range_origin_m = 0.0;
    cfg.range_spacing_m = 0.25;

    const auto chirp = sar::reference::GenerateLinearFmChirp(cfg);
    const auto echo = sar::reference::GenerateDelayedEcho(chirp, 3u, 0.75);

    const auto reference_start = Clock::now();
    const auto compressed = sar::reference::MatchedFilterRangeCompress(echo, chirp);
    const auto reference_end = Clock::now();

    const auto metrics_start = Clock::now();
    const auto image = sar::reference::MagnitudeImage(16u, 1u, compressed);
    const auto metrics = sar::reference::MeasureImageQuality(image, 3u, 0u);
    const auto metrics_end = Clock::now();

    out.range_compression_reference_ms =
        std::chrono::duration<double, std::milli>(reference_end - reference_start).count();
    out.image_metric_ms =
        std::chrono::duration<double, std::milli>(metrics_end - metrics_start).count();
    out.matched_filter_vector_length = cfg.sample_count;
    out.runtime_compression_mode = "matched_filter";
    out.matched_filter_peak_bin = metrics.peak.x;
    out.matched_filter_peak_value = metrics.peak.value;
    out.peak_location_error_pixels = metrics.peak_location_error_pixels;
    out.impulse_response_width_pixels = metrics.impulse_response_width_pixels;
    out.peak_sidelobe_ratio_db = metrics.peak_sidelobe_ratio_db;
    out.integrated_sidelobe_ratio_db = metrics.integrated_sidelobe_ratio_db;
    out.dynamic_range_db = metrics.dynamic_range_db;
    out.image_hash = metrics.image_hash;

    sar::SarPulseBlockMessage runtime_input{};
    runtime_input.envelope.sequence_id = 0;
    runtime_input.envelope.marker = sar::SarFrameMarker::Data;
    runtime_input.buffer.byte_count = echo.size() * sizeof(sar::SarIqSample);
    runtime_input.iq_samples.reserve(echo.size());
    for (const auto& sample : echo) {
        runtime_input.iq_samples.emplace_back(
            static_cast<float>(sample.real()),
            static_cast<float>(sample.imag()));
    }

    sar::RangeCompressionConfig runtime_config{};
    runtime_config.mode = sar::RangeCompressionMode::MatchedFilter;
    runtime_config.output = sar::RangeCompressionOutput::Magnitude;
    runtime_config.gain = 1.0f;
    runtime_config.sample_rate_hz = cfg.sample_rate_hz;
    runtime_config.bandwidth_hz = cfg.bandwidth_hz;
    runtime_config.chirp_duration_s = cfg.chirp_duration_s;
    runtime_config.range_origin_m = cfg.range_origin_m;
    runtime_config.range_spacing_m = cfg.range_spacing_m;
    sar::RangeCompressionNode runtime_node(runtime_config);

    const auto runtime_start = Clock::now();
    const auto runtime_output = runtime_node.Transfer(
        runtime_input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    const auto runtime_end = Clock::now();
    out.range_compression_runtime_ms =
        std::chrono::duration<double, std::milli>(runtime_end - runtime_start).count();

    if (runtime_output) {
        sar::reference::Image runtime_image{};
        runtime_image.width = 16u;
        runtime_image.height = 1u;
        runtime_image.pixels.reserve(runtime_output->iq_samples.size());
        for (const auto& sample : runtime_output->iq_samples) {
            runtime_image.pixels.push_back(sample.real());
        }
        const auto runtime_error = sar::reference::CompareImages(runtime_image, image);
        out.runtime_reference_l_inf = runtime_error.l_inf;
        out.runtime_reference_rms = runtime_error.rms;
        out.runtime_reference_relative_l2 = runtime_error.relative_l2;
    }

    // The current graph path reports diagnostics rather than image samples; keep the
    // parity fields explicit and zero for the deterministic shared reference fixture.
    out.graph_direct_peak_delta_pixels = 0.0;
    out.graph_direct_peak_value_delta = 0.0;
    return out;
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

    auto backprojection = ResolveBackprojectionNode(executor->GetGraphManager());

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
    out.completion_signaled = executor->IsCompletionSignaled();
    out.run_timeout_proxy = !out.completion_signaled && out.run_ms >= 14900.0;
    out.run_exit_mode = out.completion_signaled
                            ? "completion_signaled"
                            : (out.run_timeout_proxy
                                   ? "timeout_proxy"
                                   : "stopped_without_completion");

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    if (!sink) {
        throw std::runtime_error("Failed to resolve SarDiagnosticsSinkNode in benchmark");
    }

    if (!backprojection.node) {
        backprojection = ResolveBackprojectionNode(executor->GetGraphManager());
    }
    if (backprojection.node) {
        out.backprojection_concrete_type = backprojection.concrete_type;
        out.backprojection_native_kernel_bound = backprojection.node->native_kernel_bound();
        out.backprojection_last_kernel_ticket = backprojection.node->last_kernel_ticket();
    }

    const auto& metrics = executor->GetGraphManager()->GetMetrics();
    sink->UpdateFromGraphMetrics(metrics);
    out.diagnostics = sink->last_diagnostics();
    out.last_status = sink->last_status();
    if (out.backprojection_last_kernel_ticket.backend != graph::gpu::accel::BackendKind::Unknown) {
        out.resolved_execution_backend =
            BackendKindToString(out.backprojection_last_kernel_ticket.backend);
    } else {
        out.resolved_execution_backend =
            BackendKindToString(out.last_status.gpu.kernel_ticket.backend);
    }
    out.backprojection_native_kernel_executed =
        out.backprojection_native_kernel_bound &&
        out.backprojection_last_kernel_ticket.backend == graph::gpu::accel::BackendKind::Metal &&
        out.backprojection_last_kernel_ticket.kernel_id > 0u &&
        out.backprojection_last_kernel_ticket.execution_queue_id > 0u &&
        out.backprojection_last_kernel_ticket.arg_count >= 1u;
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

void VerifyNativeBackendCoreParity(const GraphRunResult& graph,
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
        throw std::runtime_error(
            "Graph and baseline native-core diagnostics parity failed: "
            "pulses=" + std::to_string(graph.diagnostics.pulses_processed) + "/" +
            std::to_string(baseline.diagnostics.pulses_processed) +
            ", tiles=" + std::to_string(graph.diagnostics.tiles_processed) + "/" +
            std::to_string(baseline.diagnostics.tiles_processed) +
            ", bytes_h2d=" + std::to_string(graph.diagnostics.bytes_h2d) + "/" +
            std::to_string(baseline.diagnostics.bytes_h2d) +
            ", bytes_d2h=" + std::to_string(graph.diagnostics.bytes_d2h) + "/" +
            std::to_string(baseline.diagnostics.bytes_d2h) +
            ", dispatches=" + std::to_string(graph.diagnostics.kernel_dispatches) + "/" +
            std::to_string(baseline.diagnostics.kernel_dispatches) +
            ", dup=" + std::to_string(graph.diagnostics.duplicate_tile_count) + "/" +
            std::to_string(baseline.diagnostics.duplicate_tile_count) +
            ", missing=" + std::to_string(graph.diagnostics.missing_tile_count) + "/" +
            std::to_string(baseline.diagnostics.missing_tile_count) +
            ", ooo=" + std::to_string(graph.diagnostics.out_of_order_completion_count) + "/" +
            std::to_string(baseline.diagnostics.out_of_order_completion_count));
    }
}

void PrintSummary(const BenchmarkProfile& profile,
                  const BenchmarkStats& graph_build,
                  const BenchmarkStats& graph_run,
                  const BenchmarkStats& graph_lifecycle,
                  const BenchmarkStats& baseline_exec,
                  const GraphRunResult& last_graph,
                  const DeviceReduceEvaluation& device_reduce_eval,
                  const Pr5ReferenceMetrics& pr5_reference,
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
    std::cout << "- accel-token movement: bytes_h2d=" << last_graph.diagnostics.bytes_h2d
              << ", bytes_d2h=" << last_graph.diagnostics.bytes_d2h
              << " (transfer payload counters; graph edges carry tokens/sidecars)\n";
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
    std::cout << "- PR4 cost buckets: algorithm_baseline_ms=" << baseline_exec.median_ms
              << ", dsp_range_stage="
              << (options.range_stage == RangeStageKind::Compression ? "compression" : "window")
              << ", transfer_payload_bytes="
              << (last_graph.diagnostics.bytes_h2d + last_graph.diagnostics.bytes_d2h)
              << ", kernel_dispatches=" << last_graph.diagnostics.kernel_dispatches
              << ", graph_overhead_ms=" << scheduling_overhead_ms
              << ", diagnostics_contract=sink-status\n";
    std::cout << "- PR5 accuracy/fidelity: matched_filter_peak_bin="
              << pr5_reference.matched_filter_peak_bin
              << ", peak_error_px=" << pr5_reference.peak_location_error_pixels
              << ", pslr_db=" << pr5_reference.peak_sidelobe_ratio_db
              << ", islr_db=" << pr5_reference.integrated_sidelobe_ratio_db
              << ", dynamic_range_db=" << pr5_reference.dynamic_range_db
              << ", image_hash=" << pr5_reference.image_hash << "\n";
    std::cout << "- PR6 runtime matched filter: mode="
              << pr5_reference.runtime_compression_mode
              << ", runtime_ms=" << pr5_reference.range_compression_runtime_ms
              << ", reference_ms=" << pr5_reference.range_compression_reference_ms
              << ", l_inf=" << pr5_reference.runtime_reference_l_inf
              << ", rms=" << pr5_reference.runtime_reference_rms
              << ", relative_l2=" << pr5_reference.runtime_reference_relative_l2 << "\n";

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
                    const DeviceReduceEvaluation& device_reduce_eval,
                    const Pr5ReferenceMetrics& pr5_reference) {
    const auto& profile = options.profile;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    const double graph_overhead_ms =
        std::max(0.0, graph_run.median_ms - baseline_exec.median_ms);
    const bool attribution_evidence_present =
        graph_run.median_ms >= 0.0 &&
        baseline_exec.median_ms >= 0.0 &&
        last_graph.diagnostics.bytes_h2d > 0u &&
        last_graph.diagnostics.bytes_d2h > 0u &&
        last_graph.diagnostics.kernel_dispatches > 0u;

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
                {"selected_backend", last_graph.resolved_execution_backend},
                {"fallback_reason", options.native_backend ? "none" : "ci-stub-profile"},
                {"input_token_type", "HostPinnedBufferView"},
                {"output_token_type", "DeviceBufferView"},
            },
            {
                {"intent_type", "SarBackprojectionTransformNode"},
                {"concrete_type", last_graph.backprojection_concrete_type},
                {"selected_backend", last_graph.resolved_execution_backend},
                {"fallback_reason", options.native_backend ? "none" : "ci-stub-profile"},
                {"input_token_type", "DeviceBufferView"},
                {"output_token_type", "DeviceBufferView"},
            },
            {
                {"intent_type", "D2HAsyncNode"},
                {"concrete_type", "D2HAsyncNode"},
                {"selected_backend", last_graph.resolved_execution_backend},
                {"fallback_reason", options.native_backend ? "none" : "ci-stub-profile"},
                {"input_token_type", "DeviceBufferView"},
                {"output_token_type", "HostPinnedBufferView"},
            },
        })},
        {"native_execution_evidence", {
            {"requested_native_backend", options.native_backend},
            {"resolved_execution_backend", last_graph.resolved_execution_backend},
            {"backprojection_concrete_type", last_graph.backprojection_concrete_type},
            {"backprojection_native_kernel_bound", last_graph.backprojection_native_kernel_bound},
            {"backprojection_native_kernel_executed", last_graph.backprojection_native_kernel_executed},
            {"kernel_ticket_backend", BackendKindToString(last_graph.backprojection_last_kernel_ticket.backend)},
            {"kernel_ticket_id", last_graph.backprojection_last_kernel_ticket.kernel_id},
            {"kernel_ticket_queue_id", last_graph.backprojection_last_kernel_ticket.execution_queue_id},
            {"kernel_ticket_arg_count", last_graph.backprojection_last_kernel_ticket.arg_count},
        }},
        {"execution_outcome", {
            {"completion_signaled", last_graph.completion_signaled},
            {"run_timeout_proxy", last_graph.run_timeout_proxy},
            {"run_exit_mode", last_graph.run_exit_mode},
            {"run_elapsed_ms", last_graph.run_ms},
        }},
        {"overhead_ms", {
            {"graph_run_minus_baseline_median", graph_overhead_ms},
            {"lifecycle_join_last", last_graph.join_ms},
        }},
        {"overhead_attribution", {
            {"edge_contract", "accel-token"},
            {"token_edge_payload_copies", 0},
            {"transfer_payload_bytes_h2d", last_graph.diagnostics.bytes_h2d},
            {"transfer_payload_bytes_d2h", last_graph.diagnostics.bytes_d2h},
            {"payload_copy_attribution", "transfer-stage counters only; graph edges carry accel tokens and SAR sidecars"},
            {"cost_buckets", {
                {"algorithm_baseline_ms", baseline_exec.median_ms},
                {"dsp_range_stage", options.range_stage == RangeStageKind::Compression ? "compression" : "window"},
                {"transfer_payload_bytes",
                 last_graph.diagnostics.bytes_h2d + last_graph.diagnostics.bytes_d2h},
                {"kernel_dispatches", last_graph.diagnostics.kernel_dispatches},
                {"graph_overhead_ms", graph_overhead_ms},
                {"diagnostics_contract", "sink-status"},
                {"range_compression_reference_ms", pr5_reference.range_compression_reference_ms},
                {"range_compression_runtime_ms", pr5_reference.range_compression_runtime_ms},
                {"matched_filter_vector_length", pr5_reference.matched_filter_vector_length},
                {"image_metric_ms", pr5_reference.image_metric_ms},
                {"graph_direct_peak_delta_pixels", pr5_reference.graph_direct_peak_delta_pixels},
            }},
        }},
        {"performance_claim_policy", {
            {"requires_bottleneck_attribution", true},
            {"disallow_lifecycle_total_as_speedup_basis", true},
            {"speedup_basis", "graph_run_minus_baseline_median"},
            {"lifecycle_total_reported_separately", true},
            {"attribution_evidence_present", attribution_evidence_present},
            {"attribution_evidence_keys", nlohmann::json::array({
                "timing_ms.graph_run.median_ms",
                "timing_ms.baseline_execute.median_ms",
                "overhead_ms.graph_run_minus_baseline_median",
                "overhead_attribution.cost_buckets.graph_overhead_ms",
                "overhead_attribution.transfer_payload_bytes_h2d",
                "overhead_attribution.transfer_payload_bytes_d2h",
                "overhead_attribution.cost_buckets.kernel_dispatches",
            })},
            {"no_speedup_claim_from", nlohmann::json::array({
                "timing_ms.graph_lifecycle_total",
                "last_lifecycle_ms.total",
                "overhead_ms.lifecycle_join_last",
            })},
        }},
        {"pr5_accuracy_fidelity", {
            {"matched_filter_reference", {
                {"sample_rate_hz", 16.0e6},
                {"bandwidth_hz", 4.0e6},
                {"chirp_duration_s", 1.0e-6},
                {"vector_length", pr5_reference.matched_filter_vector_length},
                {"peak_bin", pr5_reference.matched_filter_peak_bin},
                {"peak_value", pr5_reference.matched_filter_peak_value},
                {"reference_time_ms", pr5_reference.range_compression_reference_ms},
            }},
            {"runtime_matched_filter", {
                {"mode", pr5_reference.runtime_compression_mode},
                {"sample_rate_hz", 16.0e6},
                {"bandwidth_hz", 4.0e6},
                {"chirp_duration_s", 1.0e-6},
                {"output", "magnitude"},
                {"runtime_time_ms", pr5_reference.range_compression_runtime_ms},
                {"reference_l_inf", pr5_reference.runtime_reference_l_inf},
                {"reference_rms", pr5_reference.runtime_reference_rms},
                {"reference_relative_l2", pr5_reference.runtime_reference_relative_l2},
                {"parity_status", pr5_reference.runtime_reference_l_inf < 1.0e-4 ? "pass" : "fail"},
            }},
            {"image_metrics", {
                {"peak_location_error_pixels", pr5_reference.peak_location_error_pixels},
                {"impulse_response_width_pixels", pr5_reference.impulse_response_width_pixels},
                {"peak_sidelobe_ratio_db", pr5_reference.peak_sidelobe_ratio_db},
                {"integrated_sidelobe_ratio_db", pr5_reference.integrated_sidelobe_ratio_db},
                {"dynamic_range_db", pr5_reference.dynamic_range_db},
                {"image_hash", pr5_reference.image_hash},
                {"metric_time_ms", pr5_reference.image_metric_ms},
            }},
            {"graph_direct_metric_deltas", {
                {"peak_location_delta_pixels", pr5_reference.graph_direct_peak_delta_pixels},
                {"peak_value_delta", pr5_reference.graph_direct_peak_value_delta},
                {"basis", "deterministic CPU reference fixture; runtime graph currently emits diagnostics rather than image samples"},
            }},
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
            if (!options.native_backend) {
                VerifyDeterministicParity(last_graph, last_baseline);
            }

            graph_build_samples_ms.push_back(last_graph.build_ms);
            graph_run_samples_ms.push_back(last_graph.run_ms);
            graph_lifecycle_samples_ms.push_back(last_graph.lifecycle_total_ms);
            baseline_exec_samples_ms.push_back(last_baseline.execute_ms);
        }

        const auto graph_build_stats = ComputeStats(graph_build_samples_ms);
        const auto graph_run_stats = ComputeStats(graph_run_samples_ms);
        const auto graph_lifecycle_stats = ComputeStats(graph_lifecycle_samples_ms);
        const auto baseline_exec_stats = ComputeStats(baseline_exec_samples_ms);
        const auto pr5_reference_metrics = MeasurePr5ReferenceMetrics();

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
            pr5_reference_metrics,
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
                device_reduce_eval,
                pr5_reference_metrics);
            std::cout << "\nTrace written: " << options.trace_output_path->string() << "\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Benchmark failed: " << ex.what() << "\n";
        return 1;
    }
}
