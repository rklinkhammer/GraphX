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

void ExpectContains(const std::string &text, const std::string &needle) {
  EXPECT_NE(text.find(needle), std::string::npos) << needle;
}

void ExpectNotContains(const std::string &text, const std::string &needle) {
  EXPECT_EQ(text.find(needle), std::string::npos) << needle;
}

} // namespace

TEST(BaselineArchitectureGuardrailTest, ActiveDocsNameBaselineAndCompletedRoadmap) {
  const auto root = RepositoryRoot();
  const auto readme = root / "README.md";
  const auto baseline = root / "plan" / "BASELINE.md";
  const auto roadmap = root / "plan" / "roadmap" / "GRAPHX_PR_ROADMAP.md";
  const auto agents = root / "plan" / "agents" / "GRAPHX_AGENT_ROLES.md";

  ASSERT_TRUE(std::filesystem::exists(readme));
  ASSERT_TRUE(std::filesystem::exists(baseline));
  ASSERT_TRUE(std::filesystem::exists(roadmap));
  ASSERT_TRUE(std::filesystem::exists(agents));

  const auto active_docs = ReadFile(readme) + "\n" + ReadFile(baseline);
  ExpectContains(active_docs, "plan/BASELINE.md");
  ExpectContains(active_docs, "plan/roadmap/GRAPHX_PR_ROADMAP.md");
  ExpectContains(active_docs, "plan/agents/GRAPHX_AGENT_ROLES.md");
  ExpectContains(active_docs, "plan/archive/2026-06-baseline/");
  ExpectContains(active_docs, "docs/archive/2026-06-baseline/");
  ExpectContains(active_docs, "archived");
  ExpectContains(active_docs, "historical");
  ExpectContains(active_docs, "completed cleanup roadmap");

  ExpectNotContains(active_docs, "Use archived PR plans as active scope");
  ExpectNotContains(active_docs, "Archived PR plans are active scope");
  ExpectNotContains(active_docs, "Historical roadmaps are active scope");
}

TEST(BaselineArchitectureGuardrailTest,
     CompletedRoadmapNamesEveryImplementedAndVerifiedPr) {
  const auto root = RepositoryRoot();
  const auto roadmap =
      ReadFile(root / "plan" / "roadmap" / "GRAPHX_PR_ROADMAP.md");
  ExpectContains(roadmap, "Status: Proposed; no implementation authorized.");

  EXPECT_TRUE(std::filesystem::exists(
    root / "plan" / "reviews" / "GRAPHX_INSPECTOR_REPORT.md"));
  EXPECT_TRUE(std::filesystem::exists(
    root / "plan" / "reviews" / "GRAPHX_SIMPLIFIER_REPORT.md"));
  EXPECT_FALSE(std::filesystem::exists(
    root / "plan" / "reviews" / "GRAPHX_IMPL_PR12.md"));
  EXPECT_FALSE(std::filesystem::exists(
    root / "plan" / "reviews" / "GRAPHX_VERIFY_PR12.md"));
}

TEST(BaselineArchitectureGuardrailTest, ActiveDocsPreserveCoreGraphXInvariants) {
  const auto root = RepositoryRoot();
  const auto active_docs =
      ReadFile(root / "README.md") + "\n" +
      ReadFile(root / "plan" / "BASELINE.md");

  ExpectContains(active_docs, "GraphExecutorBuilder");
  ExpectContains(active_docs, "real GraphX node");
  ExpectContains(active_docs, "graph::gpu::accel::ControlToken<");
  ExpectContains(active_docs, "Do not add new active plan roadmaps");
  ExpectContains(active_docs, "truth-in-labeling");
  ExpectContains(active_docs, "local-only");
  ExpectContains(active_docs, "experimental");
  ExpectContains(active_docs, "fixture");
  ExpectContains(active_docs, "production-like");
}

TEST(BaselineArchitectureGuardrailTest,
     TypedFixedFanNodeIsTheOnlyRepeatedPortMechanism) {
  const auto root = RepositoryRoot();
  const auto graph_include = root / "libgraph" / "include" / "graph";
  const auto canonical = graph_include / "TypedFixedFanNode.hpp";

  ASSERT_TRUE(std::filesystem::exists(canonical));
  EXPECT_FALSE(
      std::filesystem::exists(graph_include / "FixedFanInOutNode.hpp"));
  EXPECT_FALSE(std::filesystem::exists(graph_include / "RoutedFunctions.hpp"));

  const auto text = ReadFile(canonical);
  EXPECT_NE(text.find("class TypedFixedFanNode"), std::string::npos);
  EXPECT_EQ(text.find("NamedFixedFanInOutNode"), std::string::npos);
  EXPECT_EQ(text.find("RoutedInputFn"), std::string::npos);
  EXPECT_EQ(text.find("RoutedOutputFn"), std::string::npos);
  EXPECT_EQ(text.find("RoutedTransferFn"), std::string::npos);
}

TEST(BaselineArchitectureGuardrailTest,
     StaticNodeAdapterIsDeletedAndProviderHasNoCompatibilityHook) {
  const auto root = RepositoryRoot();
  const auto graph_include = root / "libgraph" / "include" / "graph";
  const auto graph_src = root / "libgraph" / "src" / "graph";

  EXPECT_FALSE(std::filesystem::exists(graph_include / "StaticNodeAdapter.hpp"));
  EXPECT_FALSE(std::filesystem::exists(graph_src / "StaticNodeAdapter.cpp"));

  const auto provider_header =
      ReadFile(graph_include / "RegisteredNodeProvider.hpp");
  const auto provider_source =
      ReadFile(graph_src / "RegisteredNodeProvider.cpp");

  ExpectNotContains(provider_header, "RegisterStaticNodes");
  ExpectNotContains(provider_source, "RegisterStaticNodes");
  ExpectNotContains(provider_source, "StaticNodeAdapter");
}
