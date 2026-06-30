// SPDX-License-Identifier: MIT

/**
 * @file test_crsd_focused_image_metal.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "graph/GraphBuilder.hpp"
#include "graph/GraphConfigParser.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeProviderBootstrap.hpp"
#include "sar/CrsdApertureAssemblyAdapterNode.hpp"
#include "sar/CrsdFocusedImageTransformMetal.hpp"
#include "sar/CrsdFocusedImageTransformNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef GPU_PLUGIN_OUTPUT_DIRECTORY
#define GPU_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef SAR_DEFINITIVE_JSON_CONFIG_PATH
#define SAR_DEFINITIVE_JSON_CONFIG_PATH "examples/SAR/config/sar_stripmap_definitive.json"
#endif

#ifndef SAR_CRSD_TINY_FOCUSED_IMAGE_CPU_CONFIG_JSON
#define SAR_CRSD_TINY_FOCUSED_IMAGE_CPU_CONFIG_JSON \
    "examples/SAR/config/sar_crsd_tiny_fixture_focused_image_cpu.json"
#endif

#ifndef SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON
#define SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON \
    "examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json"
#endif

constexpr double kSpeedOfLight = 299792458.0;
constexpr double kDefaultCarrierHz = 9.6e9;
constexpr double kDefaultSampleRateHz = 1.0e9;

class FakeAdapterReader final : public graphx::sar::ICrsdReader {
public:
    explicit FakeAdapterReader(graphx::sar::CrsdReadResult result)
        : result_(std::move(result)) {}

    [[nodiscard]] graphx::sar::CrsdReadResult ReadOrderedSet(
        const graphx::sar::CrsdReadOptions&) const override {
        return result_;
    }

private:
    graphx::sar::CrsdReadResult result_{};
};

graphx::sar::CrsdReadResult MakeCoherentPointTargetResult(
    std::uint64_t segments,
    std::uint64_t vectors_per_segment,
    std::uint64_t samples_per_vector,
    double carrier_hz = kDefaultCarrierHz,
    double sample_rate_hz = kDefaultSampleRateHz) {

    constexpr double kPi = 3.141592653589793238462643383279502884;
    const double wavelength_m = kSpeedOfLight / carrier_hz;
    const double range_spacing_m = kSpeedOfLight / (2.0 * sample_rate_hz);
    const double target_range_m = range_spacing_m * static_cast<double>(samples_per_vector / 4u);
    const double platform_y_m = -target_range_m;
    const double total_vectors = static_cast<double>(segments * vectors_per_segment);

    graphx::sar::CrsdReadResult result{};
    result.success = true;
    std::uint64_t global_start = 0u;

    for (std::uint64_t seg_idx = 0u; seg_idx < segments; ++seg_idx) {
        graphx::sar::CrsdSegmentRecord seg{};
        seg.segment_index = seg_idx;
        seg.channel_id = 0u;
        seg.global_vector_start = global_start;
        seg.vector_count = vectors_per_segment;
        seg.samples_per_vector = samples_per_vector;
        seg.carrier_hz = carrier_hz;
        seg.sample_rate_hz = sample_rate_hz;

        for (std::uint64_t v = 0u; v < vectors_per_segment; ++v) {
            const std::uint64_t global_v = global_start + v;

            graphx::sar::CrsdVectorRecord vec{};
            vec.vector_index = global_v;
            vec.channel_id = 0u;
            const double t = static_cast<double>(global_v) / std::max(total_vectors - 1.0, 1.0);
            const double platform_x = -50.0 + t * 100.0;
            vec.platform_position_m = {platform_x, platform_y_m, 0.0};
            vec.platform_velocity_mps = {1.0, 0.0, 0.0};
            vec.rcv_time_s = static_cast<double>(global_v) * 1.0e-4;

            const double dx = -platform_x;
            const double dy = -vec.platform_position_m[1];
            const double range_to_origin = std::sqrt(dx * dx + dy * dy);

            vec.signal.resize(samples_per_vector, {0.0f, 0.0f});
            const std::size_t bin = static_cast<std::size_t>(
                std::llround(range_to_origin / range_spacing_m));
            if (bin < samples_per_vector) {
                const double phase = -4.0 * kPi * range_to_origin / wavelength_m;
                vec.signal[bin] = {
                    static_cast<float>(std::cos(phase)),
                    static_cast<float>(std::sin(phase))};
            }
            seg.vectors.push_back(vec);
        }

        if (!seg.vectors.empty()) {
            seg.first_vector = seg.vectors.front();
            seg.last_vector = seg.vectors.back();
        }
        seg.payload_hash = seg_idx + 1u;
        result.value.segments.push_back(seg);
        global_start += vectors_per_segment;
    }

    result.value.total_vector_count = global_start;
    result.value.ordered_set_payload_hash = 0xFACECAFEu;
    return result;
}

std::optional<sar::SarPhaseHistoryControlMessage> BuildAssembledFrame(
    graphx::sar::CrsdReadResult reader_result) {
    auto fake_reader = std::make_shared<FakeAdapterReader>(std::move(reader_result));
    sar::CrsdApertureAssemblyAdapterNode adapter(
        sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"a/product.crsd", "b/product.crsd", "c/product.crsd"})}}));

    const auto segment_count = fake_reader->ReadOrderedSet({}).value.segments.size();
    for (std::uint64_t i = 0u; i < segment_count; ++i) {
        sar::SarAccelControlToken tok{};
        tok.sidecar.sequence_id = i;
        tok.sidecar.stream_id = 3u;
        tok.sidecar.tile_id = 0u;
        tok.sidecar.tile_count = 1u;
        tok.sidecar.marker = sar::SarFrameMarker::Data;
        adapter.Transfer(tok,
            std::integral_constant<std::size_t, 0>{},
            std::integral_constant<std::size_t, 0>{});
    }

    sar::SarAccelControlToken eos{};
    eos.sidecar.sequence_id = segment_count;
    eos.sidecar.stream_id = 3u;
    eos.sidecar.tile_id = 0u;
    eos.sidecar.tile_count = 1u;
    eos.sidecar.marker = sar::SarFrameMarker::EndOfStream;
    return adapter.Transfer(
        eos,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
}

const graph::NodeResolutionDiagnostic* FindResolverDiagnostic(
    const std::vector<graph::NodeResolutionDiagnostic>& diagnostics,
    const std::string& intent_type) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.intent_type == intent_type) {
            return &diagnostic;
        }
    }
    return nullptr;
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "unable to open config: " << path;
    nlohmann::json json;
    in >> json;
    return json;
}

void BindDefaultMetalCapabilities(
    sar::CrsdFocusedImageTransformMetalNode& node,
    graph::CapabilityBus& bus,
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>& telemetry_out) {
    auto context = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalContextCapability>();
    auto shared_queue = std::make_shared<graph::gpu::metal::capabilities::MetalSharedQueueCapability>(context);
    auto memory_pool = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalMemoryPoolCapability>();
    auto transfer = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTransferCapability>();
    auto kernel = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalKernelCapability>();
    auto telemetry = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability>();

    bus.Register<graph::gpu::metal::capabilities::IMetalContextCapability>(context);
    bus.Register<graph::gpu::metal::capabilities::IMetalSharedQueueCapability>(shared_queue);
    bus.Register<graph::gpu::metal::capabilities::IMetalMemoryPoolCapability>(memory_pool);
    bus.Register<graph::gpu::metal::capabilities::IMetalTransferCapability>(transfer);
    bus.Register<graph::gpu::metal::capabilities::IMetalKernelCapability>(kernel);
    bus.Register<graph::gpu::metal::capabilities::IMetalTelemetryCapability>(telemetry);

    ASSERT_TRUE(node.BindGpuCapabilities(bus));
    telemetry_out = std::move(telemetry);
}

} // namespace

TEST(CrsdFocusedImageMetalTest, CpuAndMetalTinyFixtureConfigsExistAndAreWellFormed) {
    const std::filesystem::path cpu_path{SAR_CRSD_TINY_FOCUSED_IMAGE_CPU_CONFIG_JSON};
    const std::filesystem::path metal_path{SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON};
    ASSERT_TRUE(std::filesystem::exists(cpu_path));
    ASSERT_TRUE(std::filesystem::exists(metal_path));

    const auto cpu = LoadJson(cpu_path);
    const auto metal = LoadJson(metal_path);

    EXPECT_EQ(cpu.at("name").get<std::string>(), "sar_crsd_tiny_fixture_focused_image_cpu");
    EXPECT_EQ(metal.at("name").get<std::string>(), "sar_crsd_tiny_fixture_focused_image_metal");
    EXPECT_EQ(cpu.at("edge_contract").get<std::string>(), "accel-token");
    EXPECT_EQ(metal.at("edge_contract").get<std::string>(), "accel-token");
    EXPECT_EQ(metal.at("execution_backend").get<std::string>(), "metal");
}

TEST(CrsdFocusedImageMetalTest, CpuVsMetalParityUsesSamePhaseHistoryContractAndTolerance) {
    const auto assembled = BuildAssembledFrame(MakeCoherentPointTargetResult(3u, 4u, 64u));
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformNode cpu(
        sar::CrsdFocusedImageTransformConfig{16u, 16u});
    auto cpu_result = cpu.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(cpu_result.has_value());

    sar::CrsdFocusedImageTransformMetalNode metal(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 16u,
            .image_height = 16u,
            .execution_backend = "metal",
            .allow_fallback = true,
            .require_kernel_execution = false,
        });
    graph::CapabilityBus bus;
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability> telemetry;
    BindDefaultMetalCapabilities(metal, bus, telemetry);

    auto metal_result = metal.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(metal_result.has_value());

    EXPECT_EQ(cpu_result->grid.width, metal_result->grid.width);
    EXPECT_EQ(cpu_result->grid.height, metal_result->grid.height);
    ASSERT_EQ(cpu_result->pixels.size(), metal_result->pixels.size());

    // Native lane is expected to be close in aggregate energy but not per-pixel identical.
    constexpr float kMaxAbsError = 2.0f;
    double l1_error = 0.0;
    for (std::size_t i = 0; i < cpu_result->pixels.size(); ++i) {
        const float err = std::fabs(cpu_result->pixels[i] - metal_result->pixels[i]);
        EXPECT_LE(err, kMaxAbsError);
        l1_error += static_cast<double>(err);
    }
    EXPECT_GT(l1_error, 0.0);
    EXPECT_GT(telemetry->KernelSamples(), 0u);
    EXPECT_NE(cpu_result->output_hash, metal_result->output_hash);
    EXPECT_EQ(cpu_result->input_ordered_set_hash, metal_result->input_ordered_set_hash);
}

TEST(CrsdFocusedImageMetalTest, MetalDiagnosticsAreNonzeroWhenNativeLaneRuns) {
    const auto assembled = BuildAssembledFrame(MakeCoherentPointTargetResult(3u, 3u, 32u));
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformMetalNode metal(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 16u,
            .image_height = 16u,
            .execution_backend = "metal",
            .allow_fallback = false,
            .require_kernel_execution = true,
            .backend_id = 0u,
            .h2d_queue_id = 11u,
            .kernel_queue_id = 21u,
            .d2h_queue_id = 31u,
        });
    graph::CapabilityBus bus;
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability> telemetry;
    BindDefaultMetalCapabilities(metal, bus, telemetry);

    auto result = metal.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->control.sidecar.bytes_h2d, 0u);
    EXPECT_GT(result->control.sidecar.bytes_d2h, 0u);
    EXPECT_GT(result->control.sidecar.kernel_dispatches, 0u);
    EXPECT_TRUE(result->control.has_transfer_ticket);
    EXPECT_TRUE(result->control.has_kernel_ticket);
    EXPECT_EQ(result->control.transfer_ticket.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_EQ(result->control.kernel_ticket.backend, graph::gpu::accel::BackendKind::Metal);
    EXPECT_GT(telemetry->KernelSamples(), 0u);
    EXPECT_GE(telemetry->TransferSamples(), 3u);
}

TEST(CrsdFocusedImageMetalTest, GuardrailRejectsForwardOnlyMetalExecution) {
    const auto assembled = BuildAssembledFrame(MakeCoherentPointTargetResult(2u, 3u, 32u));
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformMetalNode metal(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 16u,
            .image_height = 16u,
            .execution_backend = "metal",
            .allow_fallback = true,
            .require_kernel_execution = true,
            .force_forward_only_guardrail = true,
        });
    graph::CapabilityBus bus;
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability> telemetry;
    BindDefaultMetalCapabilities(metal, bus, telemetry);

    auto result = metal.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    EXPECT_FALSE(result.has_value());
}

TEST(CrsdFocusedImageMetalTest, PreservesSarAccelControlTokenIdentityWithGpuBackfillDiagnostics) {
    const auto assembled = BuildAssembledFrame(MakeCoherentPointTargetResult(3u, 3u, 32u));
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformMetalNode metal(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 16u,
            .image_height = 16u,
            .execution_backend = "metal",
            .allow_fallback = true,
            .require_kernel_execution = false,
            .backend_id = 7u,
            .h2d_queue_id = 101u,
            .kernel_queue_id = 201u,
            .d2h_queue_id = 301u,
        });
    graph::CapabilityBus bus;
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability> telemetry;
    BindDefaultMetalCapabilities(metal, bus, telemetry);

    auto result = metal.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->control.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(result->control.sidecar.stream_id, assembled->control.sidecar.stream_id);
    EXPECT_EQ(result->control.sidecar.aperture_id, assembled->control.sidecar.aperture_id);
    EXPECT_EQ(result->control.sidecar.pulse_range_start, assembled->control.sidecar.pulse_range_start);

    EXPECT_EQ(result->control.sidecar.backend, sar::SarBackendKind::NativeDevice);
    EXPECT_EQ(result->control.sidecar.backend_id, 7u);
    EXPECT_EQ(result->control.sidecar.h2d_queue_id, 101u);
    EXPECT_EQ(result->control.sidecar.kernel_queue_id, 201u);
    EXPECT_EQ(result->control.sidecar.d2h_queue_id, 301u);
    EXPECT_GT(result->control.sidecar.bytes_h2d, 0u);
    EXPECT_GT(result->control.sidecar.bytes_d2h, 0u);
    EXPECT_GT(telemetry->KernelSamples(), 0u);
}

TEST(CrsdFocusedImageMetalTest, NativeMetalOutputChangesWithGeometryPerturbation) {
    auto baseline = BuildAssembledFrame(MakeCoherentPointTargetResult(3u, 4u, 64u));
    auto perturbed = BuildAssembledFrame(MakeCoherentPointTargetResult(3u, 4u, 64u));
    ASSERT_TRUE(baseline.has_value());
    ASSERT_TRUE(perturbed.has_value());

    for (auto& seg : perturbed->frame.segments) {
        for (auto& vec : seg.vectors) {
            vec.platform_position_m[0] += 1.5;
            vec.rcv_time_s += 1.0e-6;
        }
    }

    sar::CrsdFocusedImageTransformMetalNode metal(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 16u,
            .image_height = 16u,
            .execution_backend = "metal",
            .allow_fallback = false,
            .require_kernel_execution = true,
        });
    graph::CapabilityBus bus;
    std::shared_ptr<graph::gpu::metal::capabilities::DefaultMetalTelemetryCapability> telemetry;
    BindDefaultMetalCapabilities(metal, bus, telemetry);

    auto a = metal.Transfer(
        *baseline,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    auto b = metal.Transfer(
        *perturbed,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(a->output_hash, b->output_hash);
    EXPECT_GT(telemetry->KernelSamples(), 1u);
}

TEST(CrsdFocusedImageMetalTest, ResolverDiagnosticsSelectMetalH2DKernelD2HIntents) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    const std::filesystem::path sar_plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    const std::filesystem::path gpu_plugin_dir{GPU_PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(sar_plugin_dir));
    ASSERT_TRUE(std::filesystem::exists(gpu_plugin_dir));

    auto metal_config = LoadJson(config_path);
    metal_config["execution_backend"] = "metal";
    metal_config["backend_fallback_policy"] = "strict";

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_crsd_focused_metal_resolver_selection.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto bootstrap = app::NodeProviderBootstrap::CreateProviderExpected(
        std::vector<std::string>{gpu_plugin_dir.string(), sar_plugin_dir.string()});
    ASSERT_TRUE(bootstrap);

    auto graph_cap = std::make_shared<capabilities::GraphCapability>();
    graph_cap->SetNodeProvider(bootstrap->provider);
    graph_cap->SetJsonConfigPath(temp_path.string());

    app::GraphBuilder graph_builder(graph_cap);
    const auto build_result = graph_builder.Build();
    ASSERT_TRUE(build_result.success) << build_result.error_message;

    const auto* h2d = FindResolverDiagnostic(build_result.resolver_diagnostics, "H2DAsyncAccelNode");
    const auto* bp = FindResolverDiagnostic(build_result.resolver_diagnostics, "SarBackprojectionTransformAccelNode");
    const auto* d2h = FindResolverDiagnostic(build_result.resolver_diagnostics, "D2HAsyncAccelNode");

    ASSERT_NE(h2d, nullptr);
    ASSERT_NE(bp, nullptr);
    ASSERT_NE(d2h, nullptr);

    EXPECT_EQ(h2d->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(bp->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_EQ(d2h->selected_backend, graph::ResolverBackend::Metal);
    EXPECT_FALSE(h2d->fallback_used);
    EXPECT_FALSE(bp->fallback_used);
    EXPECT_FALSE(d2h->fallback_used);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST(CrsdFocusedImageMetalTest, SplitMergePathPreservesSarTokenAndNonzeroGpuDiagnostics) {
    const std::filesystem::path config_path{SAR_DEFINITIVE_JSON_CONFIG_PATH};
    const std::filesystem::path plugin_dir{PLUGIN_OUTPUT_DIRECTORY};
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(plugin_dir));

    auto metal_config = LoadJson(config_path);
    metal_config["execution_backend"] = "metal";

    const auto temp_path = std::filesystem::temp_directory_path() /
                           "sar_crsd_focused_metal_split_merge_runtime.json";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << metal_config.dump(2) << '\n';
    }

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(temp_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(15))
                        .Build();
    ASSERT_NE(executor, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled());

    auto sink = sar::runtime::ResolveDiagnosticsSink(executor->GetGraphManager());
    ASSERT_NE(sink, nullptr);
    sink->UpdateFromGraphMetrics(executor->GetGraphManager()->GetMetrics());

    const auto& status = sink->last_token();
    EXPECT_EQ(status.sidecar.marker, sar::SarFrameMarker::EndOfStream);
    EXPECT_EQ(status.sidecar.expected_tiles, 4u);
    EXPECT_EQ(status.sidecar.received_tiles, 4u);
    EXPECT_TRUE(status.sidecar.merge_complete);
    EXPECT_GT(status.sidecar.bytes_h2d, 0u);
    EXPECT_GT(status.sidecar.bytes_d2h, 0u);
    EXPECT_GT(status.sidecar.kernel_dispatches, 0u);

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}
