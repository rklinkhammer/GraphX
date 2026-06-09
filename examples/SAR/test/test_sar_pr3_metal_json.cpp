#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphConfigParser.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/RangeCompressionNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

#include <nlohmann/json.hpp>

namespace {

std::shared_ptr<sar::SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
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

std::shared_ptr<sar::RangeCompressionNode> ResolveRangeCompressionNode(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        if (wrapper->GetType() != "RangeCompressionNode") {
            continue;
        }
        return wrapper->GetNode<sar::RangeCompressionNode>();
    }

    return nullptr;
}

void ValidateMetalSarConfig(const std::filesystem::path& config_path) {
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_TRUE(
        diagnostics.envelope.marker == sar::SarFrameMarker::EndOfStream ||
        diagnostics.envelope.marker == sar::SarFrameMarker::Data);
    EXPECT_EQ(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_EQ(diagnostics.bytes_h2d, 32768u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 32u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 28u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_GT(diagnostics.transfer_h2d_time_us, 0u);
    EXPECT_GT(diagnostics.kernel_exec_time_us, 0u);
    EXPECT_GT(diagnostics.transfer_d2h_time_us, 0u);
}

void ValidateMetalSarFanoutConfig(const std::filesystem::path& config_path) {
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 21U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 23U);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_TRUE(
        diagnostics.envelope.marker == sar::SarFrameMarker::EndOfStream ||
        diagnostics.envelope.marker == sar::SarFrameMarker::Data);
    EXPECT_GE(diagnostics.pulses_processed, 25u);
    EXPECT_LE(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_GT(diagnostics.bytes_h2d, 0u);
    EXPECT_GE(diagnostics.bytes_h2d, 102400u);
    EXPECT_LE(diagnostics.bytes_h2d, 131072u);
    EXPECT_GE(diagnostics.kernel_dispatches, 100u);
    EXPECT_LE(diagnostics.kernel_dispatches, 128u);
    EXPECT_EQ(diagnostics.duplicate_tile_count + diagnostics.tiles_processed,
              diagnostics.kernel_dispatches);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_GE(diagnostics.out_of_order_completion_count, 0u);
    EXPECT_LE(diagnostics.out_of_order_completion_count, 3u);
    EXPECT_GT(diagnostics.transfer_h2d_time_us, 0u);
    EXPECT_GT(diagnostics.kernel_exec_time_us, 0u);
    EXPECT_GT(diagnostics.transfer_d2h_time_us, 0u);
}

void ValidateAccelTokenResolverMetadata(const std::filesystem::path& config_path) {
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::ifstream in(config_path);
    ASSERT_TRUE(in.good());

    nlohmann::json config;
    in >> config;

    ASSERT_TRUE(config.contains("execution_backend"));
    EXPECT_EQ(config.at("execution_backend").get<std::string>(), "metal");
    ASSERT_TRUE(config.contains("backend_fallback_policy"));
    EXPECT_EQ(config.at("backend_fallback_policy").get<std::string>(), "allow_fallback");
    ASSERT_TRUE(config.contains("resolver_diagnostics"));
    EXPECT_TRUE(config.at("resolver_diagnostics").get<bool>());
    ASSERT_TRUE(config.contains("edge_contract"));
    EXPECT_EQ(config.at("edge_contract").get<std::string>(), "accel-token");

    const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(config_path.string());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->resolver.execution_backend, "metal");
    EXPECT_EQ(parsed->resolver.backend_fallback_policy, "allow_fallback");
    EXPECT_TRUE(parsed->resolver.resolver_diagnostics);
    EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

    const std::set<std::string> generic_intents{
        "H2DAsyncNode",
        "SarBackprojectionTransformNode",
        "D2HAsyncNode",
    };
    std::set<std::string> seen_intents;

    ASSERT_TRUE(config.contains("nodes"));
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        EXPECT_FALSE(type.ends_with("Metal")) << "portable PR3 presets should use generic intents";
        if (generic_intents.contains(type)) {
            seen_intents.insert(type);
        }

        if (!node.contains("node_config") || !node.at("node_config").is_object()) {
            continue;
        }
        const auto& node_config = node.at("node_config");
        if (node_config.contains("backend")) {
            EXPECT_NE(node_config.at("backend").get<int>(), 2)
                << "backend=2 must not stand in for resolver-selected accel-token variants";
        }
    }

    EXPECT_EQ(seen_intents, generic_intents);
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH
#define SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr3_metal_window.json"
#endif

#ifndef SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH
#define SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr3_metal_compression.json"
#endif

#ifndef SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH
#define SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr3_metal_fanout.json"
#endif

#ifndef SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH
#define SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr6_matched_filter.json"
#endif

TEST(SarPr3MetalJsonTest, ExecutesMetalWindowPipeline) {
    ValidateMetalSarConfig(std::filesystem::path{SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, ExecutesMetalCompressionPipeline) {
    ValidateMetalSarConfig(std::filesystem::path{SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, MetalWindowPresetUsesAccelTokenResolverMetadata) {
    ValidateAccelTokenResolverMetadata(std::filesystem::path{SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, MetalCompressionPresetUsesAccelTokenResolverMetadata) {
    ValidateAccelTokenResolverMetadata(std::filesystem::path{SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, ExecutesMetalFanoutPipeline) {
    ValidateMetalSarFanoutConfig(std::filesystem::path{SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, MetalFanoutPresetUsesAccelTokenResolverMetadata) {
    ValidateAccelTokenResolverMetadata(std::filesystem::path{SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, ExecutesPr6MatchedFilterPipeline) {
    const std::filesystem::path config_path{SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH};
    ValidateMetalSarConfig(config_path);

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    auto compression = ResolveRangeCompressionNode(executor->GetGraphManager());
    ASSERT_NE(compression, nullptr);
    EXPECT_EQ(compression->GetConfig().mode, sar::RangeCompressionMode::MatchedFilter);
    EXPECT_EQ(compression->GetConfig().output, sar::RangeCompressionOutput::Magnitude);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
}

TEST(SarPr3MetalJsonTest, Pr6MatchedFilterPresetUsesAccelTokenResolverMetadata) {
    ValidateAccelTokenResolverMetadata(std::filesystem::path{SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH});
}
