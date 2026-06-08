#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {

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
    EXPECT_TRUE(diagnostics.contains("fanin_wait_ms"));
    EXPECT_TRUE(diagnostics.contains("duplicate_tile_count"));
    EXPECT_TRUE(diagnostics.contains("missing_tile_count"));
    EXPECT_TRUE(diagnostics.contains("out_of_order_completion_count"));

    ASSERT_TRUE(trace.contains("queue"));
    const auto& queue = trace.at("queue");
    EXPECT_TRUE(queue.contains("backpressure_events"));
    EXPECT_TRUE(queue.contains("peak_queue_depth"));

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
    EXPECT_GT(tokens.at("host_view").at("host_ptr_token").get<std::uint64_t>(), 0u);
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
    }
}

} // namespace
