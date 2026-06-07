#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

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

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_JSON_CONFIG_PATH
#define SAR_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr1.json"
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
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 7U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 6U);

    const auto run_result = executor->Execute();
    EXPECT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
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
}
