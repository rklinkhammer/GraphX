// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::size_t LineCount(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << path;
    std::size_t count = 0;
    std::string line;
    while (std::getline(input, line)) ++count;
    return count;
}

TEST(Pr14MetalDecompositionGuardrailTest,
     NativeMetalImplementationIsSplitByResponsibility) {
    const auto dir = std::filesystem::path{GRAPHX_SOURCE_ROOT} /
        "libgpu/src/gpu/metal/native";
    EXPECT_FALSE(std::filesystem::exists(dir / "NativeMetalCapabilities.cpp"));
    const std::vector<std::string> units{
        "NativeMetalContext.cpp", "NativeMetalMemory.cpp",
        "NativeMetalTransfer.cpp", "NativeMetalKernel.cpp",
        "NativeMetalDiagnostics.cpp", "NativeMetalCollective.cpp"};
    for (const auto& unit : units) {
        const auto path = dir / unit;
        EXPECT_TRUE(std::filesystem::exists(path)) << path;
        EXPECT_LT(LineCount(path), 500u) << path;
    }
}

} // namespace
