// SPDX-License-Identifier: MIT

/**
 * @file test_metal_truth_in_labeling_guardrails.cpp
 * @brief GraphX source file.
 */

#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
#include "gpu/metal/nodes/CollectiveReduceNodeMetal.hpp"
#include "graph/CapabilityBus.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

/**
 * @brief Read text.
 * @param path Parameter for read text.
 */
std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "unable to open " << path;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST(MetalTruthInLabelingGuardrailGpuTest, CollectiveReduceIsUnsupportedWithDefaultMetalCapability) {
    graph::CapabilityBus bus;
    auto collective = std::make_shared<graph::gpu::metal::capabilities::DefaultMetalCollectiveCapability>();
    bus.Register<graph::gpu::metal::capabilities::IMetalCollectiveCapability>(collective);

    graph::gpu::metal::nodes::CollectiveReduceNodeMetal node;
    ASSERT_TRUE(node.BindGpuCapabilities(bus));

    graph::gpu::accel::BufferLease lease{};
    lease.pool_id = 42;
    lease.allocation_id = 99;
    lease.release_policy = graph::gpu::accel::ReleasePolicy::Manual;
    lease.device_view.backend = graph::gpu::accel::BackendKind::Metal;
    lease.device_view.device_ptr = reinterpret_cast<void*>(0x5000);
    lease.device_view.bytes = 128;
    lease.device_view.dtype = graph::gpu::accel::DataType::UInt8;
    lease.device_view.layout.rank = 1;
    lease.device_view.layout.shape[0] = 128;
    lease.device_view.layout.stride[0] = 1;

    auto out = node.Transfer(
        lease.device_view,
        std::integral_constant<std::size_t, 0>{},
        std::integral_constant<std::size_t, 0>{});

    EXPECT_FALSE(out.has_value());
}

TEST(MetalTruthInLabelingGuardrailGpuTest, CollectiveReducePluginInfoDeclaresRuntimeUnsupported) {
    const auto plugin_src = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
                            "libgpu/plugins/metal_collective_reduce_node_plugin.cpp";
    const std::string text = ReadText(plugin_src);

    EXPECT_NE(text.find("runtime unsupported"), std::string::npos);
    EXPECT_NE(text.find("CollectiveReduceNodeMetal"), std::string::npos);
}

TEST(MetalTruthInLabelingGuardrailGpuTest, InventoryStatesNonKernelNodeClassesAreValidMetalNodes) {
    const auto doc = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
                     "docs/sar/metal_node_truth_in_labeling.md";
    const std::string text = ReadText(doc);

    EXPECT_NE(text.find("H2DAsyncNodeMetal | transfer"), std::string::npos);
    EXPECT_NE(text.find("QueueSyncNodeMetal | sync/control"), std::string::npos);
    EXPECT_NE(text.find("LeaseReleaseNodeMetal | memory"), std::string::npos);
    EXPECT_NE(text.find("Transfer/memory/sync/control nodes are valid Metal nodes without kernels"),
              std::string::npos);
}
