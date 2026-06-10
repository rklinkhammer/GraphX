#include <gtest/gtest.h>

#include "sar/AzimuthTileSplitNode.hpp"
#include "sar/RangeCompressionNode.hpp"
#include "sar/RangeWindowNode.hpp"
#include "sar/SyntheticApertureIqSourceNode.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <utility>

namespace {

#ifndef SAR_DEFINITIVE_JSON_CONFIG_PATH
#define SAR_DEFINITIVE_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_definitive.json"
#endif

bool HasEdge(const nlohmann::json& config,
             const char* source,
             const char* target) {
    if (!config.contains("edges") || !config["edges"].is_array()) {
        return false;
    }

    for (const auto& edge : config["edges"]) {
        if (!edge.is_object()) {
            continue;
        }
        const auto source_id = edge.value("source_node_id", "");
        const auto target_id = edge.value("target_node_id", "");
        if (source_id == source && target_id == target) {
            return true;
        }
    }
    return false;
}

nlohmann::json::object_t BuildNodeTypeById(const nlohmann::json& config) {
    nlohmann::json::object_t result{};
    if (!config.contains("nodes") || !config["nodes"].is_array()) {
        return result;
    }
    for (const auto& node : config["nodes"]) {
        if (!node.is_object()) {
            continue;
        }
        const auto id = node.value("id", "");
        const auto type = node.value("type", "");
        if (!id.empty()) {
            result[id] = type;
        }
    }
    return result;
}

TEST(SarPr2TokenContractTest, DefinitiveTopologyDeclaresTokenContractThroughSplitHandoff) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::ifstream in(config_path);
    ASSERT_TRUE(in.good());

    nlohmann::json config;
    in >> config;

    ASSERT_TRUE(config.is_object());
    EXPECT_EQ(config.value("edge_contract", ""), "accel-token");

    const auto node_types = BuildNodeTypeById(config);
    ASSERT_TRUE(node_types.contains("src"));
    ASSERT_TRUE(node_types.contains("window"));
    ASSERT_TRUE(node_types.contains("compression"));
    ASSERT_TRUE(node_types.contains("split"));
    ASSERT_TRUE(node_types.contains("h2d"));

    EXPECT_EQ(node_types.at("src"), "SyntheticApertureIqSourceNode");
    EXPECT_EQ(node_types.at("window"), "RangeWindowNode");
    EXPECT_EQ(node_types.at("compression"), "RangeCompressionNode");
    EXPECT_EQ(node_types.at("split"), "AzimuthTileSplitNode");
    EXPECT_EQ(node_types.at("h2d"), "H2DAsyncNode");

    EXPECT_TRUE(HasEdge(config, "src", "window"));
    EXPECT_TRUE(HasEdge(config, "window", "compression"));
    EXPECT_TRUE(HasEdge(config, "compression", "split"));
    EXPECT_TRUE(HasEdge(config, "split", "h2d"));
}

TEST(SarPr2TokenContractTest, SourceAndDspNodeSignaturesAreTokenBased) {
    static_assert(std::is_same_v<
                      decltype(std::declval<sar::SyntheticApertureIqSourceNode>().Produce(
                          std::integral_constant<std::size_t, 0>{})),
                      std::optional<sar::SarAccelControlToken>>);
    static_assert(std::is_same_v<
                      decltype(std::declval<sar::RangeWindowNode>().Transfer(
                          std::declval<const sar::SarAccelControlToken&>(),
                          std::integral_constant<std::size_t, 0>{},
                          std::integral_constant<std::size_t, 0>{})),
                      std::optional<sar::SarAccelControlToken>>);
    static_assert(std::is_same_v<
                      decltype(std::declval<sar::RangeCompressionNode>().Transfer(
                          std::declval<const sar::SarAccelControlToken&>(),
                          std::integral_constant<std::size_t, 0>{},
                          std::integral_constant<std::size_t, 0>{})),
                      std::optional<sar::SarAccelControlToken>>);
    static_assert(std::is_same_v<
                      decltype(std::declval<sar::AzimuthTileSplitNode>().Transfer(
                          std::declval<const sar::SarAccelControlToken&>(),
                          std::integral_constant<std::size_t, 0>{},
                          std::integral_constant<std::size_t, 0>{})),
                      std::optional<sar::SarAccelControlToken>>);
    SUCCEED();
}

TEST(SarPr2TokenContractTest, SourceThroughSplitInitializesTokenSidecarAndTimings) {
    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 7;
    source_cfg.total_pulses = 1;
    source_cfg.samples_per_pulse = 256;
    source_cfg.backend_id = 3;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::RangeWindowConfig window_cfg{};
    window_cfg.enabled = true;
    window_cfg.gain = 1.0f;

    sar::RangeCompressionConfig compression_cfg{};
    compression_cfg.enabled = true;
    compression_cfg.mode = sar::RangeCompressionMode::FftMagnitude;
    compression_cfg.output = sar::RangeCompressionOutput::Magnitude;
    compression_cfg.sample_rate_hz = 48000.0;

    sar::AzimuthTileSplitConfig split_cfg{};
    split_cfg.tile_count = 4;
    split_cfg.fixed_tile_id = 1;
    split_cfg.backend_id = 3;
    split_cfg.backend = sar::SarBackendKind::Host;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::RangeWindowNode window(window_cfg);
    sar::RangeCompressionNode compression(compression_cfg);
    sar::AzimuthTileSplitNode split(split_cfg);

    auto pulse = src.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(pulse.has_value());
    ASSERT_EQ(pulse->sidecar.marker, sar::SarFrameMarker::Data);

    auto windowed = window.Transfer(
        *pulse,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(windowed.has_value());

    auto compressed = compression.Transfer(
        *windowed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(compressed.has_value());

    auto token = split.Transfer(
        *compressed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(token.has_value());
    ASSERT_TRUE(token->has_host_view);

    EXPECT_EQ(token->sidecar.sequence_id, pulse->sidecar.sequence_id);
    EXPECT_EQ(token->sidecar.batch_id, pulse->sidecar.batch_id);
    EXPECT_EQ(token->sidecar.aperture_id, pulse->sidecar.aperture_id);
    EXPECT_EQ(token->sidecar.pulse_range_start, pulse->sidecar.pulse_range_start);
    EXPECT_EQ(token->sidecar.pulse_range_count, pulse->sidecar.pulse_range_count);
    EXPECT_EQ(token->sidecar.stream_id, source_cfg.stream_id);
    EXPECT_EQ(token->sidecar.backend_id, source_cfg.backend_id);
    EXPECT_EQ(token->sidecar.marker, sar::SarFrameMarker::Data);

    EXPECT_EQ(token->sidecar.payload_byte_count, token->host_view.bytes);
    EXPECT_GT(token->sidecar.stage_timings.range_window_time_us, 0u);
    EXPECT_GT(token->sidecar.stage_timings.range_compression_time_us, 0u);
    EXPECT_GT(token->sidecar.stage_timings.split_time_us, 0u);
}

TEST(SarPr2TokenContractTest, EndOfStreamPreservesIdentityAndCarriesTokenToHandoff) {
    sar::SyntheticApertureIqSourceConfig source_cfg{};
    source_cfg.stream_id = 11;
    source_cfg.total_pulses = 1;
    source_cfg.samples_per_pulse = 128;
    source_cfg.backend_id = 2;
    source_cfg.backend = sar::SarBackendKind::Host;

    sar::SyntheticApertureIqSourceNode src(source_cfg);
    sar::RangeWindowNode window;
    sar::RangeCompressionNode compression;
    sar::AzimuthTileSplitNode split;

    auto data = src.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(data.has_value());
    auto eos = src.Produce(std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos.has_value());
    ASSERT_EQ(eos->sidecar.marker, sar::SarFrameMarker::EndOfStream);

    auto eos_windowed = window.Transfer(
        *eos,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos_windowed.has_value());

    auto eos_compressed = compression.Transfer(
        *eos_windowed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos_compressed.has_value());

    auto eos_token = split.Transfer(
        *eos_compressed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(eos_token.has_value());

    EXPECT_EQ(eos_token->sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(eos_token->sidecar.sequence_id, eos->sidecar.sequence_id);
    EXPECT_EQ(eos_token->sidecar.stream_id, eos->sidecar.stream_id);
    EXPECT_EQ(eos_token->sidecar.pulse_range_count, 0u);
    EXPECT_EQ(eos_token->sidecar.payload_byte_count, 0u);
    ASSERT_TRUE(eos_token->has_host_view);
    EXPECT_GE(eos_token->host_view.bytes, static_cast<std::uint64_t>(sizeof(float)));
}

} // namespace