#include <gtest/gtest.h>

#include "sar_reference_parity_fixture.hpp"

#include <cstdlib>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

namespace reference_parity = sar::test::reference_parity;

#ifndef SAR_BENCHMARK_EXECUTABLE_PATH
#define SAR_BENCHMARK_EXECUTABLE_PATH "./sar_benchmark"
#endif

std::string QuoteArg(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

TEST(SarTraceSchemaTest, BenchmarkTraceContainsRequiredSchemaAndDiagnosticsFields) {
    const std::filesystem::path benchmark_path{SAR_BENCHMARK_EXECUTABLE_PATH};
    if (!std::filesystem::exists(benchmark_path)) {
        GTEST_SKIP() << "sar_benchmark executable not found: " << benchmark_path;
    }

    const auto trace_path = std::filesystem::temp_directory_path() / "sar_trace_schema_test.json";
    std::error_code ec;
    std::filesystem::remove(trace_path, ec);

    const std::string command =
        QuoteArg(benchmark_path.string()) +
        " --profile=ci --range-stage=compression --native-backend --trace-out " +
        QuoteArg(trace_path.string());
    const int exit_code = std::system(command.c_str());
    ASSERT_EQ(exit_code, 0) << "Command failed: " << command;

    ASSERT_TRUE(std::filesystem::exists(trace_path));

    std::ifstream in(trace_path);
    ASSERT_TRUE(in.good());

    nlohmann::json trace;
    in >> trace;

    // These keys define the benchmark trace contract that native-backend parity must preserve.
    const std::array<const char*, 13> required_trace_top_level_keys{{
        "schema",
        "timing_ms",
        "profile",
        "diagnostics",
        "queue",
        "overhead_attribution",
        "overhead_ms",
        "matched_filter_fidelity",
        "execution_outcome",
        "token_lifecycle",
        "resolved_nodes",
        "native_execution_evidence",
        "native_backend_parity",
    }};
    for (const char* key : required_trace_top_level_keys) {
        EXPECT_TRUE(trace.contains(key)) << "Missing required trace key: " << key;
    }

    ASSERT_TRUE(trace.contains("schema"));
    EXPECT_EQ(trace.at("schema").get<std::string>(), "graphx.sar.benchmark.trace.v1");

    ASSERT_TRUE(trace.contains("timing_ms"));
    ASSERT_TRUE(trace.at("timing_ms").contains("graph_run"));
    ASSERT_TRUE(trace.at("timing_ms").contains("graph_lifecycle_total"));
    ASSERT_TRUE(trace.contains("profile"));
    EXPECT_TRUE(trace.at("profile").contains("range_stage"));
    EXPECT_TRUE(trace.at("profile").contains("native_backend"));
    EXPECT_TRUE(trace.at("profile").contains("execution_backend"));
    EXPECT_TRUE(trace.at("profile").contains("backend_fallback_policy"));
    EXPECT_TRUE(trace.at("profile").contains("edge_contract"));
    EXPECT_EQ(trace.at("profile").at("range_stage").get<std::string>(), "compression");
    EXPECT_TRUE(trace.at("profile").at("native_backend").get<bool>());
    EXPECT_EQ(trace.at("profile").at("execution_backend").get<std::string>(), "metal");
    EXPECT_EQ(trace.at("profile").at("edge_contract").get<std::string>(), "accel-token");

    ASSERT_TRUE(trace.contains("diagnostics"));
    const auto& diagnostics = trace.at("diagnostics");
    EXPECT_TRUE(diagnostics.contains("pulses_processed"));
    EXPECT_TRUE(diagnostics.contains("tiles_processed"));
    EXPECT_TRUE(diagnostics.contains("bytes_h2d"));
    EXPECT_TRUE(diagnostics.contains("bytes_d2h"));
    EXPECT_TRUE(diagnostics.contains("kernel_dispatches"));
    EXPECT_TRUE(diagnostics.contains("transfer_h2d_time_us"));
    EXPECT_TRUE(diagnostics.contains("kernel_exec_time_us"));
    EXPECT_TRUE(diagnostics.contains("transfer_d2h_time_us"));
    EXPECT_TRUE(diagnostics.contains("range_window_time_us"));
    EXPECT_TRUE(diagnostics.contains("range_compression_time_us"));
    EXPECT_TRUE(diagnostics.contains("split_time_us"));
    EXPECT_TRUE(diagnostics.contains("h2d_stage_time_us"));
    EXPECT_TRUE(diagnostics.contains("backprojection_stage_time_us"));
    EXPECT_TRUE(diagnostics.contains("d2h_stage_time_us"));
    EXPECT_TRUE(diagnostics.contains("merge_stage_time_us"));
    EXPECT_TRUE(diagnostics.contains("diagnostics_sink_time_us"));
    EXPECT_TRUE(diagnostics.contains("fanin_wait_ms"));
    EXPECT_TRUE(diagnostics.contains("duplicate_tile_count"));
    EXPECT_TRUE(diagnostics.contains("missing_tile_count"));
    EXPECT_TRUE(diagnostics.contains("out_of_order_completion_count"));
    if (trace.at("profile").at("range_stage").get<std::string>() == "window") {
        EXPECT_GT(diagnostics.at("range_window_time_us").get<std::uint64_t>(), 0u);
    } else {
        EXPECT_EQ(diagnostics.at("range_window_time_us").get<std::uint64_t>(), 0u);
    }
    EXPECT_GT(diagnostics.at("range_compression_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("split_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("h2d_stage_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("backprojection_stage_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("d2h_stage_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("merge_stage_time_us").get<std::uint64_t>(), 0u);
    EXPECT_GT(diagnostics.at("diagnostics_sink_time_us").get<std::uint64_t>(), 0u);

    ASSERT_TRUE(trace.contains("queue"));
    const auto& queue = trace.at("queue");
    EXPECT_TRUE(queue.contains("backpressure_events"));
    EXPECT_TRUE(queue.contains("peak_queue_depth"));

    ASSERT_TRUE(trace.contains("overhead_attribution"));
    const auto& overhead = trace.at("overhead_attribution");
    EXPECT_EQ(overhead.at("edge_contract").get<std::string>(), "accel-token");
    EXPECT_EQ(overhead.at("token_edge_payload_copies").get<int>(), 0);
    EXPECT_TRUE(overhead.contains("transfer_payload_bytes_h2d"));
    EXPECT_TRUE(overhead.contains("transfer_payload_bytes_d2h"));
    EXPECT_TRUE(overhead.at("payload_copy_attribution")
                    .get<std::string>()
                    .find("graph edges carry accel tokens") != std::string::npos);
    ASSERT_TRUE(overhead.contains("cost_buckets"));
    const auto& cost_buckets = overhead.at("cost_buckets");
    EXPECT_TRUE(cost_buckets.contains("algorithm_baseline_ms"));
    EXPECT_TRUE(cost_buckets.contains("dsp_range_stage"));
    EXPECT_TRUE(cost_buckets.contains("transfer_payload_bytes"));
    EXPECT_TRUE(cost_buckets.contains("kernel_dispatches"));
    EXPECT_TRUE(cost_buckets.contains("graph_overhead_ms"));
    EXPECT_TRUE(cost_buckets.contains("diagnostics_contract"));
    EXPECT_TRUE(cost_buckets.contains("range_compression_reference_ms"));
    EXPECT_TRUE(cost_buckets.contains("range_compression_runtime_ms"));
    EXPECT_TRUE(cost_buckets.contains("matched_filter_vector_length"));
    EXPECT_TRUE(cost_buckets.contains("image_metric_ms"));
    EXPECT_TRUE(cost_buckets.contains("graph_direct_peak_delta_pixels"));
    EXPECT_EQ(cost_buckets.at("dsp_range_stage").get<std::string>(), "compression");
    EXPECT_GT(cost_buckets.at("transfer_payload_bytes").get<std::uint64_t>(), 0u);
    EXPECT_GT(cost_buckets.at("kernel_dispatches").get<std::uint64_t>(), 0u);
    EXPECT_EQ(cost_buckets.at("matched_filter_vector_length").get<std::uint32_t>(), 16u);

    ASSERT_TRUE(trace.contains("overhead_ms"));
    const auto& overhead_ms = trace.at("overhead_ms");
    EXPECT_TRUE(overhead_ms.contains("graph_run_minus_baseline_median"));
    EXPECT_TRUE(overhead_ms.contains("lifecycle_join_last"));
    EXPECT_GE(overhead_ms.at("graph_run_minus_baseline_median").get<double>(), 0.0);
    EXPECT_GE(overhead_ms.at("lifecycle_join_last").get<double>(), 0.0);
    EXPECT_NEAR(
        overhead_ms.at("graph_run_minus_baseline_median").get<double>(),
        cost_buckets.at("graph_overhead_ms").get<double>(),
        1.0e-9);

    ASSERT_TRUE(trace.contains("performance_claim_policy"));
    const auto& claim_policy = trace.at("performance_claim_policy");
    EXPECT_TRUE(claim_policy.contains("requires_bottleneck_attribution"));
    EXPECT_TRUE(claim_policy.contains("disallow_lifecycle_total_as_speedup_basis"));
    EXPECT_TRUE(claim_policy.contains("speedup_basis"));
    EXPECT_TRUE(claim_policy.contains("lifecycle_total_reported_separately"));
    EXPECT_TRUE(claim_policy.contains("attribution_evidence_present"));
    EXPECT_TRUE(claim_policy.contains("attribution_evidence_keys"));
    EXPECT_TRUE(claim_policy.contains("no_speedup_claim_from"));

    EXPECT_TRUE(claim_policy.at("requires_bottleneck_attribution").get<bool>());
    EXPECT_TRUE(claim_policy.at("disallow_lifecycle_total_as_speedup_basis").get<bool>());
    EXPECT_EQ(claim_policy.at("speedup_basis").get<std::string>(), "graph_run_minus_baseline_median");
    EXPECT_TRUE(claim_policy.at("lifecycle_total_reported_separately").get<bool>());
    EXPECT_TRUE(claim_policy.at("attribution_evidence_present").get<bool>());

    ASSERT_TRUE(claim_policy.at("attribution_evidence_keys").is_array());
    EXPECT_GE(claim_policy.at("attribution_evidence_keys").size(), 5u);
    ASSERT_TRUE(claim_policy.at("no_speedup_claim_from").is_array());
    EXPECT_GE(claim_policy.at("no_speedup_claim_from").size(), 2u);

    ASSERT_TRUE(trace.contains("matched_filter_fidelity"));
    const auto& fidelity = trace.at("matched_filter_fidelity");
    ASSERT_TRUE(fidelity.contains("matched_filter_reference"));
    ASSERT_TRUE(fidelity.contains("runtime_matched_filter"));
    ASSERT_TRUE(fidelity.contains("image_metrics"));
    ASSERT_TRUE(fidelity.contains("graph_direct_metric_deltas"));

    const std::array<const char*, 4> required_fidelity_keys{{
        "matched_filter_reference",
        "runtime_matched_filter",
        "image_metrics",
        "graph_direct_metric_deltas",
    }};
    for (const char* key : required_fidelity_keys) {
        EXPECT_TRUE(fidelity.contains(key)) << "Missing required matched_filter_fidelity key: " << key;
    }

    const auto& matched_filter = fidelity.at("matched_filter_reference");
    EXPECT_EQ(matched_filter.at("vector_length").get<std::uint32_t>(), reference_parity::kMatchedFilterVectorLength);
    EXPECT_EQ(matched_filter.at("peak_bin").get<std::uint32_t>(), reference_parity::kMatchedFilterPeakBin);
    EXPECT_NEAR(matched_filter.at("peak_value").get<double>(), reference_parity::kMatchedFilterPeakValue, 1.0e-5);
    EXPECT_GE(matched_filter.at("reference_time_ms").get<double>(), 0.0);

    const auto& runtime_filter = fidelity.at("runtime_matched_filter");
    EXPECT_EQ(runtime_filter.at("mode").get<std::string>(), "matched_filter");
    EXPECT_EQ(runtime_filter.at("output").get<std::string>(), "magnitude");
    EXPECT_GE(runtime_filter.at("runtime_time_ms").get<double>(), 0.0);
    EXPECT_LT(runtime_filter.at("reference_l_inf").get<double>(), reference_parity::kMatchedFilterReferenceLInfTolerance);
    EXPECT_LT(runtime_filter.at("reference_rms").get<double>(), reference_parity::kMatchedFilterReferenceRmsTolerance);
    EXPECT_EQ(runtime_filter.at("parity_status").get<std::string>(), "pass");

    const auto& image_metrics = fidelity.at("image_metrics");
    EXPECT_DOUBLE_EQ(image_metrics.at("peak_location_error_pixels").get<double>(), reference_parity::kImagePeakLocationErrorTolerancePixels);
    EXPECT_GE(image_metrics.at("dynamic_range_db").get<double>(), reference_parity::kImageDynamicRangeMinDb);
    EXPECT_NE(image_metrics.at("image_hash").get<std::uint64_t>(), 0u);

    const auto& metric_deltas = fidelity.at("graph_direct_metric_deltas");
    EXPECT_DOUBLE_EQ(metric_deltas.at("peak_location_delta_pixels").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metric_deltas.at("peak_value_delta").get<double>(), 0.0);

    ASSERT_TRUE(trace.contains("execution_outcome"));
    const auto& execution_outcome = trace.at("execution_outcome");
    EXPECT_TRUE(execution_outcome.contains("completion_signaled"));
    EXPECT_TRUE(execution_outcome.contains("run_timeout_proxy"));
    EXPECT_TRUE(execution_outcome.contains("run_exit_mode"));
    EXPECT_TRUE(execution_outcome.contains("run_elapsed_ms"));
    ASSERT_TRUE(execution_outcome.at("completion_signaled").get<bool>())
        << "benchmark did not complete normally; run_exit_mode="
        << execution_outcome.at("run_exit_mode").get<std::string>()
        << ", run_timeout_proxy="
        << (execution_outcome.at("run_timeout_proxy").get<bool>() ? "true" : "false")
        << ", run_elapsed_ms="
        << execution_outcome.at("run_elapsed_ms").get<double>();
    EXPECT_FALSE(execution_outcome.at("run_timeout_proxy").get<bool>());
    EXPECT_EQ(execution_outcome.at("run_exit_mode").get<std::string>(), "completion_signaled");

    ASSERT_TRUE(trace.contains("token_lifecycle"));
    const auto& tokens = trace.at("token_lifecycle");
    EXPECT_TRUE(tokens.at("has_host_view").get<bool>());
    EXPECT_TRUE(tokens.at("has_transfer_ticket").get<bool>());
    EXPECT_TRUE(tokens.at("has_kernel_ticket").get<bool>());
    EXPECT_TRUE(tokens.contains("host_view"));
    EXPECT_TRUE(tokens.contains("device_view"));
    EXPECT_TRUE(tokens.contains("lease"));
    EXPECT_TRUE(tokens.contains("transfer_ticket"));
    EXPECT_TRUE(tokens.contains("kernel_ticket"));
    EXPECT_GT(tokens.at("host_view").at("host_view_handle").get<std::uint64_t>(), 0u);
    EXPECT_GT(tokens.at("transfer_ticket").at("transfer_id").get<std::uint64_t>(), 0u);
    EXPECT_GT(tokens.at("kernel_ticket").at("kernel_id").get<std::uint64_t>(), 0u);
    EXPECT_GT(tokens.at("kernel_ticket").at("queue_id").get<std::uint64_t>(), 0u);

    ASSERT_TRUE(trace.contains("resolved_nodes"));
    ASSERT_TRUE(trace.at("resolved_nodes").is_array());
    EXPECT_GE(trace.at("resolved_nodes").size(), 3u);
    for (const auto& node : trace.at("resolved_nodes")) {
        EXPECT_TRUE(node.contains("intent_type"));
        EXPECT_TRUE(node.contains("concrete_type"));
        EXPECT_TRUE(node.contains("selected_backend"));
        EXPECT_TRUE(node.contains("fallback_reason"));
        EXPECT_TRUE(node.contains("input_token_type"));
        EXPECT_TRUE(node.contains("output_token_type"));
        EXPECT_EQ(node.at("selected_backend").get<std::string>(), "metal");
        EXPECT_EQ(node.at("fallback_reason").get<std::string>(), "none");
    }

    ASSERT_TRUE(trace.contains("native_execution_evidence"));
    const auto& native = trace.at("native_execution_evidence");
    EXPECT_TRUE(native.contains("requested_native_backend"));
    EXPECT_TRUE(native.contains("resolved_execution_backend"));
    EXPECT_TRUE(native.contains("backprojection_concrete_type"));
    EXPECT_TRUE(native.contains("backprojection_native_kernel_bound"));
    EXPECT_TRUE(native.contains("backprojection_native_kernel_executed"));
    EXPECT_TRUE(native.contains("kernel_ticket_backend"));
    EXPECT_TRUE(native.contains("kernel_ticket_id"));
    EXPECT_TRUE(native.contains("kernel_ticket_queue_id"));
    EXPECT_TRUE(native.contains("kernel_ticket_arg_count"));

    EXPECT_TRUE(native.at("requested_native_backend").get<bool>());
    EXPECT_EQ(native.at("resolved_execution_backend").get<std::string>(), "metal");
    EXPECT_EQ(native.at("kernel_ticket_backend").get<std::string>(), "metal");
    EXPECT_TRUE(native.at("backprojection_native_kernel_bound").get<bool>());

    ASSERT_TRUE(trace.contains("native_metal_telemetry"));
    const auto& native_metal_telemetry = trace.at("native_metal_telemetry");
    EXPECT_TRUE(native_metal_telemetry.contains("transfer_samples"));
    EXPECT_TRUE(native_metal_telemetry.contains("kernel_samples"));
    EXPECT_TRUE(native_metal_telemetry.contains("error_count"));
    EXPECT_TRUE(native_metal_telemetry.contains("transfer_total_duration_ns"));
    EXPECT_TRUE(native_metal_telemetry.contains("kernel_total_duration_ns"));
    EXPECT_TRUE(native_metal_telemetry.contains("last_transfer_duration_ns"));
    EXPECT_TRUE(native_metal_telemetry.contains("last_kernel_duration_ns"));
    EXPECT_TRUE(native_metal_telemetry.contains("h2d_transfer_samples"));
    EXPECT_TRUE(native_metal_telemetry.contains("d2h_transfer_samples"));
    EXPECT_TRUE(native_metal_telemetry.contains("d2d_transfer_samples"));
    const auto transfer_samples =
        native_metal_telemetry.at("transfer_samples").get<std::uint64_t>();
    const auto kernel_samples =
        native_metal_telemetry.at("kernel_samples").get<std::uint64_t>();
    const auto transfer_total_duration_ns =
        native_metal_telemetry.at("transfer_total_duration_ns").get<std::uint64_t>();
    const auto kernel_total_duration_ns =
        native_metal_telemetry.at("kernel_total_duration_ns").get<std::uint64_t>();
    const auto last_transfer_duration_ns =
        native_metal_telemetry.at("last_transfer_duration_ns").get<std::uint64_t>();
    const auto last_kernel_duration_ns =
        native_metal_telemetry.at("last_kernel_duration_ns").get<std::uint64_t>();
    const auto h2d_transfer_samples =
        native_metal_telemetry.at("h2d_transfer_samples").get<std::uint64_t>();
    const auto d2h_transfer_samples =
        native_metal_telemetry.at("d2h_transfer_samples").get<std::uint64_t>();
    const auto d2d_transfer_samples =
        native_metal_telemetry.at("d2d_transfer_samples").get<std::uint64_t>();

    EXPECT_EQ(transfer_samples,
              h2d_transfer_samples + d2h_transfer_samples + d2d_transfer_samples);
    EXPECT_GE(transfer_total_duration_ns, last_transfer_duration_ns);
    EXPECT_GE(kernel_total_duration_ns, last_kernel_duration_ns);
    if (transfer_samples == 0u) {
        EXPECT_EQ(transfer_total_duration_ns, 0u);
        EXPECT_EQ(last_transfer_duration_ns, 0u);
    }
    if (kernel_samples == 0u) {
        EXPECT_EQ(kernel_total_duration_ns, 0u);
        EXPECT_EQ(last_kernel_duration_ns, 0u);
    }

    ASSERT_TRUE(trace.contains("native_metal_memory_pool"));
    const auto& native_metal_memory_pool = trace.at("native_metal_memory_pool");
    EXPECT_TRUE(native_metal_memory_pool.contains("live_device_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("live_shared_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("live_host_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("peak_device_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("peak_shared_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("peak_host_bytes"));
    EXPECT_TRUE(native_metal_memory_pool.contains("allocation_count"));
    EXPECT_TRUE(native_metal_memory_pool.contains("release_count"));
    const auto live_device_bytes =
        native_metal_memory_pool.at("live_device_bytes").get<std::uint64_t>();
    const auto live_shared_bytes =
        native_metal_memory_pool.at("live_shared_bytes").get<std::uint64_t>();
    const auto live_host_bytes =
        native_metal_memory_pool.at("live_host_bytes").get<std::uint64_t>();
    const auto peak_device_bytes =
        native_metal_memory_pool.at("peak_device_bytes").get<std::uint64_t>();
    const auto peak_shared_bytes =
        native_metal_memory_pool.at("peak_shared_bytes").get<std::uint64_t>();
    const auto peak_host_bytes =
        native_metal_memory_pool.at("peak_host_bytes").get<std::uint64_t>();
    const auto allocation_count =
        native_metal_memory_pool.at("allocation_count").get<std::uint64_t>();
    const auto release_count =
        native_metal_memory_pool.at("release_count").get<std::uint64_t>();

    EXPECT_GE(peak_device_bytes, live_device_bytes);
    EXPECT_GE(peak_shared_bytes, live_shared_bytes);
    EXPECT_GE(peak_host_bytes, live_host_bytes);
    EXPECT_GE(allocation_count, release_count);
    if (allocation_count == 0u) {
        EXPECT_EQ(release_count, 0u);
        EXPECT_EQ(peak_device_bytes, 0u);
        EXPECT_EQ(peak_shared_bytes, 0u);
        EXPECT_EQ(peak_host_bytes, 0u);
    }
    EXPECT_TRUE(native.at("backprojection_native_kernel_executed").get<bool>());
    EXPECT_GE(native.at("kernel_ticket_arg_count").get<std::uint64_t>(), 1u);
    EXPECT_GT(native.at("kernel_ticket_id").get<std::uint64_t>(), 0u);
    EXPECT_GT(native.at("kernel_ticket_queue_id").get<std::uint64_t>(), 0u);

    ASSERT_TRUE(trace.contains("native_backend_parity"));
    const auto& native_parity = trace.at("native_backend_parity");
    EXPECT_TRUE(native_parity.contains("canonical_token_path_locked"));
    EXPECT_TRUE(native_parity.contains("dual_path_artifacts_detected"));
    EXPECT_TRUE(native_parity.contains("portable_intent_types_only"));
    EXPECT_TRUE(native_parity.contains("native_backend_requested"));
    EXPECT_TRUE(native_parity.contains("native_backend_resolved"));
    EXPECT_TRUE(native_parity.contains("backprojection_native_kernel_executed"));
    EXPECT_TRUE(native_parity.at("canonical_token_path_locked").get<bool>());
    EXPECT_FALSE(native_parity.at("dual_path_artifacts_detected").get<bool>());
    EXPECT_TRUE(native_parity.at("portable_intent_types_only").get<bool>());
    EXPECT_TRUE(native_parity.at("native_backend_requested").get<bool>());
    EXPECT_TRUE(native_parity.at("native_backend_resolved").get<bool>());
    EXPECT_TRUE(native_parity.at("backprojection_native_kernel_executed").get<bool>());
}

} // namespace
