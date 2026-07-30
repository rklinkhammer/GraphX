// SPDX-License-Identifier: MIT

/**
 * @file test_gotcha_dataset_adapter.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"
#include "config/ConfigError.hpp"
#include "sar/GotchaReplaySourceNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
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

std::filesystem::path WriteTempTopologyFile(const nlohmann::json& topology) {
    const auto path = std::filesystem::temp_directory_path() / "gotcha_replay_metal_topology.json";
    std::ofstream output(path);
    output << topology.dump(2) << '\n';
    return path;
}

std::filesystem::path WriteExternalFixtureFile() {
    const auto fixture_path = std::filesystem::temp_directory_path() / "gotcha_external_manual_fixture.json";
    const nlohmann::json fixture{
        {"schema", "graphx.sar.gotcha.normalized.v1"},
        {"records", nlohmann::json::array({
            {
                {"frame_id", 1},
                {"pass_id", 77},
                {"pulse_block_id", 101},
                {"range_bin_start", 0},
                {"range_bin_count", 2},
                {"aperture_span_start", 0},
                {"aperture_span_count", 1},
                {"timestamp_us", 12345},
                {"ordering_key", 1},
                {"stream_id", 3},
                {"backend_id", 0},
                {"backend", 0},
                {"iq_samples", nlohmann::json::array({
                    nlohmann::json{{"real", 1.0f}, {"imag", 0.0f}},
                    nlohmann::json{{"real", 0.5f}, {"imag", -0.25f}}
                })}
            }
        })}
    };

    std::ofstream out(fixture_path);
    out << fixture.dump(2) << '\n';
    return fixture_path;
}

class ScopedEnvVar {
public:
    explicit ScopedEnvVar(const std::string& value) {
        const char* current = std::getenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
        had_original_ = current != nullptr;
        if (had_original_) {
            original_ = current;
        }
#ifdef _WIN32
        _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str());
#else
        setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
        if (had_original_) {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str());
#else
            setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", "");
#else
            unsetenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
#endif
        }
    }

private:
    bool had_original_{false};
    std::string original_{};
};

nlohmann::json MakeGotchaPr3Topology() {
    return nlohmann::json{
        {"name", "sar_gotcha_replay_metal"},
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
                {"type", "H2DAsyncAccelNode"},
                {"node_config", {
                    {"override_backend", false},
                    {"backend_id", 0}
                }}
            },
            {
                {"id", "bp"},
                {"type", "SarBackprojectionTransformAccelNode"},
                {"node_config", {
                    {"image_width", 16},
                    {"backend_id", 0},
                    {"queue_id", 0},
                    {"kernel_id", 3301}
                }}
            },
            {
                {"id", "d2h"},
                {"type", "D2HAsyncAccelNode"},
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
    EXPECT_EQ(first->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(first->sidecar.sequence_id, 0u);
    EXPECT_EQ(first->sidecar.batch_id, 17u);
    EXPECT_EQ(first->sidecar.aperture_id, 9000u);
    EXPECT_EQ(first->sidecar.pulse_range_start, 0u);
    EXPECT_EQ(first->sidecar.pulse_range_count, 8u);
    EXPECT_EQ(first->sidecar.stream_id, 5u);
    EXPECT_EQ(first->sidecar.tile_id, 0u);
    EXPECT_EQ(first->sidecar.tile_count, 4u);
    EXPECT_FALSE(first->sidecar.synthetic);
    EXPECT_EQ(first->sidecar.payload_byte_count, 8u * sizeof(float));
    ASSERT_TRUE(first->has_host_view);

    for (int i = 0; i < 3; ++i) {
        auto out = node.Produce(std::integral_constant<std::size_t, 0>{});
        ASSERT_TRUE(out.has_value());
        EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    }

    auto eos = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    EXPECT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos->sidecar.sequence_id, 4u);
    EXPECT_EQ(eos->sidecar.payload_byte_count, 0u);

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
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    ASSERT_NE(executor->GetGraphManager(), nullptr);
    EXPECT_EQ(executor->GetGraphManager()->GetNodes().size(), 8u);
    EXPECT_EQ(executor->GetGraphManager()->GetEdges().size(), 7u);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    EXPECT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& diagnostics = sink->last_diagnostics();
    EXPECT_EQ(diagnostics.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(diagnostics.sidecar.sequence_id, 4u);
    EXPECT_EQ(diagnostics.sidecar.batch_id, 17u);
    EXPECT_EQ(diagnostics.sidecar.aperture_id, 4u);
    EXPECT_EQ(diagnostics.sidecar.stream_id, 5u);
    EXPECT_LE(diagnostics.sidecar.backend_id, 2u);
    EXPECT_EQ(diagnostics.sidecar.synthetic, false);
    EXPECT_EQ(diagnostics.sidecar.payload_byte_count, 0u);
    EXPECT_EQ(diagnostics.pulses_processed, 4u);
    EXPECT_EQ(diagnostics.tiles_processed, 4u);
    EXPECT_EQ(diagnostics.bytes_h2d, 128u);
    EXPECT_EQ(diagnostics.bytes_d2h, 128u);
    EXPECT_EQ(diagnostics.kernel_dispatches, 4u);
    EXPECT_EQ(diagnostics.duplicate_tile_count, 0u);
    EXPECT_EQ(diagnostics.missing_tile_count, 0u);
    EXPECT_EQ(diagnostics.out_of_order_completion_count, 0u);
}

TEST(GotchaDatasetAdapterTest, RejectsExternalFixtureWithoutExplicitOptIn) {
    const auto fixture_path = WriteExternalFixtureFile();
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    sar::GotchaReplaySourceNode node;
    nlohmann::json cfg_json{
        {"fixture_path", fixture_path.string()},
        {"emit_watermark", false},
        {"allow_external_fixture", false}
    };

    EXPECT_THROW(node.Configure(graph::JsonView(cfg_json)), graph::ConfigError);
}

TEST(GotchaDatasetAdapterTest, RejectsExternalFixtureWithoutEnvironmentGate) {
    const auto fixture_path = WriteExternalFixtureFile();
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    sar::GotchaReplaySourceNode node;
    nlohmann::json cfg_json{
        {"fixture_path", fixture_path.string()},
        {"emit_watermark", false},
        {"allow_external_fixture", true}
    };

    EXPECT_THROW(node.Configure(graph::JsonView(cfg_json)), graph::ConfigError);
}

TEST(GotchaDatasetAdapterTest, AllowsExternalFixtureWithExplicitLocalManualOptIn) {
    const auto fixture_path = WriteExternalFixtureFile();
    ASSERT_TRUE(std::filesystem::exists(fixture_path));

    ScopedEnvVar env_guard("1");

    sar::GotchaReplaySourceNode node;
    nlohmann::json cfg_json{
        {"fixture_path", fixture_path.string()},
        {"emit_watermark", false},
        {"allow_external_fixture", true}
    };

    EXPECT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));

    auto out = node.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->sidecar.marker, sar::SarFrameMarker::Data);
    EXPECT_EQ(out->sidecar.synthetic, false);
}
