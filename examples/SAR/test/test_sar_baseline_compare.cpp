#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncNode.hpp"
#include "sar/H2DAsyncNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/SarBackprojectionTransformNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SyntheticApertureIqSourceNode.hpp"

#include <chrono>
#include <cstddef>
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

sar::SarDiagnosticsMessage RunBaselinePipeline() {
    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 0;
    source_cfg.total_pulses = 32;
    source_cfg.samples_per_pulse = 256;
    source_cfg.backend_id = 0;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = 4;
    split_cfg.backend_id = 0;
    split_cfg.backend = sar::SarBackendKind::Host;

    sar::SarBackprojectionTransformConfig bp_cfg{};
    bp_cfg.image_width = 16;
    bp_cfg.backend_id = 0;
    bp_cfg.queue_id = 0;
    bp_cfg.kernel_id = 3301;
    bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = 4;
    merge_cfg.require_watermark_before_complete = false;
    merge_cfg.backend_id = 0;
    merge_cfg.backend = sar::SarBackendKind::Host;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::AzimuthTileSplitNode split(split_cfg);
    sar::H2DAsyncNode h2d;
    sar::SarBackprojectionTransformNode bp(bp_cfg);
    sar::D2HAsyncNode d2h;
    sar::ImageTileMergeNode merge(merge_cfg);
    sar::SarDiagnosticsSinkNode sink;

    while (true) {
        auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
        if (!pulse.has_value()) {
            break;
        }

        auto range_tile = split.Transfer(
            *pulse,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!range_tile.has_value()) {
            ADD_FAILURE() << "Baseline split stage returned null";
            return {};
        }

        auto h2d_tile = h2d.Transfer(
            *range_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!h2d_tile.has_value()) {
            ADD_FAILURE() << "Baseline h2d stage returned null";
            return {};
        }

        auto image_tile = bp.Transfer(
            *h2d_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!image_tile.has_value()) {
            ADD_FAILURE() << "Baseline backprojection stage returned null";
            return {};
        }

        auto d2h_tile = d2h.Transfer(
            *image_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!d2h_tile.has_value()) {
            ADD_FAILURE() << "Baseline d2h stage returned null";
            return {};
        }

        auto status = merge.Transfer(
            *d2h_tile,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!status.has_value()) {
            ADD_FAILURE() << "Baseline merge stage returned null";
            return {};
        }

        if (!sink.Consume(*status, std::integral_constant<std::size_t, 0>{})) {
            ADD_FAILURE() << "Baseline diagnostics sink rejected status";
            return {};
        }
    }

    return sink.last_diagnostics();
}

} // namespace

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_JSON_CONFIG_PATH
#define SAR_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr1.json"
#endif

TEST(SarBaselineCompareTest, GraphPipelineMatchesDirectBaselineWithinTolerance) {
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
    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto graph_sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(graph_sink, nullptr);

    const auto baseline = RunBaselinePipeline();
    const auto graph_diag = graph_sink->last_diagnostics();

    EXPECT_EQ(graph_diag.pulses_processed, baseline.pulses_processed);
    EXPECT_EQ(graph_diag.tiles_processed, baseline.tiles_processed);
    EXPECT_EQ(graph_diag.bytes_h2d, baseline.bytes_h2d);
    EXPECT_EQ(graph_diag.bytes_d2h, baseline.bytes_d2h);
    EXPECT_EQ(graph_diag.kernel_dispatches, baseline.kernel_dispatches);
    EXPECT_EQ(graph_diag.fanin_wait_ms, baseline.fanin_wait_ms);
    EXPECT_EQ(graph_diag.e2e_latency_ms, baseline.e2e_latency_ms);
    EXPECT_EQ(graph_diag.duplicate_tile_count, baseline.duplicate_tile_count);
    EXPECT_EQ(graph_diag.missing_tile_count, baseline.missing_tile_count);
}
