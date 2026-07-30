// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_input_source_node.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "sar/OrderedCrsdSetInputSourceNode.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"
#include "sar/io/CrsdReader.hpp"

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_CRSD_TINY_FIXTURE_BASE_DIR
#define SAR_CRSD_TINY_FIXTURE_BASE_DIR "examples/SAR/test/fixtures/crsd_binary_tiny_multisegment"
#endif

#ifndef SAR_CRSD_TINY_CONFIG_PATHS_JSON
#define SAR_CRSD_TINY_CONFIG_PATHS_JSON "examples/SAR/config/sar_crsd_tiny_fixture_set_input.json"
#endif

#ifndef SAR_CRSD_TINY_CONFIG_DIRECTORY_JSON
#define SAR_CRSD_TINY_CONFIG_DIRECTORY_JSON "examples/SAR/config/sar_crsd_tiny_fixture_set_input_directory.json"
#endif

#ifndef SAR_CRSD_TINY_CONFIG_MANIFEST_JSON
#define SAR_CRSD_TINY_CONFIG_MANIFEST_JSON "examples/SAR/config/sar_crsd_tiny_fixture_set_input_manifest.json"
#endif

std::string CrsdInputSourcePluginFilename() {
    return std::string("libcrsd_input_source_node") + kSharedLibraryExtension;
}

std::filesystem::path FixtureBaseDir() {
    return std::filesystem::path{SAR_CRSD_TINY_FIXTURE_BASE_DIR};
}

std::filesystem::path SegmentPath(const std::string& segment_name) {
    return FixtureBaseDir() / segment_name / "product.crsd";
}

std::filesystem::path WriteTempTopologyFile(const nlohmann::json& topology) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("graphx_crsd_input_source_topology_" + std::to_string(now) + ".json");
    std::ofstream output(path);
    output << topology.dump(2) << '\n';
    return path;
}

nlohmann::json LoadJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "unable to open json file: " << path;
    nlohmann::json json;
    in >> json;
    return json;
}

nlohmann::json BuildTopologyForMode(const std::string& mode) {
    const auto fixture_base = FixtureBaseDir();
    nlohmann::json node_config{
        {"stream_id", 21},
        {"backend_id", 0},
        {"backend", 0},
    };

    if (mode == "paths") {
        node_config["crsd_paths"] = nlohmann::json::array({
            (fixture_base / "segment_000" / "product.crsd").string(),
            (fixture_base / "segment_001" / "product.crsd").string(),
            (fixture_base / "segment_002" / "product.crsd").string(),
        });
    } else if (mode == "directory") {
        node_config["crsd_directory"] =
            (fixture_base.parent_path() / "crsd_binary_tiny_multisegment_directory").string();
    } else {
        node_config["manifest_path"] = (fixture_base / "manifest.json").string();
    }

    return nlohmann::json{
        {"name", "sar_crsd_input_smoke_" + mode},
        {"execution_backend", "auto"},
        {"backend_fallback_policy", "allow_fallback"},
        {"resolver_diagnostics", true},
        {"edge_contract", "accel-token"},
        {"num_threads", 2},
        {"nodes", nlohmann::json::array({
            {
                {"id", "src"},
                {"type", "OrderedCrsdSetInputSourceNode"},
                {"node_config", node_config}
            },
            {
                {"id", "sink"},
                {"type", "SarDiagnosticsSinkNode"},
                {"node_config", {{"completion_signal_enabled", true}}}
            }
        })},
        {"edges", nlohmann::json::array({
            {
                {"source_node_id", "src"},
                {"source_port", 0},
                {"target_node_id", "sink"},
                {"target_port", 0}
            }
        })}
    };
}

} // namespace

TEST(CrsdReaderTest, BinaryPathsModeParsesMetadataSignalPvpGeometryAndHashes) {
    graphx::sar::CrsdReader reader;
    graphx::sar::CrsdReadOptions options{};
    options.ordered_crsd_paths = {
        SegmentPath("segment_000"),
        SegmentPath("segment_001"),
        SegmentPath("segment_002"),
    };

    const auto result = reader.ReadOrderedSet(options);
    ASSERT_TRUE(result.success) << result.diagnostic;
    ASSERT_EQ(result.value.segments.size(), 3u);
    EXPECT_EQ(result.value.total_vector_count, 6u);
    EXPECT_NE(result.value.ordered_set_payload_hash, 0u);

    const auto& first_segment = result.value.segments.front();
    const auto& last_segment = result.value.segments.back();

    EXPECT_EQ(first_segment.segment_index, 0u);
    EXPECT_EQ(first_segment.global_vector_start, 0u);
    EXPECT_EQ(first_segment.vector_count, 2u);
    EXPECT_EQ(first_segment.samples_per_vector, 4u);
    EXPECT_NE(first_segment.payload_hash, 0u);
    EXPECT_NE(first_segment.first_vector_hash, 0u);
    EXPECT_NE(first_segment.last_vector_hash, 0u);

    EXPECT_EQ(first_segment.first_vector.vector_index, 0u);
    EXPECT_NEAR(first_segment.first_vector.rcv_time_s, 0.0, 1e-12);
    EXPECT_NEAR(first_segment.first_vector.platform_position_m[0], 1000.0, 1e-6);
    EXPECT_NEAR(first_segment.first_vector.platform_velocity_mps[2], 30.0, 1e-6);

    EXPECT_EQ(last_segment.last_vector.vector_index, 1u);
    EXPECT_NEAR(last_segment.last_vector.rcv_time_s, 0.5, 1e-12);
    EXPECT_NEAR(last_segment.last_vector.platform_position_m[1], 2005.0, 1e-6);
    EXPECT_NEAR(last_segment.last_vector.platform_velocity_mps[0], 15.0, 1e-6);
}

TEST(CrsdReaderTest, DirectoryAndManifestModesResolveBinaryOrderedSetDeterministically) {
    graphx::sar::CrsdReader reader;

    graphx::sar::CrsdReadOptions directory_options{};
    directory_options.crsd_directory = FixtureBaseDir().parent_path() / "crsd_binary_tiny_multisegment_directory";

    graphx::sar::CrsdReadOptions manifest_options{};
    manifest_options.manifest_path = FixtureBaseDir() / "manifest.json";

    const auto directory_result = reader.ReadOrderedSet(directory_options);
    const auto manifest_result = reader.ReadOrderedSet(manifest_options);

    ASSERT_TRUE(directory_result.success) << directory_result.diagnostic;
    ASSERT_TRUE(manifest_result.success) << manifest_result.diagnostic;

    EXPECT_EQ(directory_result.value.total_vector_count, manifest_result.value.total_vector_count);
    EXPECT_EQ(directory_result.value.segments.size(), manifest_result.value.segments.size());
    EXPECT_EQ(directory_result.value.ordered_set_payload_hash, manifest_result.value.ordered_set_payload_hash);
}

TEST(CrsdReaderTest, SidecarJsonFilesRemainOptionalAndNonAuthoritativeForBinaryCrsd) {
    graphx::sar::CrsdReader reader;
    graphx::sar::CrsdReadOptions options{};
    options.ordered_crsd_paths = {
        SegmentPath("segment_000"),
        SegmentPath("segment_001"),
        SegmentPath("segment_002"),
    };

    ASSERT_FALSE(std::filesystem::exists(FixtureBaseDir() / "segment_000" / "metadata.json"));
    ASSERT_FALSE(std::filesystem::exists(FixtureBaseDir() / "segment_002" / "pvp.json"));

    const auto result = reader.ReadOrderedSet(options);
    ASSERT_TRUE(result.success) << result.diagnostic;
    EXPECT_EQ(result.value.total_vector_count, 6u);
    EXPECT_NE(result.value.ordered_set_payload_hash, 0u);
}

TEST(CrsdReaderTest, DeterministicDiagnosticsCoverInvalidAndOrderingCases) {
    graphx::sar::CrsdReader reader;

    graphx::sar::CrsdReadOptions duplicate_options{};
    duplicate_options.ordered_crsd_paths = {
        SegmentPath("segment_000"),
        SegmentPath("segment_000"),
    };
    const auto duplicate = reader.ReadOrderedSet(duplicate_options);
    ASSERT_FALSE(duplicate.success);
    EXPECT_EQ(duplicate.diagnostic, "duplicate_segment_index:0");

    graphx::sar::CrsdReadOptions missing_options{};
    missing_options.ordered_crsd_paths = {
        SegmentPath("segment_000"),
        SegmentPath("segment_002"),
    };
    const auto missing = reader.ReadOrderedSet(missing_options);
    ASSERT_FALSE(missing.success);
    EXPECT_EQ(missing.diagnostic, "missing_segment_index:1");

    graphx::sar::CrsdReadOptions out_of_order_options{};
    out_of_order_options.ordered_crsd_paths = {
        SegmentPath("segment_001"),
    };
    const auto out_of_order = reader.ReadOrderedSet(out_of_order_options);
    ASSERT_FALSE(out_of_order.success);
    EXPECT_EQ(out_of_order.diagnostic, "out_of_order_segment_index:1");

    graphx::sar::CrsdReadOptions non_crsd_options{};
    non_crsd_options.ordered_crsd_paths = {
        FixtureBaseDir() / "bad" / "non_crsd" / "product.crsd",
    };
    const auto non_crsd = reader.ReadOrderedSet(non_crsd_options);
    ASSERT_FALSE(non_crsd.success);
    EXPECT_TRUE(non_crsd.diagnostic.rfind("unsupported_non_crsd_file:", 0) == 0u);

    graphx::sar::CrsdReadOptions truncated_options{};
    truncated_options.ordered_crsd_paths = {
        FixtureBaseDir() / "bad" / "truncated" / "product.crsd",
    };
    const auto truncated = reader.ReadOrderedSet(truncated_options);
    ASSERT_FALSE(truncated.success);
    EXPECT_TRUE(truncated.diagnostic.rfind("malformed_crsd:", 0) == 0u);

    graphx::sar::CrsdReadOptions missing_pvp_options{};
    missing_pvp_options.ordered_crsd_paths = {
        FixtureBaseDir() / "bad" / "missing_pvp" / "product.crsd",
    };
    const auto missing_pvp = reader.ReadOrderedSet(missing_pvp_options);
    ASSERT_FALSE(missing_pvp.success);
    EXPECT_TRUE(
        missing_pvp.diagnostic.rfind("missing_required_pvp:", 0) == 0u ||
        missing_pvp.diagnostic.rfind("missing_required_metadata:", 0) == 0u);

    const auto empty_dir = std::filesystem::temp_directory_path() / "graphx_crsd_reader_empty";
    std::filesystem::create_directories(empty_dir);
    graphx::sar::CrsdReadOptions empty_options{};
    empty_options.crsd_directory = empty_dir;
    const auto empty = reader.ReadOrderedSet(empty_options);
    ASSERT_FALSE(empty.success);
    EXPECT_EQ(empty.diagnostic, "missing_product_crsd");
}

TEST(OrderedCrsdSetInputSourceNodeTest, EmitsOneOrderedApertureSetStreamThenEos) {
    sar::OrderedCrsdSetInputSourceNode node;

    const nlohmann::json cfg_json{
        {"crsd_paths", nlohmann::json::array({
            SegmentPath("segment_000").string(),
            SegmentPath("segment_001").string(),
            SegmentPath("segment_002").string(),
        })},
        {"stream_id", 7},
        {"backend_id", 0},
        {"backend", 0},
    };

    ASSERT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));
    const auto& read_result = node.GetLastReadResult();
    ASSERT_TRUE(read_result.success);
    ASSERT_EQ(read_result.value.segments.size(), 3u);

    std::vector<sar::SarControlToken> tokens;
    while (true) {
        auto token = node.Produce(std::integral_constant<std::size_t, 0>{});
        if (!token.has_value()) {
            break;
        }
        tokens.push_back(*token);
        if (token->sidecar.marker == sar::SarFrameMarker::EndOfStream) {
            break;
        }
    }

    ASSERT_EQ(tokens.size(), 4u);
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& token = tokens[i];
        EXPECT_EQ(token.sidecar.marker, sar::SarFrameMarker::Data);
        EXPECT_EQ(token.sidecar.sequence_id, i);
        EXPECT_EQ(token.sidecar.batch_id, 7u);
        EXPECT_EQ(token.sidecar.aperture_id, i);
        EXPECT_EQ(token.sidecar.pulse_range_start, i * 2u);
        EXPECT_EQ(token.sidecar.stream_id, 7u);
        EXPECT_EQ(token.sidecar.tile_id, i);
        EXPECT_EQ(token.sidecar.tile_count, 3u);
        EXPECT_EQ(token.sidecar.backend_id, 0u);
        EXPECT_EQ(token.sidecar.backend, sar::SarBackendKind::Host);
        EXPECT_EQ(token.sidecar.pulse_range_count, 2u);
        EXPECT_GT(token.sidecar.payload_byte_count, 0u);
        EXPECT_FALSE(token.sidecar.synthetic);
        EXPECT_TRUE(token.has_host_view);
        EXPECT_NE(token.host_view.host_ptr, nullptr);
    }

    const auto& eos = tokens.back();
    EXPECT_EQ(eos.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos.sidecar.sequence_id, 3u);
    EXPECT_EQ(eos.sidecar.batch_id, 7u);
    EXPECT_EQ(eos.sidecar.aperture_id, 3u);
    EXPECT_EQ(eos.sidecar.pulse_range_start, 6u);
    EXPECT_EQ(eos.sidecar.pulse_range_count, 0u);
}

TEST(OrderedCrsdSetInputSourceNodeTest, AcceptsBinaryDirectoryAndManifestNodeConfigModes) {
    sar::OrderedCrsdSetInputSourceNode directory_node;
    const nlohmann::json directory_cfg{
        {"crsd_directory", (FixtureBaseDir().parent_path() / "crsd_binary_tiny_multisegment_directory").string()},
        {"stream_id", 9},
        {"backend_id", 0},
        {"backend", 0},
    };
    ASSERT_NO_THROW(directory_node.Configure(graph::JsonView(directory_cfg)));
    ASSERT_TRUE(directory_node.GetLastReadResult().success);
    EXPECT_EQ(directory_node.GetLastReadResult().value.segments.size(), 3u);

    sar::OrderedCrsdSetInputSourceNode manifest_node;
    const nlohmann::json manifest_cfg{
        {"manifest_path", (FixtureBaseDir() / "manifest.json").string()},
        {"stream_id", 10},
        {"backend_id", 0},
        {"backend", 0},
    };
    ASSERT_NO_THROW(manifest_node.Configure(graph::JsonView(manifest_cfg)));
    ASSERT_TRUE(manifest_node.GetLastReadResult().success);
    EXPECT_EQ(manifest_node.GetLastReadResult().value.total_vector_count, 6u);
}

TEST(OrderedCrsdSetInputSourceNodeTest, DynamicPluginLoadAndInstantiationSmoke) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(CrsdInputSourcePluginFilename()));

    auto created = registry->CreateNodeExpected("OrderedCrsdSetInputSourceNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::OrderedCrsdSetInputSourceNode>();
    ASSERT_TRUE(node);
}

TEST(OrderedCrsdSetInputSourceNodeTest, JsonTopologySmokeRunsForBinaryPathsDirectoryAndManifestModes) {
    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    for (const auto* mode : {"paths", "directory", "manifest"}) {
        const auto topology = BuildTopologyForMode(mode);
        const auto topology_path = WriteTempTopologyFile(topology);
        ASSERT_TRUE(std::filesystem::exists(topology_path));

        auto executor = graph::GraphExecutorBuilder()
                            .WithJsonConfig(topology_path.string())
                            .WithPluginDirectory(plugin_dir.string())
                            .WithExecutorTimeout(std::chrono::seconds(10))
                            .Build();

        ASSERT_NE(executor, nullptr) << "mode=" << mode;
        const auto initialized = executor->Init();
        ASSERT_TRUE(initialized.success)
            << "mode=" << mode << " " << initialized.message;
        ASSERT_NE(executor->GetGraphManager(), nullptr) << "mode=" << mode;

        const auto run_result = executor->Execute();
        ASSERT_TRUE(run_result.success) << "mode=" << mode << " message=" << run_result.message
                                        << " details=" << run_result.error_details;

        auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
        ASSERT_NE(sink, nullptr) << "mode=" << mode;
        EXPECT_GE(sink->consume_count(), 4u) << "mode=" << mode;
        const auto& sidecar = sink->last_diagnostics().sidecar;
        EXPECT_EQ(sidecar.marker, sar::SarFrameMarker::EndOfStream)
            << "mode=" << mode;
        EXPECT_EQ(sink->last_diagnostics().pulses_processed, 3u) << "mode=" << mode;
        EXPECT_EQ(sidecar.batch_id, 21u) << "mode=" << mode;
        EXPECT_EQ(sidecar.stream_id, 21u) << "mode=" << mode;
        EXPECT_EQ(sidecar.backend_id, 0u) << "mode=" << mode;
        EXPECT_EQ(sidecar.synthetic, false) << "mode=" << mode;
        EXPECT_EQ(sidecar.pulse_range_start, 6u) << "mode=" << mode;
        EXPECT_EQ(sidecar.pulse_range_count, 0u) << "mode=" << mode;
    }

    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path{SAR_CRSD_TINY_CONFIG_PATHS_JSON}));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path{SAR_CRSD_TINY_CONFIG_DIRECTORY_JSON}));
    ASSERT_TRUE(std::filesystem::exists(std::filesystem::path{SAR_CRSD_TINY_CONFIG_MANIFEST_JSON}));

    const auto paths_config = LoadJsonFile(std::filesystem::path{SAR_CRSD_TINY_CONFIG_PATHS_JSON});
    const auto directory_config = LoadJsonFile(std::filesystem::path{SAR_CRSD_TINY_CONFIG_DIRECTORY_JSON});
    const auto manifest_config = LoadJsonFile(std::filesystem::path{SAR_CRSD_TINY_CONFIG_MANIFEST_JSON});

    EXPECT_EQ(paths_config.at("nodes").at(0).at("type").get<std::string>(), "OrderedCrsdSetInputSourceNode");
    EXPECT_TRUE(paths_config.at("nodes").at(0).at("node_config").contains("crsd_paths"));
    EXPECT_TRUE(directory_config.at("nodes").at(0).at("node_config").contains("crsd_directory"));
    EXPECT_TRUE(manifest_config.at("nodes").at(0).at("node_config").contains("manifest_path"));
}

TEST(OrderedCrsdSetInputSourceNodeTest, OptionalLocalDataCrsdDirectorySmokeIsGated) {
    const char* gate = std::getenv("GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE");
    if (gate == nullptr || std::string(gate) != "1") {
        GTEST_SKIP() << "Set GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 to run local data/crsd smoke.";
    }

    const auto local_dir = std::filesystem::path{"data/crsd"};
    if (!std::filesystem::exists(local_dir)) {
        GTEST_SKIP() << "Local data/crsd directory not present.";
    }

    sar::OrderedCrsdSetInputSourceNode node;
    const nlohmann::json cfg_json{
        {"crsd_directory", local_dir.string()},
        {"stream_id", 77},
        {"backend_id", 0},
        {"backend", 0},
    };

    ASSERT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));
    ASSERT_TRUE(node.GetLastReadResult().success);
    EXPECT_GE(node.GetLastReadResult().value.segments.size(), 1u);
}

TEST(OrderedCrsdSetInputSourceNodeTest, OptionalLocalDataCrsdDirectorySmokeValidatesAllTenSegments) {
    const char* gate = std::getenv("GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE");
    if (gate == nullptr || std::string(gate) != "1") {
        GTEST_SKIP() << "Set GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 to run local data/crsd smoke.";
    }

    const auto local_dir = std::filesystem::path{"data/crsd"};
    if (!std::filesystem::exists(local_dir)) {
        GTEST_SKIP() << "Local data/crsd directory not present.";
    }

    sar::OrderedCrsdSetInputSourceNode node;
    const nlohmann::json cfg_json{
        {"crsd_directory", local_dir.string()},
        {"require_contiguous_segment_indices", false},
        {"stream_id", 77},
        {"backend_id", 0},
        {"backend", 0},
    };

    ASSERT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));
    const auto& result = node.GetLastReadResult();
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.value.segments.size(), 10u)
        << "Expected 10 CRSD segments (subData01..subData10)";
    EXPECT_GT(result.value.total_vector_count, 0u);
    EXPECT_NE(result.value.ordered_set_payload_hash, 0u);

    // Every segment must have nonzero vector count and a finite carrier/sample rate.
    std::uint64_t sum_vectors = 0u;
    for (const auto& seg : result.value.segments) {
        EXPECT_GT(seg.vector_count, 0u) << "segment " << seg.segment_index;
        EXPECT_GT(seg.samples_per_vector, 0u) << "segment " << seg.segment_index;
        EXPECT_NE(seg.payload_hash, 0u) << "segment " << seg.segment_index;
        sum_vectors += seg.vector_count;
    }
    EXPECT_EQ(sum_vectors, result.value.total_vector_count)
        << "Accounting: sum of segment vector counts must equal total_vector_count";
}

TEST(OrderedCrsdSetInputSourceNodeTest, OptionalLocalDataCrsdPathsSmokeValidatesAllTenSegments) {
    const char* gate = std::getenv("GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE");
    if (gate == nullptr || std::string(gate) != "1") {
        GTEST_SKIP() << "Set GRAPHX_SAR_ENABLE_LOCAL_CRSD_SMOKE=1 to run local data/crsd smoke.";
    }

    const std::vector<std::string> real_paths = {
        "data/crsd/subData01.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData02.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData03.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData04.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData05.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData06.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData07.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData08.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData09.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
        "data/crsd/subData10.crsd_output/gotcha_crsd_chunk_0000.crsd/product.crsd",
    };

    if (!std::filesystem::exists(real_paths.front())) {
        GTEST_SKIP() << "Local data/crsd paths not present.";
    }

    sar::OrderedCrsdSetInputSourceNode node;
    nlohmann::json cfg_json;
    cfg_json["crsd_paths"] = real_paths;
    cfg_json["stream_id"] = 78;
    cfg_json["backend_id"] = 0;
    cfg_json["backend"] = 0;

    ASSERT_NO_THROW(node.Configure(graph::JsonView(cfg_json)));
    const auto& result = node.GetLastReadResult();
    ASSERT_TRUE(result.success);

    EXPECT_EQ(result.value.segments.size(), 10u)
        << "Expected 10 CRSD segments from explicit paths list";
    EXPECT_GT(result.value.total_vector_count, 0u);
    EXPECT_NE(result.value.ordered_set_payload_hash, 0u);

    // Verify accounting and per-segment non-emptiness.
    std::uint64_t sum_vectors = 0u;
    for (const auto& seg : result.value.segments) {
        EXPECT_GT(seg.vector_count, 0u) << "segment " << seg.segment_index;
        EXPECT_GT(seg.samples_per_vector, 0u) << "segment " << seg.segment_index;
        EXPECT_NE(seg.payload_hash, 0u) << "segment " << seg.segment_index;
        EXPECT_FALSE(seg.vectors.empty()) << "segment " << seg.segment_index;
        if (!seg.vectors.empty()) {
            EXPECT_EQ(seg.first_vector.vector_index, seg.vectors.front().vector_index);
            EXPECT_EQ(seg.last_vector.vector_index, seg.vectors.back().vector_index);
            EXPECT_GT(seg.vectors.front().signal.size(), 0u);
        }
        sum_vectors += seg.vector_count;
    }
    EXPECT_EQ(sum_vectors, result.value.total_vector_count)
        << "Accounting: sum of segment vector counts must equal total_vector_count";

    // Emit the full stream and count tokens.
    std::size_t data_token_count = 0u;
    bool eos_seen = false;
    while (true) {
        auto token = node.Produce(std::integral_constant<std::size_t, 0>{});
        if (!token.has_value()) {
            break;
        }
        if (token->sidecar.marker == sar::SarFrameMarker::Data) {
            ++data_token_count;
        } else if (token->sidecar.marker == sar::SarFrameMarker::EndOfStream) {
            eos_seen = true;
        }
    }
    EXPECT_EQ(data_token_count, 10u)
        << "Expected one data token per CRSD segment";
    EXPECT_TRUE(eos_seen) << "EOS token must be emitted after all segments";
}
