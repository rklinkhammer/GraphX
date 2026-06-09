#include <gtest/gtest.h>

#include "graph/GraphConfigParser.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

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

nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "unable to open preset: " << path;

    nlohmann::json json;
    in >> json;
    return json;
}

void AssertPortableIntents(const nlohmann::json& config) {
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    std::set<std::string> seen_intents;
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        EXPECT_FALSE(type.ends_with("Metal")) << "preset must keep portable node intent types";
        if (type == "H2DAsyncNode" || type == "SarBackprojectionTransformNode" || type == "D2HAsyncNode") {
            seen_intents.insert(type);
        }
    }

    EXPECT_TRUE(seen_intents.contains("H2DAsyncNode"));
    EXPECT_TRUE(seen_intents.contains("SarBackprojectionTransformNode"));
    EXPECT_TRUE(seen_intents.contains("D2HAsyncNode"));

    ASSERT_TRUE(config.contains("resolver_mappings"));
    ASSERT_TRUE(config.at("resolver_mappings").is_array());
    bool has_backprojection_mapping = false;
    for (const auto& mapping : config.at("resolver_mappings")) {
        if (!mapping.is_object() || !mapping.contains("intent_type")) {
            continue;
        }
        if (mapping.at("intent_type").get<std::string>() == "SarBackprojectionTransformNode") {
            has_backprojection_mapping = true;
            ASSERT_TRUE(mapping.contains("variants"));
            ASSERT_TRUE(mapping.at("variants").is_array());
        }
    }
    EXPECT_TRUE(has_backprojection_mapping);
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_JSON_CONFIG_PATH
#define SAR_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr1.json"
#endif

#ifndef SAR_DEFINITIVE_JSON_CONFIG_PATH
#define SAR_DEFINITIVE_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_definitive.json"
#endif

TEST(SarJsonRuntimeTest, JsonTopologyRunsWithProviderBootstrapPath) {
    const std::filesystem::path config_path{SAR_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 8U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 7U);

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());
    EXPECT_GT(sink->consume_count(), 0u);

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_EQ(diagnostics.bytes_h2d, 32768u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 32u);
    EXPECT_EQ(diagnostics.fanin_wait_ms, 32u);
    EXPECT_EQ(diagnostics.e2e_latency_ms, 32u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 28u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_EQ(
        diagnostics.queue_backpressure_events,
        executor->GetGraphManager()->GetMetrics().backpressure_events.load(std::memory_order_relaxed));
    EXPECT_EQ(
        diagnostics.peak_queue_depth,
        executor->GetGraphManager()->GetMetrics().peak_queue_depth.load(std::memory_order_relaxed));
}

TEST(SarJsonRuntimeTest, MaintainedPresetsKeepAccelTokenAndResolverContractExplicit) {
    struct PresetExpectation {
        const char* path;
        const char* backend;
    };

    const std::vector<PresetExpectation> presets{
        {SAR_JSON_CONFIG_PATH, "auto"},
        {SAR_PR2_FANOUT_JSON_CONFIG_PATH, "auto"},
        {SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH, "metal"},
        {SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH, "metal"},
        {SAR_PR3_METAL_FANOUT_JSON_CONFIG_PATH, "metal"},
        {SAR_PR6_MATCHED_FILTER_JSON_CONFIG_PATH, "metal"},
        {SAR_PR7_MATERIALIZED_IMAGE_JSON_CONFIG_PATH, "auto"},
        {SAR_PROJECTILE_JSON_CONFIG_PATH, "auto"},
    };

    for (const auto& preset : presets) {
        const std::filesystem::path config_path{preset.path};
        ASSERT_TRUE(std::filesystem::exists(config_path)) << "missing preset " << config_path;

        const auto config = LoadJsonFile(config_path);
        ASSERT_TRUE(config.contains("execution_backend"));
        ASSERT_TRUE(config.contains("backend_fallback_policy"));
        ASSERT_TRUE(config.contains("resolver_diagnostics"));
        ASSERT_TRUE(config.contains("edge_contract"));

        EXPECT_EQ(config.at("execution_backend").get<std::string>(), preset.backend);
        EXPECT_EQ(config.at("backend_fallback_policy").get<std::string>(), "allow_fallback");
        EXPECT_TRUE(config.at("resolver_diagnostics").get<bool>());
        EXPECT_EQ(config.at("edge_contract").get<std::string>(), "accel-token");

        const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(config_path.string());
        ASSERT_TRUE(parsed) << "failed parsing preset " << config_path;
        EXPECT_EQ(parsed->resolver.execution_backend, preset.backend);
        EXPECT_EQ(parsed->resolver.backend_fallback_policy, "allow_fallback");
        EXPECT_TRUE(parsed->resolver.resolver_diagnostics);
        EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

        AssertPortableIntents(config);
    }
}

TEST(SarJsonRuntimeTest, DefinitivePresetKeepsStrictResolverContractAndPortableIntent) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto config = LoadJsonFile(config_path);
    ASSERT_TRUE(config.contains("execution_backend"));
    ASSERT_TRUE(config.contains("backend_fallback_policy"));
    ASSERT_TRUE(config.contains("resolver_diagnostics"));
    ASSERT_TRUE(config.contains("edge_contract"));

    EXPECT_EQ(config.at("execution_backend").get<std::string>(), "auto");
    EXPECT_EQ(config.at("backend_fallback_policy").get<std::string>(), "strict");
    EXPECT_TRUE(config.at("resolver_diagnostics").get<bool>());
    EXPECT_EQ(config.at("edge_contract").get<std::string>(), "accel-token");

    const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(config_path.string());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->resolver.execution_backend, "auto");
    EXPECT_EQ(parsed->resolver.backend_fallback_policy, "strict");
    EXPECT_TRUE(parsed->resolver.resolver_diagnostics);
    EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

    AssertPortableIntents(config);
}

TEST(SarJsonRuntimeTest, DefinitivePresetSignalsCompletionInGraphExecutorPath) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
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
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 64u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_GT(diagnostics.bytes_h2d, 0u);
}

TEST(SarJsonRuntimeTest, DefinitivePresetStrictMetalSelectionFailsWithoutConcreteProvider) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto metal_config = LoadJsonFile(config_path);
    metal_config["execution_backend"] = "metal";

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_stripmap_definitive_runtime_metal.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    const auto parsed = graph::config::GraphConfigParser::ParseFileSafe(temp_path.string());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->resolver.execution_backend, "metal");
    EXPECT_EQ(parsed->resolver.backend_fallback_policy, "strict");
    EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

    EXPECT_THROW(
        {
            auto _ = graph::GraphExecutorBuilder()
                         .WithJsonConfig(temp_path.string())
                         .WithPluginDirectory(plugin_dir.string())
                         .WithExecutorTimeout(std::chrono::seconds(15))
                         .Build();
        },
        std::runtime_error);

    metal_config["backend_fallback_policy"] = "allow_fallback";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto fallback_executor = graph::GraphExecutorBuilder()
                                 .WithJsonConfig(temp_path.string())
                                 .WithPluginDirectory(plugin_dir.string())
                                 .WithExecutorTimeout(std::chrono::seconds(15))
                                 .Build();

    ASSERT_NE(fallback_executor, nullptr);
    ASSERT_NE(fallback_executor->GetGraphManager(), nullptr);

    const auto run_result = fallback_executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(fallback_executor->IsCompletionSignaled());

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}
