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

    const auto& status = sink->last_status();
    EXPECT_TRUE(
        status.envelope.marker == sar::SarFrameMarker::EndOfStream ||
        status.envelope.marker == sar::SarFrameMarker::Data);
    EXPECT_GE(status.envelope.sequence_id, 30u);
    EXPECT_LE(status.envelope.sequence_id, 32u);
    EXPECT_EQ(status.envelope.batch_id, 0u);
    EXPECT_EQ(status.envelope.aperture_id, status.envelope.sequence_id);
    EXPECT_EQ(status.envelope.pulse_range_start, status.envelope.sequence_id);
    EXPECT_LE(status.envelope.pulse_range_count, 1u);
    EXPECT_EQ(status.envelope.stream_id, 0u);
    EXPECT_LT(status.envelope.tile_id, 4u);
    EXPECT_EQ(status.envelope.tile_count, 4u);
    EXPECT_EQ(status.envelope.backend_id, 0u);
    EXPECT_EQ(status.complete, status.envelope.marker == sar::SarFrameMarker::EndOfStream);
    EXPECT_TRUE(status.gpu.has_host_view);
    EXPECT_TRUE(status.gpu.has_transfer_ticket);
    EXPECT_EQ(status.gpu.transfer_ticket.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_GT(status.gpu.transfer_ticket.execution_queue_id, 0u);
    if (status.gpu.has_kernel_ticket) {
        EXPECT_EQ(status.gpu.kernel_ticket.backend, graph::gpu::accel::BackendKind::Metal);
        EXPECT_GT(status.gpu.kernel_ticket.execution_queue_id, 0u);
    }

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_TRUE(
        diagnostics.envelope.marker == sar::SarFrameMarker::EndOfStream ||
        diagnostics.envelope.marker == sar::SarFrameMarker::Data);
    EXPECT_GE(diagnostics.pulses_processed, 31u);
    EXPECT_LE(diagnostics.pulses_processed, 32u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, diagnostics.bytes_d2h);
    EXPECT_GE(diagnostics.bytes_h2d, 130048u);
    EXPECT_LE(diagnostics.bytes_h2d, 131072u);
    EXPECT_GE(diagnostics.kernel_dispatches, 127u);
    EXPECT_LE(diagnostics.kernel_dispatches, 128u);
    EXPECT_GE(diagnostics.duplicate_tile_count, 120u);
    EXPECT_LE(diagnostics.duplicate_tile_count, 124u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_GE(diagnostics.out_of_order_completion_count, 0u);
    EXPECT_LE(diagnostics.out_of_order_completion_count, 3u);
}
