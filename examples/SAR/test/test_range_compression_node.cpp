#include <gtest/gtest.h>

#include "sar/RangeCompressionNode.hpp"
#include "sar/SarCpuReference.hpp"

#include "graph/NodeFacade.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

#include <cstddef>
#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace {

#ifdef __APPLE__
constexpr const char* kSharedLibraryExtension = ".dylib";
#else
constexpr const char* kSharedLibraryExtension = ".so";
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

std::string RangeCompressionPluginFilename() {
    return std::string("librange_compression_node") + kSharedLibraryExtension;
}

sar::SarPulseBlockMessage MakePulse(std::size_t sample_count = 256u) {
    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = 3;
    msg.envelope.stream_id = 9;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.buffer.buffer_id = 44;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;
    msg.iq_samples.reserve(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        msg.iq_samples.emplace_back(static_cast<float>(i % 17) * 0.1f, static_cast<float>(i % 11) * 0.07f);
    }
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
}

sar::SarPulseBlockMessage MakeMatchedFilterPulse() {
    sar::reference::ChirpReferenceConfig cfg{};
    cfg.sample_count = 16;
    cfg.sample_rate_hz = 16.0e6;
    cfg.bandwidth_hz = 4.0e6;
    cfg.chirp_duration_s = 1.0e-6;
    cfg.range_origin_m = 0.0;
    cfg.range_spacing_m = 0.25;

    const auto chirp = sar::reference::GenerateLinearFmChirp(cfg);
    const auto echo = sar::reference::GenerateDelayedEcho(chirp, 3u, 0.75);

    sar::SarPulseBlockMessage msg{};
    msg.envelope.sequence_id = 5;
    msg.envelope.stream_id = 2;
    msg.envelope.marker = sar::SarFrameMarker::Data;
    msg.buffer.buffer_id = 99;
    msg.buffer.direction = sar::SarTransferDirection::HostToDevice;
    msg.iq_samples.reserve(echo.size());
    for (const auto& sample : echo) {
        msg.iq_samples.emplace_back(
            static_cast<float>(sample.real()),
            static_cast<float>(sample.imag()));
    }
    msg.buffer.byte_count = msg.iq_samples.size() * sizeof(sar::SarIqSample);
    return msg;
}

TEST(RangeCompressionNodeTest, AppliesDeterministicCompressionWhenEnabled) {
    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 48000.0;

    sar::RangeCompressionNode node(cfg);
    const auto input = MakePulse(256u);

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_NE(out->iq_samples[0].real(), input.iq_samples[0].real());
    EXPECT_EQ(out->iq_samples[0].imag(), 0.0f);
    EXPECT_EQ(out->buffer.byte_count, out->iq_samples.size() * sizeof(sar::SarIqSample));
}

TEST(RangeCompressionNodeTest, EndOfStreamPassesThroughWithoutCompression) {
    sar::RangeCompressionNode node;
    auto input = MakePulse(256u);
    input.envelope.marker = sar::SarFrameMarker::EndOfStream;

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->envelope.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_EQ(out->iq_samples[3], input.iq_samples[3]);
}

TEST(RangeCompressionNodeTest, MatchedFilterModeMatchesCpuReferenceMagnitude) {
    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.mode = sar::RangeCompressionMode::MatchedFilter;
    cfg.output = sar::RangeCompressionOutput::Magnitude;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 16.0e6;
    cfg.bandwidth_hz = 4.0e6;
    cfg.chirp_duration_s = 1.0e-6;
    cfg.range_spacing_m = 0.25;

    sar::RangeCompressionNode node(cfg);
    const auto input = MakeMatchedFilterPulse();

    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());

    sar::reference::ChirpReferenceConfig ref_cfg{};
    ref_cfg.sample_count = static_cast<std::uint32_t>(input.iq_samples.size());
    ref_cfg.sample_rate_hz = cfg.sample_rate_hz;
    ref_cfg.bandwidth_hz = cfg.bandwidth_hz;
    ref_cfg.chirp_duration_s = cfg.chirp_duration_s;
    ref_cfg.range_spacing_m = cfg.range_spacing_m;
    const auto chirp = sar::reference::GenerateLinearFmChirp(ref_cfg);

    std::vector<std::complex<double>> received;
    received.reserve(input.iq_samples.size());
    for (const auto& sample : input.iq_samples) {
        received.emplace_back(sample.real(), sample.imag());
    }
    const auto expected = sar::reference::MatchedFilterRangeCompress(received, chirp);
    const auto expected_image = sar::reference::MagnitudeImage(16u, 1u, expected);
    const auto expected_metrics = sar::reference::MeasureImageQuality(expected_image, 3u, 0u);

    std::vector<std::complex<double>> actual_complex;
    actual_complex.reserve(out->iq_samples.size());
    for (const auto& sample : out->iq_samples) {
        EXPECT_EQ(sample.imag(), 0.0f);
        actual_complex.emplace_back(sample.real(), sample.imag());
    }
    const auto actual_image = sar::reference::MagnitudeImage(16u, 1u, actual_complex);
    const auto actual_metrics = sar::reference::MeasureImageQuality(actual_image, 3u, 0u);
    const auto error = sar::reference::CompareImages(actual_image, expected_image);

    EXPECT_LT(error.l_inf, 1.0e-4);
    EXPECT_LT(error.rms, 1.0e-5);
    EXPECT_EQ(actual_metrics.peak.x, expected_metrics.peak.x);
    EXPECT_NEAR(actual_metrics.peak.value, expected_metrics.peak.value, 1.0e-4f);
}

TEST(RangeCompressionNodeTest, ConfigureEnablesMatchedFilterModeFromJson) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"mode", "matched_filter"},
        {"output", "complex"},
        {"gain", 0.5},
        {"sample_rate_hz", 16000000.0},
        {"bandwidth_hz", 4000000.0},
        {"chirp_duration_s", 0.000001},
        {"range_origin_m", 0.0},
        {"range_spacing_m", 0.25},
    };
    node.Configure(graph::JsonView(cfg_json));

    const auto& cfg = node.GetConfig();
    EXPECT_EQ(cfg.mode, sar::RangeCompressionMode::MatchedFilter);
    EXPECT_EQ(cfg.output, sar::RangeCompressionOutput::Complex);
    EXPECT_FLOAT_EQ(cfg.gain, 0.5f);
    EXPECT_DOUBLE_EQ(cfg.sample_rate_hz, 16000000.0);
    EXPECT_DOUBLE_EQ(cfg.bandwidth_hz, 4000000.0);
}

TEST(RangeCompressionNodeTest, ConfigureRejectsIncompleteMatchedFilterModeFromJson) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"mode", "matched_filter"},
        {"gain", 1.0},
    };

    EXPECT_THROW(node.Configure(graph::JsonView(cfg_json)), graph::ConfigError);
}

TEST(RangeCompressionNodeTest, DefaultModeRemainsFftMagnitudeWhenModeIsOmitted) {
    sar::RangeCompressionNode node;
    nlohmann::json cfg_json = {
        {"enabled", true},
        {"gain", 1.0},
        {"sample_rate_hz", 48000.0},
    };

    node.Configure(graph::JsonView(cfg_json));

    const auto& cfg = node.GetConfig();
    EXPECT_EQ(cfg.mode, sar::RangeCompressionMode::FftMagnitude);
    EXPECT_EQ(cfg.output, sar::RangeCompressionOutput::Magnitude);

    const auto input = MakePulse(256u);
    auto out = node.Transfer(
        input,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->iq_samples.size(), input.iq_samples.size());
    EXPECT_EQ(out->iq_samples[0].imag(), 0.0f);
    EXPECT_GE(out->iq_samples[0].real(), 0.0f);
}

TEST(RangeCompressionNodeTest, DynamicPluginLoadAndBehaviorValidation) {
    auto registry = std::make_shared<graph::PluginRegistry>();
    graph::PluginLoader loader(PLUGIN_OUTPUT_DIRECTORY, registry);

    ASSERT_TRUE(loader.LoadPluginSafe(RangeCompressionPluginFilename()));

    auto created = registry->CreateNodeExpected("RangeCompressionNode");
    ASSERT_TRUE(created);

    auto [node_handle, facade] = *created;
    ASSERT_NE(node_handle, nullptr);
    ASSERT_NE(facade, nullptr);

    graph::NodeFacadeAdapter adapter(node_handle, facade);
    auto node = adapter.GetNode<sar::RangeCompressionNode>();
    ASSERT_TRUE(node);

    sar::RangeCompressionConfig cfg{};
    cfg.enabled = true;
    cfg.gain = 1.0f;
    cfg.sample_rate_hz = 48000.0;
    node->SetConfig(cfg);

    auto out = node->Transfer(
        MakePulse(256u),
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->iq_samples.size(), 256u);
}

} // namespace
