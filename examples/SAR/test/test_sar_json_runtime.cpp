// SPDX-License-Identifier: MIT

/**
 * @file test_sar_json_runtime.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/GraphBuilder.hpp"
#include "graph/GraphConfigParser.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeProviderBootstrap.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarRuntimeHelpers.hpp"
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
        if (type == "H2DAsyncAccelNode" || type == "SarBackprojectionTransformAccelNode" || type == "D2HAsyncAccelNode") {
            seen_intents.insert(type);
        }
    }

    EXPECT_TRUE(seen_intents.contains("H2DAsyncAccelNode"));
    EXPECT_TRUE(seen_intents.contains("SarBackprojectionTransformAccelNode"));
    EXPECT_TRUE(seen_intents.contains("D2HAsyncAccelNode"));

    ASSERT_TRUE(config.contains("resolver_mappings"));
    ASSERT_TRUE(config.at("resolver_mappings").is_array());
    bool has_backprojection_mapping = false;
    for (const auto& mapping : config.at("resolver_mappings")) {
        if (!mapping.is_object() || !mapping.contains("intent_type")) {
            continue;
        }
        ASSERT_TRUE(mapping.contains("input_token_type"));
        ASSERT_TRUE(mapping.contains("output_token_type"));
        EXPECT_EQ(mapping.at("input_token_type").get<std::string>(), "SarControlToken");
        EXPECT_EQ(mapping.at("output_token_type").get<std::string>(), "SarControlToken");
        if (mapping.at("intent_type").get<std::string>() == "SarBackprojectionTransformAccelNode") {
            has_backprojection_mapping = true;
            ASSERT_TRUE(mapping.contains("variants"));
            ASSERT_TRUE(mapping.at("variants").is_array());
        }
    }
    EXPECT_TRUE(has_backprojection_mapping);
}

void AssertDefinitiveSarAccelResolverMappings(const nlohmann::json& config) {
    ASSERT_TRUE(config.contains("resolver_mappings"));
    ASSERT_TRUE(config.at("resolver_mappings").is_array());

    const std::set<std::string> expected_intents{
        "H2DAsyncAccelNode",
        "SarBackprojectionTransformAccelNode",
        "D2HAsyncAccelNode",
    };
    std::set<std::string> seen_intents;

    for (const auto& mapping : config.at("resolver_mappings")) {
        ASSERT_TRUE(mapping.is_object());
        ASSERT_TRUE(mapping.contains("intent_type"));
        const auto intent = mapping.at("intent_type").get<std::string>();
        if (!expected_intents.contains(intent)) {
            continue;
        }

        seen_intents.insert(intent);
        ASSERT_TRUE(mapping.contains("input_token_type"));
        ASSERT_TRUE(mapping.contains("output_token_type"));
        EXPECT_EQ(mapping.at("input_token_type").get<std::string>(), "SarControlToken");
        EXPECT_EQ(mapping.at("output_token_type").get<std::string>(), "SarControlToken");
    }

    EXPECT_EQ(seen_intents, expected_intents);
}

void AssertSarAccelIntentNamesStayExplicit(const nlohmann::json& config) {
    ASSERT_TRUE(config.contains("resolver_mappings"));
    ASSERT_TRUE(config.at("resolver_mappings").is_array());

    const std::set<std::string> canonical_names{
        "H2DAsyncAccelNode",
        "SarBackprojectionTransformAccelNode",
        "D2HAsyncAccelNode",
    };
    const std::set<std::string> legacy_names{
        "H2DAsyncNode",
        "SarBackprojectionTransformNode",
        "D2HAsyncNode",
    };

    for (const auto& mapping : config.at("resolver_mappings")) {
        ASSERT_TRUE(mapping.is_object());
        const auto intent = mapping.value("intent_type", "");
        EXPECT_FALSE(legacy_names.contains(intent)) << "legacy SAR GPU intent name must not be used";
        if (!canonical_names.contains(intent)) {
            continue;
        }

        ASSERT_TRUE(mapping.contains("variants"));
        ASSERT_TRUE(mapping.at("variants").is_array());
        for (const auto& variant : mapping.at("variants")) {
            ASSERT_TRUE(variant.contains("concrete_type"));
            EXPECT_EQ(variant.at("concrete_type").get<std::string>(), intent)
                << intent << " must resolve directly to the explicit SAR-token accel node";
        }
    }
}

const graph::NodeResolutionDiagnostic* FindResolverDiagnostic(
    const std::vector<graph::NodeResolutionDiagnostic>& diagnostics,
    const std::string& intent_type) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.intent_type == intent_type) {
            return &diagnostic;
        }
    }
    return nullptr;
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef GPU_PLUGIN_OUTPUT_DIRECTORY
#define GPU_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_JSON_CONFIG_PATH
#define SAR_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_simulated.json"
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
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 8U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 7U);

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());
    EXPECT_GT(sink->consume_count(), 0u);

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.sidecar.marker, sar::SarFrameMarker::EndOfStream);
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
    // Test maintained canonical and specialized stripmap configs.
    // Stripmap family includes the basic simulated config, definitive config,
    // and specialized variants (fanout, matched_filter, materialized_image).
    struct PresetExpectation {
        const char* path;
        const char* backend;
    };

    const std::vector<PresetExpectation> presets{
        {SAR_JSON_CONFIG_PATH, "auto"},
        {SAR_FANOUT_JSON_CONFIG_PATH, "auto"},
        {SAR_MATCHED_FILTER_JSON_CONFIG_PATH, "metal"},
        {SAR_MATERIALIZED_IMAGE_JSON_CONFIG_PATH, "auto"},
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
        EXPECT_STREQ(graph::ToString(parsed->resolver.execution_backend), preset.backend);
        EXPECT_EQ(parsed->resolver.backend_fallback_policy, graph::ResolverFallbackPolicy::AllowFallback);
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
    EXPECT_EQ(parsed->resolver.execution_backend, graph::ResolverBackend::Auto);
    EXPECT_EQ(parsed->resolver.backend_fallback_policy, graph::ResolverFallbackPolicy::Strict);
    EXPECT_TRUE(parsed->resolver.resolver_diagnostics);
    EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

    AssertPortableIntents(config);
    AssertDefinitiveSarAccelResolverMappings(config);
    AssertSarAccelIntentNamesStayExplicit(config);
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
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 64u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_GT(diagnostics.bytes_h2d, 0u);
    EXPECT_GT(diagnostics.stage_timings.range_window_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.range_compression_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.split_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.h2d_stage_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.backprojection_stage_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.d2h_stage_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.merge_stage_time_us, 0u);
    EXPECT_GT(diagnostics.stage_timings.diagnostics_sink_time_us, 0u);
}

TEST(SarJsonRuntimeTest, DefinitivePresetStrictMetalSelectionUsesSarAccelTokenMappings) {
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
    EXPECT_EQ(parsed->resolver.execution_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(parsed->resolver.backend_fallback_policy, graph::ResolverFallbackPolicy::Strict);
    EXPECT_EQ(parsed->resolver.edge_contract, "accel-token");

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(temp_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
                        .Build();

    ASSERT_NE(executor, nullptr);
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& status = sink->last_token();
    EXPECT_EQ(status.sidecar.sequence_id, 64u);
    EXPECT_EQ(status.sidecar.batch_id, 0u);
    EXPECT_EQ(status.sidecar.aperture_id, 64u);
    EXPECT_EQ(status.sidecar.pulse_range_start, 64u);
    EXPECT_EQ(status.sidecar.pulse_range_count, 0u);
    EXPECT_EQ(status.sidecar.stream_id, 0u);
    EXPECT_LT(status.sidecar.tile_id, status.sidecar.tile_count);
    EXPECT_EQ(status.sidecar.tile_count, 4u);
    EXPECT_EQ(status.sidecar.backend_id, 0u);
    EXPECT_EQ(status.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_TRUE(status.has_host_view);
    EXPECT_TRUE(status.has_transfer_ticket);
    EXPECT_GT(status.transfer_ticket.execution_queue_id, 0u);
    if (status.has_kernel_ticket) {
        EXPECT_GT(status.kernel_ticket.execution_queue_id, 0u);
    }

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST(SarJsonRuntimeTest, DefinitivePresetResolvesCommonMetalNodesWithComposedProvider) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path sar_plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    const std::filesystem::path gpu_plugin_dir{GPU_PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(sar_plugin_dir));
    ASSERT_TRUE(std::filesystem::exists(gpu_plugin_dir));

    auto metal_config = LoadJsonFile(config_path);
    metal_config["execution_backend"] = "metal";
    metal_config["backend_fallback_policy"] = "strict";
    AssertDefinitiveSarAccelResolverMappings(metal_config);

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_stripmap_definitive_composed_provider_metal.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{gpu_plugin_dir.string(), sar_plugin_dir.string()});
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);
    EXPECT_GE(bootstrap->diagnostics.loaded_count, 3u);

    auto available = app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(bootstrap->provider);
    ASSERT_TRUE(available);
    const std::set<std::string> available_types(available->begin(), available->end());
    EXPECT_TRUE(available_types.contains("H2DAsyncNodeMetal"));
    EXPECT_TRUE(available_types.contains("D2HAsyncNodeMetal"));
    EXPECT_TRUE(available_types.contains("SarBackprojectionTransformAccelNode"));

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(temp_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    ASSERT_TRUE(build_result.success) << build_result.error_message;
    EXPECT_EQ(build_result.node_count, metal_config.at("nodes").size());
    EXPECT_EQ(build_result.edge_count, metal_config.at("edges").size());

    const auto* h2d = FindResolverDiagnostic(build_result.resolver_diagnostics, "H2DAsyncAccelNode");
    ASSERT_NE(h2d, nullptr);
    EXPECT_EQ(h2d->concrete_type, "H2DAsyncAccelNode");
    EXPECT_EQ(h2d->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(h2d->input_token_type, "SarControlToken");
    EXPECT_EQ(h2d->output_token_type, "SarControlToken");
    EXPECT_FALSE(h2d->fallback_used);

    const auto* d2h = FindResolverDiagnostic(build_result.resolver_diagnostics, "D2HAsyncAccelNode");
    ASSERT_NE(d2h, nullptr);
    EXPECT_EQ(d2h->concrete_type, "D2HAsyncAccelNode");
    EXPECT_EQ(d2h->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(d2h->input_token_type, "SarControlToken");
    EXPECT_EQ(d2h->output_token_type, "SarControlToken");
    EXPECT_FALSE(d2h->fallback_used);

    const auto* bp = FindResolverDiagnostic(
        build_result.resolver_diagnostics, "SarBackprojectionTransformAccelNode");
    ASSERT_NE(bp, nullptr);
    EXPECT_EQ(bp->concrete_type, "SarBackprojectionTransformAccelNode");
    EXPECT_EQ(bp->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(bp->input_token_type, "SarControlToken");
    EXPECT_EQ(bp->output_token_type, "SarControlToken");
    EXPECT_FALSE(bp->fallback_used);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST(SarJsonRuntimeTest, DefinitiveResolverDiagnosticsRequireNoFallbackForCanonicalGpuStages) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path sar_plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    const std::filesystem::path gpu_plugin_dir{GPU_PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(sar_plugin_dir));
    ASSERT_TRUE(std::filesystem::exists(gpu_plugin_dir));

    auto metal_config = LoadJsonFile(config_path);
    metal_config["execution_backend"] = "metal";
    metal_config["backend_fallback_policy"] = "strict";

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_stripmap_definitive_no_fallback_assertions.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{gpu_plugin_dir.string(), sar_plugin_dir.string()});
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(temp_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    ASSERT_TRUE(build_result.success) << build_result.error_message;

    const auto* h2d = FindResolverDiagnostic(build_result.resolver_diagnostics, "H2DAsyncAccelNode");
    const auto* d2h = FindResolverDiagnostic(build_result.resolver_diagnostics, "D2HAsyncAccelNode");
    const auto* bp = FindResolverDiagnostic(
        build_result.resolver_diagnostics, "SarBackprojectionTransformAccelNode");
    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(d2h, nullptr);
    ASSERT_NE(bp, nullptr);

    EXPECT_EQ(h2d->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(d2h->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(bp->selected_backend, graph::ResolverBackend::Metal);

    EXPECT_FALSE(h2d->fallback_used);
    EXPECT_FALSE(d2h->fallback_used);
    EXPECT_FALSE(bp->fallback_used);

    EXPECT_EQ(h2d->fallback_reason, graph::ResolverFallbackReason::None);
    EXPECT_EQ(d2h->fallback_reason, graph::ResolverFallbackReason::None);
    EXPECT_EQ(bp->fallback_reason, graph::ResolverFallbackReason::None);

    EXPECT_EQ(FindResolverDiagnostic(build_result.resolver_diagnostics, "H2DAsyncNode"), nullptr);
    EXPECT_EQ(FindResolverDiagnostic(build_result.resolver_diagnostics, "D2HAsyncNode"), nullptr);
    EXPECT_EQ(FindResolverDiagnostic(build_result.resolver_diagnostics, "SarBackprojectionTransformNode"), nullptr);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST(SarJsonRuntimeTest, DefinitivePresetPreservesEndToEndSidecarIdentity) {
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
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& status = sink->last_token();
    EXPECT_EQ(status.sidecar.sequence_id, 64u);
    EXPECT_EQ(status.sidecar.batch_id, 0u);
    EXPECT_EQ(status.sidecar.aperture_id, 64u);
    EXPECT_EQ(status.sidecar.pulse_range_start, 64u);
    EXPECT_EQ(status.sidecar.pulse_range_count, 0u);
    EXPECT_EQ(status.sidecar.stream_id, 0u);
    EXPECT_LT(status.sidecar.tile_id, status.sidecar.tile_count);
    EXPECT_EQ(status.sidecar.tile_count, 4u);
    EXPECT_EQ(status.sidecar.backend_id, 0u);
    EXPECT_EQ(status.sidecar.marker, sar::SarFrameMarker::EndOfStream);

    EXPECT_TRUE(status.has_host_view);
    EXPECT_TRUE(status.has_transfer_ticket);
    EXPECT_GT(status.transfer_ticket.execution_queue_id, 0u);
    if (status.has_kernel_ticket) {
        EXPECT_GT(status.kernel_ticket.execution_queue_id, 0u);
    }
}

TEST(SarJsonRuntimeTest, ResolverSelectedDeviceStagesPreserveSidecarIdentityAndOpaqueTransportBoundaries) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto config = LoadJsonFile(config_path);
    AssertDefinitiveSarAccelResolverMappings(config);

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{plugin_dir.string()});
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(config_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    ASSERT_TRUE(build_result.success) << build_result.error_message;

    const auto* h2d_diagnostic = FindResolverDiagnostic(
        build_result.resolver_diagnostics, "H2DAsyncAccelNode");
    const auto* bp_diagnostic = FindResolverDiagnostic(
        build_result.resolver_diagnostics, "SarBackprojectionTransformAccelNode");
    const auto* d2h_diagnostic = FindResolverDiagnostic(
        build_result.resolver_diagnostics, "D2HAsyncAccelNode");
    ASSERT_NE(h2d_diagnostic, nullptr);
    ASSERT_NE(bp_diagnostic, nullptr);
    ASSERT_NE(d2h_diagnostic, nullptr);
    EXPECT_EQ(FindResolverDiagnostic(build_result.resolver_diagnostics, "H2DAsyncNode"), nullptr);
    EXPECT_EQ(FindResolverDiagnostic(build_result.resolver_diagnostics, "D2HAsyncNode"), nullptr);

    EXPECT_EQ(h2d_diagnostic->input_token_type, "SarControlToken");
    EXPECT_EQ(h2d_diagnostic->output_token_type, "SarControlToken");
    EXPECT_EQ(bp_diagnostic->input_token_type, "SarControlToken");
    EXPECT_EQ(bp_diagnostic->output_token_type, "SarControlToken");
    EXPECT_EQ(d2h_diagnostic->input_token_type, "SarControlToken");
    EXPECT_EQ(d2h_diagnostic->output_token_type, "SarControlToken");
    EXPECT_FALSE(h2d_diagnostic->fallback_used);
    EXPECT_FALSE(bp_diagnostic->fallback_used);
    EXPECT_FALSE(d2h_diagnostic->fallback_used);

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
                        .Build();

    ASSERT_NE(executor, nullptr);
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(graph_manager);
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(graph_manager->GetMetrics());
    const auto& status = sink->last_token();

    EXPECT_EQ(status.sidecar.sequence_id, 64u);
    EXPECT_EQ(status.sidecar.aperture_id, 64u);
    EXPECT_EQ(status.sidecar.pulse_range_start, 64u);
    EXPECT_EQ(status.sidecar.stream_id, 0u);
    EXPECT_EQ(status.sidecar.tile_count, 4u);
    EXPECT_EQ(status.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_TRUE(status.has_host_view);
    EXPECT_TRUE(status.has_transfer_ticket);
    EXPECT_TRUE(status.has_kernel_ticket);

    EXPECT_GT(status.transfer_ticket.completion_event, 0u);
    EXPECT_GT(status.kernel_ticket.completion_event, 0u);
    EXPECT_GT(status.sidecar.h2d_queue_id, 0u);
    EXPECT_GT(status.sidecar.kernel_queue_id, 0u);
    EXPECT_GT(status.sidecar.d2h_queue_id, 0u);

    EXPECT_NE(status.sidecar.sequence_id, status.transfer_ticket.completion_event);
    EXPECT_NE(status.sidecar.sequence_id, status.kernel_ticket.completion_event);
}

TEST(SarJsonRuntimeTest, DefinitiveRangeWindowDeviceTransformSubstitutionIsBlockedByTokenContract) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path sar_plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    const std::filesystem::path gpu_plugin_dir{GPU_PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(sar_plugin_dir));
    ASSERT_TRUE(std::filesystem::exists(gpu_plugin_dir));

    auto metal_config = LoadJsonFile(config_path);
    metal_config["execution_backend"] = "metal";
    metal_config["backend_fallback_policy"] = "strict";
    metal_config["resolver_mappings"].push_back(
        nlohmann::json{
            {"intent_type", "RangeWindowNode"},
            {"input_token_type", "SarPulseBlockMessage"},
            {"output_token_type", "SarPulseBlockMessage"},
            {"variants", nlohmann::json::array({
                nlohmann::json{{"backend", "metal"}, {"concrete_type", "DeviceTransformNodeMetal"}},
                nlohmann::json{{"backend", "stub"}, {"concrete_type", "RangeWindowNode"}},
            })},
        });

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_stripmap_definitive_range_window_transform_blocker.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{gpu_plugin_dir.string(), sar_plugin_dir.string()});
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(temp_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    EXPECT_FALSE(build_result.success);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST(SarJsonRuntimeTest, DefinitiveRangeCompressionDeviceKernelSubstitutionIsBlockedByTokenContract) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path sar_plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    const std::filesystem::path gpu_plugin_dir{GPU_PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(sar_plugin_dir));
    ASSERT_TRUE(std::filesystem::exists(gpu_plugin_dir));

    auto metal_config = LoadJsonFile(config_path);
    metal_config["execution_backend"] = "metal";
    metal_config["backend_fallback_policy"] = "strict";
    metal_config["resolver_mappings"].push_back(
        nlohmann::json{
            {"intent_type", "RangeCompressionNode"},
            {"input_token_type", "SarPulseBlockMessage"},
            {"output_token_type", "SarPulseBlockMessage"},
            {"variants", nlohmann::json::array({
                nlohmann::json{{"backend", "metal"}, {"concrete_type", "DeviceKernelNodeMetal"}},
                nlohmann::json{{"backend", "stub"}, {"concrete_type", "RangeCompressionNode"}},
            })},
        });

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_stripmap_definitive_range_compression_kernel_blocker.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{gpu_plugin_dir.string(), sar_plugin_dir.string()});
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(temp_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    EXPECT_FALSE(build_result.success);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}
