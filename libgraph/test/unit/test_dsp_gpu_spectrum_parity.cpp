// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dsp/DspIqH2DNode.hpp"
#include "dsp/DspMagnitudeD2HNode.hpp"
#include "dsp/MagnitudePacket.hpp"
#include "dsp/MetalSpectrumDftNode.hpp"
#include "dsp/SpectrumSinkNode.hpp"
#include "gpu/bootstrap/GpuCapabilityBootstrap.hpp"
#include "gpu/metal/capabilities/IMetalCapabilities.hpp"
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
#include "gpu/metal/capabilities/NativeMetalCapabilities.hpp"
#endif
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

constexpr std::size_t kFftSize = 256;
constexpr double kExpectedToneHz = 1000.0;
constexpr double kSampleRateHz = 48000.0;
constexpr double kBinWidthHz = kSampleRateHz / static_cast<double>(kFftSize);
constexpr double kPeakFrequencyToleranceHz = kBinWidthHz;

// The CPU and Metal lanes both compute a direct DFT over the same Hann-windowed
// deterministic signal. These tolerances allow small CPU/Metal floating-point
// differences without accepting a scale or bin error.
constexpr double kMagnitudeAbsTolerance = 1.0e-2;
constexpr double kMagnitudeRelTolerance = 5.0e-2;

using MagnitudePacketType = dsp::MagnitudePacket<float, kFftSize>;

std::filesystem::path CpuConfigPath() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
           "libdsp/config/dsp_sine_fft_spectrum_256.json";
}

std::filesystem::path GpuConfigPath() {
    return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
           "libdsp/config/dsp_sine_metal_dft_spectrum_256.json";
}

std::filesystem::path PluginDirectory() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
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

std::string MetalDspRuntimeUnavailableReason() {
    std::string reason =
        "Native Metal DSP GPU runtime is unavailable; skipping CPU-vs-GPU parity";
#if GRAPHX_ENABLE_METAL_NATIVE_RUNTIME
    reason += ": ";
    reason += graph::gpu::metal::capabilities::NativeMetalRuntimeDiagnostics();
#endif
    return reason;
}

std::optional<std::string> MetalDspRuntimeSkipReason() {
    if (HasUsableMetalDspRuntime()) {
        return std::nullopt;
    }
    return MetalDspRuntimeUnavailableReason();
}

std::optional<MagnitudePacketType> RunSpectrumGraph(
    const std::filesystem::path& config_path,
    std::chrono::seconds timeout) {
    if (!std::filesystem::exists(config_path)) {
        ADD_FAILURE() << "missing config: " << config_path;
        return std::nullopt;
    }

    const auto plugin_dir = PluginDirectory();
    if (!std::filesystem::exists(plugin_dir)) {
        ADD_FAILURE() << "missing plugin directory: " << plugin_dir;
        return std::nullopt;
    }

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(timeout)
                        .Build();
    if (!executor) {
        ADD_FAILURE() << "failed to build executor for " << config_path;
        return std::nullopt;
    }

    const auto run_result = executor->Execute();
    if (!run_result.success || !executor->IsCompletionSignaled()) {
        ADD_FAILURE() << "graph did not complete: " << config_path
                      << " message=" << run_result.message
                      << " details=" << run_result.error_details;
        return std::nullopt;
    }

    auto sink = ResolveNode<dsp::SpectrumSinkNode<float, kFftSize>>(executor->GetGraphManager());
    if (!sink) {
        ADD_FAILURE() << "failed to resolve SpectrumSinkNode for " << config_path;
        return std::nullopt;
    }
    if (sink->GetFrameCount() == 0) {
        ADD_FAILURE() << "spectrum sink received no frames for " << config_path;
        return std::nullopt;
    }

    auto latest = sink->GetLatestSpectrum();
    if (!latest || !latest->IsValid()) {
        ADD_FAILURE() << "latest spectrum is missing or invalid for " << config_path;
        return std::nullopt;
    }
    return latest;
}

struct ParityPackets {
    MagnitudePacketType cpu;
    MagnitudePacketType gpu;
};

std::optional<ParityPackets> RunCpuAndGpuLanes() {
    auto cpu = RunSpectrumGraph(CpuConfigPath(), std::chrono::seconds(5));
    if (!cpu) {
        return std::nullopt;
    }

    auto gpu = RunSpectrumGraph(GpuConfigPath(), std::chrono::seconds(8));
    if (!gpu) {
        return std::nullopt;
    }

    return ParityPackets{*cpu, *gpu};
}

double MagnitudeTolerance(double expected) {
    return std::max(kMagnitudeAbsTolerance,
                    kMagnitudeRelTolerance * std::abs(expected));
}

void ExpectMagnitudeNear(double actual, double expected, const std::string& label) {
    EXPECT_NEAR(actual, expected, MagnitudeTolerance(expected)) << label;
}

}  // namespace

TEST(DspGpuSpectrumParityTest, PeakFrequencyMatchesCpuReference) {
    if (const auto skip_reason = MetalDspRuntimeSkipReason()) {
        GTEST_SKIP() << *skip_reason;
    }

    const auto packets = RunCpuAndGpuLanes();
    ASSERT_TRUE(packets.has_value());

    EXPECT_NEAR(packets->cpu.peak_frequency_hz, kExpectedToneHz, kPeakFrequencyToleranceHz);
    EXPECT_NEAR(packets->gpu.peak_frequency_hz,
                packets->cpu.peak_frequency_hz,
                kPeakFrequencyToleranceHz);
    EXPECT_EQ(packets->gpu.peak_bin, packets->cpu.peak_bin);
}

TEST(DspGpuSpectrumParityTest, PeakMagnitudeMatchesCpuWithinTolerance) {
    if (const auto skip_reason = MetalDspRuntimeSkipReason()) {
        GTEST_SKIP() << *skip_reason;
    }

    const auto packets = RunCpuAndGpuLanes();
    ASSERT_TRUE(packets.has_value());

    ASSERT_GT(packets->cpu.peak_magnitude, 0.0f);
    ASSERT_GT(packets->gpu.peak_magnitude, 0.0f);
    ExpectMagnitudeNear(packets->gpu.peak_magnitude,
                        packets->cpu.peak_magnitude,
                        "peak_magnitude");
}

TEST(DspGpuSpectrumParityTest, SelectedMagnitudeBinsMatchCpuWithinTolerance) {
    if (const auto skip_reason = MetalDspRuntimeSkipReason()) {
        GTEST_SKIP() << *skip_reason;
    }

    const auto packets = RunCpuAndGpuLanes();
    ASSERT_TRUE(packets.has_value());
    ASSERT_EQ(packets->cpu.magnitudes.size(), packets->gpu.magnitudes.size());

    const std::vector<std::size_t> selected_bins{
        0,
        1,
        packets->cpu.peak_bin > 0 ? packets->cpu.peak_bin - 1 : 0,
        packets->cpu.peak_bin,
        packets->cpu.peak_bin + 1,
        16,
        32,
        64,
        96,
        packets->cpu.magnitudes.size() - 1,
    };

    for (const auto bin : selected_bins) {
        ASSERT_LT(bin, packets->cpu.magnitudes.size());
        ExpectMagnitudeNear(packets->gpu.magnitudes[bin],
                            packets->cpu.magnitudes[bin],
                            "bin " + std::to_string(bin));
    }
}

TEST(DspGpuSpectrumParityTest, SkipsClearlyWhenMetalUnavailable) {
    if (const auto skip_reason = MetalDspRuntimeSkipReason()) {
        GTEST_SKIP() << *skip_reason;
    }

    SUCCEED() << "Native Metal DSP GPU runtime is available; parity tests will execute";
}
