// SPDX-License-Identifier: MIT

/**
 * @file test_dsp_spectrum_graph_runtime.cpp
 * @brief Test DSP Spectrum Graph Runtime Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "dsp/SpectrumSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeProviderBootstrap.hpp"

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

constexpr double kExpectedToneHz = 1000.0;
constexpr double kSampleRateHz = 48000.0;
constexpr std::size_t kFftSize = 256;
constexpr double kBinWidthHz = kSampleRateHz / static_cast<double>(kFftSize);

/**
 * @brief Dsp spectrum config path.
 */
std::filesystem::path DspSpectrumConfigPath() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
           "libdsp/config/dsp_sine_fft_spectrum_256.json";
}

/**
 * @brief Plugin directory.
 */
std::filesystem::path PluginDirectory() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

/**
 * @brief Load json.
 * @param path Parameter for load json.
 */
nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open JSON file: " + path.string());
    }
    nlohmann::json json;
    input >> json;
    return json;
}

std::shared_ptr<dsp::SpectrumSinkNode<float, kFftSize>> ResolveSpectrumSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }

        auto sink = wrapper->GetNode<dsp::SpectrumSinkNode<float, kFftSize>>();
        if (sink) {
            return sink;
        }
    }

    return nullptr;
}

}  // namespace

TEST(DspSpectrumGraphRuntimeTest, ConfigUsesCpuOnlyDspNodes) {
    const auto config = LoadJson(DspSpectrumConfigPath());
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());

    const std::set<std::string> expected_types{
        "SineSignalNode<256>",
        "CpuSpectrumDftNode<256>",
        "SpectrumSinkNode<256>",
    };
    std::set<std::string> actual_types;

    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        const auto type = node.at("type").get<std::string>();
        actual_types.insert(type);
        EXPECT_EQ(type.find("Metal"), std::string::npos);
        EXPECT_EQ(type.find("GPU"), std::string::npos);
        EXPECT_EQ(type.find("Gpu"), std::string::npos);
    }

    EXPECT_EQ(actual_types, expected_types);
    EXPECT_EQ(config.value("execution_backend", ""), "auto");
}

TEST(DspSpectrumGraphRuntimeTest, JsonTopologyRunsThroughExecutorAndDetectsSinePeak) {
    const auto config_path = DspSpectrumConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto plugin_dir = PluginDirectory();
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(plugin_dir.string());
    ASSERT_TRUE(bootstrap);
    ASSERT_NE(bootstrap->provider, nullptr);

    auto available = app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(bootstrap->provider);
    ASSERT_TRUE(available);
    const std::set<std::string> available_types(available->begin(), available->end());
    EXPECT_TRUE(available_types.contains("SineSignalNode<256>"));
    EXPECT_TRUE(available_types.contains("CpuSpectrumDftNode<256>"));
    EXPECT_TRUE(available_types.contains("SpectrumSinkNode<256>"));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(5))
                        .Build();

    ASSERT_NE(executor, nullptr);
    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    EXPECT_EQ(graph_manager->GetNodes().size(), 3u);
    EXPECT_EQ(graph_manager->GetEdges().size(), 2u);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = ResolveSpectrumSink(graph_manager);
    ASSERT_NE(sink, nullptr);
    EXPECT_GE(sink->GetFrameCount(), 1u);

    const auto latest = sink->GetLatestSpectrum();
    ASSERT_TRUE(latest.has_value());
    ASSERT_TRUE(latest->IsValid());

    // SineSignalNode uses sin(theta) + j*cos(theta), so the configured negative
    // complex frequency produces a positive 1 kHz peak in FFTManager's positive bins.
    EXPECT_NEAR(latest->peak_frequency_hz, kExpectedToneHz, kBinWidthHz);
    EXPECT_GT(latest->peak_magnitude, 0.0f);
    EXPECT_DOUBLE_EQ(latest->sample_rate_hz, kSampleRateHz);
    EXPECT_EQ(latest->magnitudes.size(), kFftSize / 2);
}
