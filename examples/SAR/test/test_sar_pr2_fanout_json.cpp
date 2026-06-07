#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarPulseFanoutNode.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_PR2_FANOUT_JSON_CONFIG_PATH
#define SAR_PR2_FANOUT_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr2_fanout.json"
#endif

std::string SarPulseFanoutPluginFilename() {
    return std::string("libsar_pulse_fanout_node") + kSharedLibraryExtension;
}

std::shared_ptr<sar::SarDiagnosticsSinkNode> ResolveDiagnosticsSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    const auto nodes = graph_manager->GetNodes();
    for (const auto& node : nodes) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper || wrapper->GetType() != "SarDiagnosticsSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarDiagnosticsSinkNode>();
    }

    return nullptr;
}

} // namespace

TEST(SarPr2FanoutJsonTest, SarPulseFanoutNodeLoadsDynamically) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(SarPulseFanoutPluginFilename()));

    auto created = registry->CreateNodeExpected("SarPulseFanoutNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::SarPulseFanoutNode>();
    ASSERT_TRUE(node);

    EXPECT_EQ(node->GetInputPortCount(), 1);
    EXPECT_EQ(node->GetOutputPortCount(), 4);
}

TEST(SarPr2FanoutJsonTest, ExecutesGraphVisibleFanoutTopology) {
    const std::filesystem::path config_path{SAR_PR2_FANOUT_JSON_CONFIG_PATH};
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
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 21U);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 23U);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& status = sink->last_status();
    EXPECT_EQ(status.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_TRUE(status.complete);

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, 131072u);
    EXPECT_EQ(diagnostics.bytes_d2h, 131072u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 128u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 124u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
}
