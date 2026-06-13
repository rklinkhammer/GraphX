#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/D2HAsyncAccelNode.hpp"
#include "sar/H2DAsyncAccelNode.hpp"
#include "sar/ImageTileMergeNode.hpp"
#include "sar/RangeWindowNode.hpp"
#include "sar/SarBackprojectionTransformAccelNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SyntheticApertureIqSourceNode.hpp"

#include <chrono>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>

namespace {

sar::SarDiagnosticsSnapshot RunBaselinePipeline() {
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

    sar::SarBackprojectionTransformAccelConfig bp_cfg{};
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
    sar::RangeWindowNode window;
    sar::AzimuthTileSplitNode split(split_cfg);
    sar::H2DAsyncAccelNode h2d;
    sar::SarBackprojectionTransformAccelNode bp(bp_cfg);
    sar::D2HAsyncAccelNode d2h;
    sar::ImageTileMergeNode merge(merge_cfg);
    sar::SarDiagnosticsSinkNode sink;

    while (true) {
        auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
        if (!pulse.has_value()) {
            break;
        }

        auto windowed_pulse = window.Transfer(
            *pulse,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!windowed_pulse.has_value()) {
            ADD_FAILURE() << "Baseline range window stage returned null";
            return {};
        }

        auto range_tile = split.Transfer(
            *windowed_pulse,
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

sar::SarDiagnosticsSnapshot RunFanoutBaselinePipeline() {
    constexpr std::size_t kTileCount = 4;

    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 0;
    source_cfg.total_pulses = 32;
    source_cfg.samples_per_pulse = 256;
    source_cfg.backend_id = 0;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::ImageTileMergeConfig merge_cfg{};
    merge_cfg.expected_tiles = static_cast<std::uint32_t>(kTileCount);
    merge_cfg.require_watermark_before_complete = false;
    merge_cfg.require_all_tile_eos_before_complete = true;
    merge_cfg.backend_id = 0;
    merge_cfg.backend = sar::SarBackendKind::Host;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::RangeWindowNode window;
    std::array<sar::AzimuthTileSplitNode, kTileCount> split_nodes{};
    std::array<sar::H2DAsyncAccelNode, kTileCount> h2d_nodes{};
    std::array<sar::SarBackprojectionTransformAccelNode, kTileCount> bp_nodes{};
    std::array<sar::D2HAsyncAccelNode, kTileCount> d2h_nodes{};
    sar::ImageTileMergeNode merge(merge_cfg);
    sar::SarDiagnosticsSinkNode sink;

    for (std::size_t tile = 0; tile < kTileCount; ++tile) {
        sar::AzimuthTileSplitConfig split_cfg{};
        split_cfg.tile_count = static_cast<std::uint32_t>(kTileCount);
        split_cfg.fixed_tile_id = static_cast<int>(tile);
        split_cfg.backend_id = 0;
        split_cfg.backend = sar::SarBackendKind::Host;
        split_nodes[tile].SetConfig(split_cfg);

        sar::SarBackprojectionTransformAccelConfig bp_cfg{};
        bp_cfg.image_width = 16;
        bp_cfg.backend_id = 0;
        bp_cfg.queue_id = static_cast<std::uint32_t>(tile);
        bp_cfg.kernel_id = 3301;
        bp_cfg.backend = sar::SarBackendKind::SimulatedDevice;
        bp_nodes[tile].SetConfig(bp_cfg);
    }

    while (true) {
        auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
        if (!pulse.has_value()) {
            break;
        }

        auto windowed_pulse = window.Transfer(
            *pulse,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
        if (!windowed_pulse.has_value()) {
            ADD_FAILURE() << "Fanout baseline range window stage returned null";
            return {};
        }

        for (std::size_t tile = 0; tile < kTileCount; ++tile) {
            auto range_tile = split_nodes[tile].Transfer(
                *windowed_pulse,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!range_tile.has_value()) {
                ADD_FAILURE() << "Fanout baseline split stage returned null";
                return {};
            }

            auto h2d_tile = h2d_nodes[tile].Transfer(
                *range_tile,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!h2d_tile.has_value()) {
                ADD_FAILURE() << "Fanout baseline h2d stage returned null";
                return {};
            }

            auto image_tile = bp_nodes[tile].Transfer(
                *h2d_tile,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!image_tile.has_value()) {
                ADD_FAILURE() << "Fanout baseline backprojection stage returned null";
                return {};
            }

            auto d2h_tile = d2h_nodes[tile].Transfer(
                *image_tile,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!d2h_tile.has_value()) {
                ADD_FAILURE() << "Fanout baseline d2h stage returned null";
                return {};
            }

            auto status = merge.Transfer(
                *d2h_tile,
                std::integral_constant<std::size_t, 0>{},
                std::integral_constant<std::size_t, 0>{});
            if (!status.has_value()) {
                continue;
            }

            if (!sink.Consume(*status, std::integral_constant<std::size_t, 0>{})) {
                ADD_FAILURE() << "Fanout baseline diagnostics sink rejected status";
                return {};
            }
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

#ifndef SAR_PR2_FANOUT_JSON_CONFIG_PATH
#define SAR_PR2_FANOUT_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_pr2_fanout.json"
#endif

TEST(SarBaselineCompareTest, GraphPipelineMatchesDirectBaselineWithinTolerance) {
    const std::filesystem::path config_path{SAR_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
                        .Build();

    ASSERT_NE(executor, nullptr);
    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto graph_sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
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

TEST(SarBaselineCompareTest, FanoutGraphPipelineMatchesDirectBaselineWithinTolerance) {
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
    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto graph_sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(graph_sink, nullptr);

    const auto baseline = RunFanoutBaselinePipeline();
    const auto graph_diag = graph_sink->last_diagnostics();

    EXPECT_EQ(graph_diag.pulses_processed, baseline.pulses_processed);
    EXPECT_EQ(graph_diag.tiles_processed, baseline.tiles_processed);
    EXPECT_EQ(graph_diag.bytes_h2d, baseline.bytes_h2d);
    EXPECT_EQ(graph_diag.bytes_d2h, baseline.bytes_d2h);
    EXPECT_EQ(graph_diag.kernel_dispatches, baseline.kernel_dispatches);
    EXPECT_EQ(graph_diag.duplicate_tile_count, baseline.duplicate_tile_count);
    EXPECT_EQ(graph_diag.missing_tile_count, baseline.missing_tile_count);
}
