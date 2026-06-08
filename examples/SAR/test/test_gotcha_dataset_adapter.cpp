#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"
#include "sar/GotchaReplaySourceNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <utility>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_GOTCHA_REPLAY_FIXTURE_PATH
#define SAR_GOTCHA_REPLAY_FIXTURE_PATH "examples/SAR/test/fixtures/gotcha_replay_fixture.json"
#endif

std::string GotchaReplaySourcePluginFilename() {
    return std::string("libgotcha_replay_source_node") + kSharedLibraryExtension;
}

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

std::filesystem::path WriteTempTopologyFile(const nlohmann::json& topology) {
    const auto path = std::filesystem::temp_directory_path() / "gotcha_replay_pr3_metal_topology.json";
    std::ofstream output(path);
    output << topology.dump(2) << '\n';
    return path;
}

nlohmann::json MakeGotchaPr3Topology() {
    return nlohmann::json{
        {"name", "sar_gotcha_replay_pr3_metal"},
        {"execution_backend", "metal"},
        {"backend_fallback_policy", "allow_fallback"},
        {"resolver_diagnostics", true},
        {"edge_contract", "accel-token"},
        {"num_threads", 4},
        {"nodes", nlohmann::json::array({
            {
                {"id", "src"},
                {"type", "GotchaReplaySourceNode"},
                {"node_config", {
                    {"fixture_path", SAR_GOTCHA_REPLAY_FIXTURE_PATH},
                    {"emit_watermark", false}
                }}
            },
            {
                {"id", "compression"},
                {"type", "RangeCompressionNode"},
                {"node_config", {
                    {"enabled", true},
                    {"gain", 1.0},
                    {"sample_rate_hz", 48000.0}
                }}
            },
            {
                {"id", "split"},
                {"type", "AzimuthTileSplitNode"},
                {"node_config", {
                    {"tile_count", 4},
                    {"tile_id_offset", 0},
                    {"backend_id", 0}
                }}
            },
            {
                {"id", "h2d"},
                {"type", "H2DAsyncNode"},
                {"node_config", {
                    {"override_backend", false},
                    {"backend_id", 0}
                }}
            },
            {
                {"id", "bp"},
                {"type", "SarBackprojectionTransformNode"},
                {"node_config", {
                    {"image_width", 16},
                    {"backend_id", 0},
                    {"queue_id", 0},
                    {"kernel_id", 3301}
                }}
            },
            {
                {"id", "d2h"},
                {"type", "D2HAsyncNode"},
                {"node_config", {
                    {"override_backend", false},
                    {"backend_id", 0}
                }}
            },
            {
                {"id", "merge"},
                {"type", "ImageTileMergeNode"},
                {"node_config", {
                    {"expected_tiles", 4},
                    {"require_watermark_before_complete", false},
                    {"backend_id", 0},
                    {"backend", 1}
                }}
            },
            {
                {"id", "sink"},
                {"type", "SarDiagnosticsSinkNode"},
                {"node_config", {
                    {"completion_signal_enabled", true}
                }}
            }
        })},
        {"edges", nlohmann::json::array({
            {{"source_node_id", "src"}, {"source_port", 0}, {"target_node_id", "compression"}, {"target_port", 0}},
            {{"source_node_id", "compression"}, {"source_port", 0}, {"target_node_id", "split"}, {"target_port", 0}},
            {{"source_node_id", "split"}, {"source_port", 0}, {"target_node_id", "h2d"}, {"target_port", 0}},
            {{"source_node_id", "h2d"}, {"source_port", 0}, {"target_node_id", "bp"}, {"target_port", 0}},
            {{"source_node_id", "bp"}, {"source_port", 0}, {"target_node_id", "d2h"}, {"target_port", 0}},
            {{"source_node_id", "d2h"}, {"source_port", 0}, {"target_node_id", "merge"}, {"target_port", 0}},
            {{"source_node_id", "merge"}, {"source_port", 0}, {"target_node_id", "sink"}, {"target_port", 0}}
        })}
    };
}

} // namespace

TEST(GotchaDatasetAdapterTest, OfflineConverterLoadsNormalizedFixture) {
    sar::GotchaOfflineConverter converter;
    const auto fixture_path = std::filesystem::path{SAR_GOTCHA_REPLAY_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    const auto records = converter.LoadFromFile(fixture_path);
    ASSERT_EQ(records.size(), 4u);

    EXPECT_EQ(records.front().frame_id, 0u);
    EXPECT_EQ(records.front().pass_id, 17u);
    EXPECT_EQ(records.front().pulse_block_id, 9000u);
    EXPECT_EQ(records.front().range_bin_count, 8u);
    EXPECT_EQ(records.front().aperture_span_count, 4u);
    EXPECT_EQ(records.front().ordering_key, 0u);
    ASSERT_EQ(records.front().iq_samples.size(), 8u);
    EXPECT_FLOAT_EQ(records.front().iq_samples.front().real(), 1.0f);
    EXPECT_FLOAT_EQ(records.front().iq_samples.front().imag(), 0.0f);
}

TEST(GotchaDatasetAdapterTest, ReplaySourceEmitsDeterministicPulseBlocksThenEos) {
    sar::GotchaOfflineConverter converter;
    auto records = converter.LoadFromFile(std::filesystem::path{SAR_GOTCHA_REPLAY_FIXTURE_PATH});

    sar::GotchaReplaySourceConfig config{};
    config.records = std::move(records);
    config.emit_watermark = false;
    sar::GotchaReplaySourceNode node(config);

    auto first = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->envelope.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->envelope.sequence_id, 0u);
    EXPECT_EQ(first->envelope.batch_id, 17u);
    EXPECT_EQ(first->envelope.aperture_id, 9000u);
    EXPECT_EQ(first->envelope.pulse_range_start, 0u);
    EXPECT_EQ(first->envelope.pulse_range_count, 8u);
    EXPECT_EQ(first->envelope.stream_id, 5u);
    EXPECT_EQ(first->envelope.tile_id, 0u);
    EXPECT_EQ(first->envelope.tile_count, 4u);
    EXPECT_FALSE(first->envelope.synthetic);
    EXPECT_EQ(first->buffer.byte_count, 8u * sizeof(sar::SarIqSample));
    ASSERT_EQ(first->iq_samples.size(), 8u);

    for (int i = 0; i < 3; ++i) {
        auto out = node.Produce(std::integral_constant<std::size_t, 0>{});
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::Data);
    }

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos->envelope.sequence_id, 4u);
    EXPECT_EQ(eos->buffer.byte_count, 0u);

    auto after = node.Produce(std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(after.has_value());
}

TEST(GotchaDatasetAdapterTest, PluginLoadedGotchaReplayPipelineRunsEndToEnd) {
    const auto fixture_path = std::filesystem::path{SAR_GOTCHA_REPLAY_FIXTURE_PATH};
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);
    ASSERT_TRUE(loader.LoadPluginSafe(GotchaReplaySourcePluginFilename()));

    const auto topology_path = WriteTempTopologyFile(MakeGotchaPr3Topology());
    ASSERT_TRUE(std::filesystem::exists(topology_path));

    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(topology_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 8u);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 7u);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.pulses_processed, 4u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, 128u);
    EXPECT_EQ(diagnostics.bytes_d2h, 128u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 4u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 0u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_EQ(diagnostics.out_of_order_completion_count, 0u);
}
