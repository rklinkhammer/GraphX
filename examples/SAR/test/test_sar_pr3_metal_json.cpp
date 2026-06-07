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

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
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

TEST(SarPr3MetalJsonTest, ExecutesMetalWindowPipeline) {
    ValidateMetalSarConfig(std::filesystem::path{SAR_PR3_METAL_WINDOW_JSON_CONFIG_PATH});
}

TEST(SarPr3MetalJsonTest, ExecutesMetalCompressionPipeline) {
    ValidateMetalSarConfig(std::filesystem::path{SAR_PR3_METAL_COMPRESSION_JSON_CONFIG_PATH});
}
