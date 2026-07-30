// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "dsp/DspIqH2DNode.hpp"
#include "dsp/DspMagnitudeD2HNode.hpp"
#include "dsp/MetalSpectrumDftNode.hpp"
#include "dsp/SpectrumSinkNode.hpp"
#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#endif
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

constexpr std::size_t kFftSize = 256;

std::filesystem::path DspGpuSpectrumConfigPath() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
           "libdsp/config/dsp_sine_metal_dft_spectrum_256.json";
}

std::filesystem::path PluginDirectory() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.good()) {
        throw std::runtime_error("failed to open JSON file: " + path.string());
    }
    nlohmann::json json;
    input >> json;
    return json;
}

bool HasUsableMetalDspRuntime() {
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    if (!graph::gpu::metal::capabilities::NativeMetalRuntimeAvailable()) {
        return false;
    }
#else
    return false;
#endif

    graph::CapabilityBus bus;
    graph::gpu::GpuCapabilityBootstrapOptions options{};
    options.require_native_metal_runtime = true;
    graph::gpu::RegisterDefaultGpuCapabilities(bus, options);

    auto metal_context =
        bus.Get<graph::gpu::metal::capabilities::IMetalContextCapability>();
    if (!metal_context) {
        return false;
    }
    bus.Register<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>(
        std::make_shared<graph::gpu::metal::capabilities::MetalSharedQueueCapability>(
            metal_context));

    dsp::DspIqH2DNode<kFftSize> h2d;
    dsp::MetalSpectrumDftNode<kFftSize> dft;
    dsp::DspMagnitudeD2HNode<kFftSize> d2h;
    return h2d.BindGpuCapabilities(bus) &&
           dft.BindGpuCapabilities(bus) &&
           d2h.BindGpuCapabilities(bus);
}

std::optional<std::string> MetalDspRuntimeSkipReason() {
    if (HasUsableMetalDspRuntime()) {
        return std::nullopt;
    }

    std::string reason =
        "Native Metal DSP GPU runtime is unavailable; skipping real GPU graph execution";
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    reason += ": ";
    reason += graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#endif
    return reason;
}

template <typename NodeT>
std::shared_ptr<NodeT> ResolveNode(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        auto concrete = wrapper->GetNode<NodeT>();
        if (concrete) {
            return concrete;
        }
    }

    return nullptr;
}

}  // namespace

TEST(DspGpuSpectrumGraphRuntimeTest, ConfigUsesExplicitGpuDspNodes) {
    const auto config = LoadJson(DspGpuSpectrumConfigPath());
    ASSERT_TRUE(config.contains("nodes"));
    ASSERT_TRUE(config.at("nodes").is_array());
    ASSERT_TRUE(config.contains("edges"));
    ASSERT_TRUE(config.at("edges").is_array());

    const std::set<std::string> expected_types{
        "SineSignalNode<256>",
        "DspIqH2DNode<256>",
        "MetalSpectrumDftNode<256>",
        "DspMagnitudeD2HNode<256>",
        "SpectrumSinkNode<256>",
    };
    std::set<std::string> actual_types;
    for (const auto& node : config.at("nodes")) {
        ASSERT_TRUE(node.contains("type"));
        actual_types.insert(node.at("type").get<std::string>());
    }

    EXPECT_EQ(actual_types, expected_types);
    EXPECT_EQ(config.at("nodes").size(), 5u);
    EXPECT_EQ(config.at("edges").size(), 4u);
    EXPECT_EQ(config.value("execution_backend", ""), "metal");
}

TEST(DspGpuSpectrumGraphRuntimeTest, JsonTopologyRunsThroughExecutorAndSinkReceivesSpectrum) {
    if (const auto skip_reason = MetalDspRuntimeSkipReason()) {
        GTEST_SKIP() << *skip_reason;
    }

    const auto config_path = DspGpuSpectrumConfigPath();
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
    EXPECT_TRUE(available_types.contains("DspIqH2DNode<256>"));
    EXPECT_TRUE(available_types.contains("MetalSpectrumDftNode<256>"));
    EXPECT_TRUE(available_types.contains("DspMagnitudeD2HNode<256>"));
    EXPECT_TRUE(available_types.contains("SpectrumSinkNode<256>"));

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(8))
                        .Build();

    ASSERT_NE(executor, nullptr);
    const auto initialized = executor->Init();
    ASSERT_TRUE(initialized.success) << initialized.message;
    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    EXPECT_EQ(graph_manager->GetNodes().size(), 5u);
    EXPECT_EQ(graph_manager->GetEdges().size(), 4u);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto h2d = ResolveNode<dsp::DspIqH2DNode<kFftSize>>(graph_manager);
    auto dft = ResolveNode<dsp::MetalSpectrumDftNode<kFftSize>>(graph_manager);
    auto d2h = ResolveNode<dsp::DspMagnitudeD2HNode<kFftSize>>(graph_manager);
    auto sink = ResolveNode<dsp::SpectrumSinkNode<float, kFftSize>>(graph_manager);
    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(dft, nullptr);
    ASSERT_NE(d2h, nullptr);
    ASSERT_NE(sink, nullptr);

    ASSERT_TRUE(executor->IsCompletionSignaled())
        << "sink_frames=" << sink->GetFrameCount()
        << " h2d=" << h2d->GetDiagnostics().Raw().dump()
        << " dft=" << dft->GetDiagnostics().Raw().dump()
        << " d2h=" << d2h->GetDiagnostics().Raw().dump();
    EXPECT_GE(sink->GetFrameCount(), 1u);
    const auto latest = sink->GetLatestSpectrum();
    ASSERT_TRUE(latest.has_value());
    ASSERT_TRUE(latest->IsValid());
    EXPECT_EQ(latest->magnitudes.size(), kFftSize / 2);
    EXPECT_GT(latest->peak_magnitude, 0.0f);

    const auto h2d_diagnostics = h2d->GetDiagnostics().Raw();
    EXPECT_TRUE(h2d_diagnostics.at("has_device_view").get<bool>());
    EXPECT_TRUE(h2d_diagnostics.at("has_transfer_ticket").get<bool>());
    EXPECT_EQ(h2d_diagnostics.at("backend").get<std::string>(), "Metal");

    const auto dft_diagnostics = dft->GetDiagnostics().Raw();
    EXPECT_EQ(dft_diagnostics.at("backend").get<std::string>(), "Metal");
    EXPECT_TRUE(dft_diagnostics.at("has_input_device_view").get<bool>());
    EXPECT_TRUE(dft_diagnostics.at("has_output_device_view").get<bool>());
    EXPECT_TRUE(dft_diagnostics.at("has_kernel_ticket").get<bool>());
    EXPECT_TRUE(dft_diagnostics.at("kernel_registered").get<bool>());

    const auto d2h_diagnostics = d2h->GetDiagnostics().Raw();
    EXPECT_EQ(d2h_diagnostics.at("backend").get<std::string>(), "Metal");
    EXPECT_TRUE(d2h_diagnostics.at("has_device_view").get<bool>());
    EXPECT_TRUE(d2h_diagnostics.at("has_host_view").get<bool>());
    EXPECT_TRUE(d2h_diagnostics.at("has_transfer_ticket").get<bool>());
    EXPECT_GT(d2h_diagnostics.at("peak_magnitude").get<float>(), 0.0f);
}
