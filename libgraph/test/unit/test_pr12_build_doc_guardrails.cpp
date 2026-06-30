// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::filesystem::path RepositoryRoot() {
  auto path = std::filesystem::path(__FILE__).lexically_normal();
  while (!path.empty() && path.filename() != "libgraph") {
    path = path.parent_path();
  }
  return path.parent_path();
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "unable to read " << path;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::size_t CountOccurrences(const std::string &text, const std::string &needle) {
  if (needle.empty()) {
    return 0;
  }
  std::size_t count = 0;
  std::size_t cursor = 0;
  while (true) {
    const auto pos = text.find(needle, cursor);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    cursor = pos + needle.size();
  }
  return count;
}

void ExpectNotContains(const std::string &text, const std::string &needle) {
  EXPECT_EQ(text.find(needle), std::string::npos) << needle;
}

void ExpectContains(const std::string &text, const std::string &needle) {
  EXPECT_NE(text.find(needle), std::string::npos) << needle;
}

} // namespace

TEST(PR12BuildDocGuardrailTest, Cxx26EnforcementIsCentralizedAndModulePilotRemoved) {
  const auto root = RepositoryRoot();

  const auto root_cmake = ReadFile(root / "CMakeLists.txt");
  const auto graph_cmake = ReadFile(root / "libgraph" / "CMakeLists.txt");
  const auto gpu_cmake = ReadFile(root / "libgpu" / "CMakeLists.txt");
  const auto dsp_cmake = ReadFile(root / "libdsp" / "CMakeLists.txt");
  const auto sensor_cmake = ReadFile(root / "libsensor" / "CMakeLists.txt");

  EXPECT_EQ(CountOccurrences(root_cmake, "set(CMAKE_CXX_STANDARD 26)"), 1u);
  ExpectContains(root_cmake, "if(NOT CMAKE_CXX_STANDARD EQUAL 26)");
  ExpectNotContains(root_cmake, "GRAPHX_ENABLE_MODULE_PILOT");

  ExpectNotContains(graph_cmake, "set(CMAKE_CXX_STANDARD 26)");
  ExpectNotContains(gpu_cmake, "set(CMAKE_CXX_STANDARD 26)");
  ExpectNotContains(dsp_cmake, "set(CMAKE_CXX_STANDARD 26)");
  ExpectNotContains(sensor_cmake, "set(CMAKE_CXX_STANDARD 26)");

  ExpectNotContains(graph_cmake, "GRAPHX_ENABLE_MODULE_PILOT");
  EXPECT_FALSE(std::filesystem::exists(root / "libgraph" / "modules" / "graph.port_metadata.ixx"));
}

TEST(PR12BuildDocGuardrailTest, ActiveDocsUseMarkdownReviewInputsAndNoMalformedSimplifierFile) {
  const auto root = RepositoryRoot();

  const auto pr_agents = ReadFile(root / "plan" / "agents" / "GRAPHX_PR_AGENTS.md");
  ExpectContains(pr_agents, "plan/reviews/GRAPHX_SIMPLIFIER_REPORT.md");
  ExpectNotContains(pr_agents, "GRAPHX_SIMPLIFIER_REPORT.m`");

  EXPECT_TRUE(std::filesystem::exists(root / "plan" / "reviews" / "GRAPHX_INSPECTOR_REPORT.md"));
  EXPECT_TRUE(std::filesystem::exists(root / "plan" / "reviews" / "GRAPHX_SIMPLIFIER_REPORT.md"));
  EXPECT_FALSE(std::filesystem::exists(root / "plan" / "reviews" / "GRAPHX_SIMPLIFIER_REPORT.m"));

  EXPECT_FALSE(std::filesystem::exists(
      root / "plan" / "archive" / "2026-06-28-baseline" / "GRAPHX_SIMPLIFIER_REPORT.m"));
}

TEST(PR12BuildDocGuardrailTest, LibgraphUnitTargetDoesNotRepeatStaticLibraryLinks) {
  const auto root = RepositoryRoot();
  const auto test_cmake = ReadFile(root / "libgraph" / "test" / "CMakeLists.txt");
  ExpectNotContains(test_cmake, "target_link_libraries(test_libgraph_unit PRIVATE graph gpu graph");
}

TEST(PR12BuildDocGuardrailTest, GraphXPackageConfigDeclaresGpuDependencyBeforeDsp) {
  const auto root = RepositoryRoot();
  const auto graphx_config = ReadFile(root / "cmake" / "GraphXConfig.cmake.in");

  const auto gpu_pos = graphx_config.find("find_dependency(gpu CONFIG REQUIRED)");
  const auto dsp_pos = graphx_config.find("find_dependency(dsp CONFIG REQUIRED)");

  ASSERT_NE(gpu_pos, std::string::npos);
  ASSERT_NE(dsp_pos, std::string::npos);
  EXPECT_LT(gpu_pos, dsp_pos);
}

TEST(PR12BuildDocGuardrailTest, PackageSmokeConsumerUsesInstalledPublicHeaders) {
  const auto root = RepositoryRoot();
  const auto smoke_main = ReadFile(root / "tools" / "package_smoke_consumer" / "main.cpp");

  ExpectNotContains(smoke_main, "#include <graph/NodeFactory.hpp>");
  ExpectNotContains(smoke_main, "#include <core/VariantRouter.hpp>");
  ExpectNotContains(smoke_main, "#include <config/DataTypes.hpp>");
  ExpectContains(smoke_main, "#include <graph/Message.hpp>");
  ExpectContains(smoke_main, "#include <dsp/CpuSpectrumDftNode.hpp>");
}
