// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to read " << path;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

} // namespace

TEST(Pr13BackendSurfaceGuardrailTest,
     MetalCollectiveReduceNodeIsExplicitlyLabeledUnsupportedAcrossActiveSurfaces) {
    const auto root = std::filesystem::path{GRAPHX_SOURCE_ROOT};

    const auto readme = ReadText(root / "README.md");
    const auto plugin_src =
        ReadText(root / "libgpu/plugins/metal_collective_reduce_node_plugin.cpp");
    const auto plugin_cmake = ReadText(root / "libgpu/plugins/CMakeLists.txt");

    EXPECT_NE(readme.find("CollectiveReduceNodeMetal | unsupported"), std::string::npos);
    EXPECT_NE(plugin_src.find("runtime unsupported"), std::string::npos);
    EXPECT_NE(plugin_cmake.find("runtime unsupported"), std::string::npos);
}

TEST(Pr13BackendSurfaceGuardrailTest,
     NoCudaOrSyclCollectiveReduceNodePluginSurfaceExistsWithoutOwnedImplementation) {
    const auto root = std::filesystem::path{GRAPHX_SOURCE_ROOT};

    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/plugins/cuda_collective_reduce_node_plugin.cpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/plugins/sycl_collective_reduce_node_plugin.cpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/include/gpu/cuda/nodes/CollectiveReduceNodeCuda.hpp"));
    EXPECT_FALSE(std::filesystem::exists(
        root / "libgpu/include/gpu/sycl/nodes/CollectiveReduceNodeSycl.hpp"));
}

TEST(Pr13BackendSurfaceGuardrailTest,
     RetainedBackendSurfacesHaveRegisteredLibgpuTestLanes) {
    const auto root = std::filesystem::path{GRAPHX_SOURCE_ROOT};
    const auto cmake_text = ReadText(root / "libgpu/test/CMakeLists.txt");

    EXPECT_NE(cmake_text.find("add_test(NAME libgpu_stub_unit"), std::string::npos);
    EXPECT_NE(cmake_text.find("add_test(NAME libgpu_backend_unit"), std::string::npos);
    EXPECT_NE(cmake_text.find("add_test(NAME libgpu_metal_runtime"), std::string::npos);
}
