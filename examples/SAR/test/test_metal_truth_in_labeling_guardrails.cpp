// SPDX-License-Identifier: MIT

/**
 * @file test_metal_truth_in_labeling_guardrails.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "graph/CapabilityBus.hpp"
#include "sar/CrsdApertureAssemblyAdapterNode.hpp"
#include "sar/CrsdFocusedImageTransformMetal.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace {

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

graphx::sar::CrsdReadResult MakeSmallResult() {
    graphx::sar::CrsdReadResult result{};
    result.success = true;

    graphx::sar::CrsdSegmentRecord seg{};
    seg.segment_index = 0;
    seg.channel_id = 0;
    seg.global_vector_start = 0;
    seg.vector_count = 2;
    seg.samples_per_vector = 8;
    seg.carrier_hz = 9.6e9;
    seg.sample_rate_hz = 1.0e9;

    for (std::uint64_t v = 0; v < 2; ++v) {
        graphx::sar::CrsdVectorRecord vec{};
        vec.vector_index = v;
        vec.channel_id = 0;
        vec.rcv_time_s = static_cast<double>(v) * 1.0e-4;
        vec.platform_position_m = {static_cast<double>(v), -10.0, 0.0};
        vec.platform_velocity_mps = {1.0, 0.0, 0.0};
        vec.signal.resize(8, {0.0f, 0.0f});
        vec.signal[1] = {1.0f, 0.0f};
        seg.vectors.push_back(vec);
    }

    seg.first_vector = seg.vectors.front();
    seg.last_vector = seg.vectors.back();
    seg.payload_hash = 123;
    result.value.segments.push_back(seg);
    result.value.total_vector_count = 2;
    result.value.ordered_set_payload_hash = 0xABCD;
    return result;
}

std::optional<sar::SarPhaseHistoryControlMessage> BuildAssembledFrame() {
    auto fake_reader = std::make_shared<FakeAdapterReader>(MakeSmallResult());
    sar::CrsdApertureAssemblyAdapterNode adapter(
        sar::CrsdApertureAssemblyAdapterConfig{}, fake_reader);
    adapter.Configure(graph::JsonView(nlohmann::json{
        {"crsd_paths", nlohmann::json::array({"one/product.crsd"})}}));

    sar::SarAccelControlToken tok{};
    tok.sidecar.sequence_id = 0;
    tok.sidecar.stream_id = 9;
    tok.sidecar.marker = sar::SarFrameMarker::Data;
    (void)adapter.Transfer(
        tok,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    sar::SarAccelControlToken eos{};
    eos.sidecar.sequence_id = 1;
    eos.sidecar.stream_id = 9;
    eos.sidecar.marker = sar::SarFrameMarker::EndOfStream;
    return adapter.Transfer(
        eos,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "unable to read " << path;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void BindDefaultCapabilities(sar::CrsdFocusedImageTransformMetalNode& node) {
    graph::CapabilityBus bus;
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
}

} // namespace

TEST(MetalTruthInLabelingGuardrailTest, InventoryDocumentClassifiesAllActiveMetalNodesAndBlocksPr6) {
    const auto path = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
                      "README.md";
    ASSERT_TRUE(std::filesystem::exists(path));

    const std::string text = ReadFile(path);
    const std::array<const char*, 13> node_names = {
        "HostIngressPinnedSourceNodeMetal",
        "H2DAsyncNodeMetal",
        "D2HAsyncNodeMetal",
        "PeerCopyNodeMetal",
        "DeviceShardNodeMetal",
        "LeaseReleaseNodeMetal",
        "QueueSyncNodeMetal",
        "HostEgressSinkNodeMetal",
        "DeviceKernelNodeMetal",
        "DeviceTransformNodeMetal",
        "DeviceReduceNodeMetal",
        "CollectiveReduceNodeMetal",
        "CrsdFocusedImageTransformMetalNode",
    };
    for (const char* name : node_names) {
        EXPECT_NE(text.find(name), std::string::npos) << name;
    }

    EXPECT_NE(text.find("PR6 gate: blocked"), std::string::npos);
    EXPECT_NE(text.find("CollectiveReduceNodeMetal | unsupported"), std::string::npos);
    EXPECT_NE(text.find("CrsdFocusedImageTransformMetalNode | domain algorithm"), std::string::npos);
    EXPECT_NE(text.find("experimental incomplete"), std::string::npos);
}

TEST(MetalTruthInLabelingGuardrailTest, FocusedImageMetalNodeReportsExperimentalIncompleteStatus) {
    auto assembled = BuildAssembledFrame();
    ASSERT_TRUE(assembled.has_value());

    sar::CrsdFocusedImageTransformMetalNode node(
        sar::CrsdFocusedImageTransformMetalConfig{
            .image_width = 8,
            .image_height = 8,
            .execution_backend = "metal",
            .allow_fallback = false,
            .require_kernel_execution = true,
        });
    BindDefaultCapabilities(node);

    auto out = node.Transfer(
        *assembled,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});
    ASSERT_TRUE(out.has_value());

    EXPECT_NE(std::string(node.GetAlgorithmStatus()).find("experimental_incomplete"), std::string::npos);
    EXPECT_NE(node.GetLastDiagnostic().find("incomplete"), std::string::npos);

    auto params = node.GetParameters();
    auto status = params.TryGetString("algorithm_status");
    ASSERT_TRUE(status.has_value());
    EXPECT_NE(status.value().find("experimental_incomplete"), std::string::npos);

    auto complete_claim = params.TryGetBool("claims_complete_native_algorithm");
    ASSERT_TRUE(complete_claim.has_value());
    EXPECT_FALSE(complete_claim.value());
}

TEST(MetalTruthInLabelingGuardrailTest, FocusedImageMetalPluginDescriptorDeclaresExperimentalIncomplete) {
    const auto plugin_src = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
                            "examples/SAR/plugins/crsd_focused_image_transform_metal_node_plugin.cpp";
    const std::string text = ReadFile(plugin_src);

    EXPECT_NE(text.find("experimental"), std::string::npos);
    EXPECT_NE(text.find("algorithm incomplete"), std::string::npos);
}
