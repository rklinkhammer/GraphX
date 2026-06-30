// SPDX-License-Identifier: MIT

/**
 * @file test_metal_truth_in_labeling_guardrails.cpp
 * @brief Test Metal Truth In Labeling Guardrails GPU acceleration support.
 *
 * @details Provides GPU test coverage for accelerator contracts and runtime behavior. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>

#include "gpu/accel/types/AccelValidation.hpp"
#include "gpu/metal/capabilities/DefaultMetalCapabilities.hpp"
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

TEST(MetalTruthInLabelingGuardrailGpuTest, UnsupportedCollectiveNodeSurfaceWasDeleted) {
    const auto root = std::filesystem::path{GRAPHX_SOURCE_ROOT};
    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/include/gpu/metal/nodes/CollectiveReduceNodeMetal.hpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/plugins/metal_collective_reduce_node_plugin.cpp"));
}

TEST(MetalTruthInLabelingGuardrailGpuTest, InventoryStatesNonKernelNodeClassesAreValidMetalNodes) {
    const auto doc = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
                     "README.md";
    const std::string text = ReadText(doc);

    EXPECT_NE(text.find("H2DAsyncNodeMetal | transfer"), std::string::npos);
    EXPECT_NE(text.find("QueueSyncNodeMetal | sync/control"), std::string::npos);
    EXPECT_NE(text.find("LeaseReleaseNodeMetal | memory"), std::string::npos);
    EXPECT_NE(text.find("Transfer/memory/sync/control nodes are valid Metal nodes without kernels"),
              std::string::npos);
}
