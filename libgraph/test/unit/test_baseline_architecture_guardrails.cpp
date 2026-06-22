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

TEST(BaselineArchitectureGuardrailTest, ActiveDocsNameCurrentBaselineAndRoadmap) {
  const auto root = RepositoryRoot();
  const auto readme = root / "README.md";
  const auto baseline = root / "plan" / "BASELINE.md";
  const auto roadmap = root / "plan" / "roadmap" / "GRAPHX_PR_ROADMAP.md";
  const auto agents = root / "plan" / "agents" / "GRAPHX_PR_AGENTS.md";

  ASSERT_TRUE(std::filesystem::exists(readme));
  ASSERT_TRUE(std::filesystem::exists(baseline));
  ASSERT_TRUE(std::filesystem::exists(roadmap));
  ASSERT_TRUE(std::filesystem::exists(agents));

  const auto active_docs = ReadFile(readme) + "\n" + ReadFile(baseline);
  ExpectContains(active_docs, "plan/BASELINE.md");
  ExpectContains(active_docs, "plan/roadmap/GRAPHX_PR_ROADMAP.md");
  ExpectContains(active_docs, "plan/agents/GRAPHX_PR_AGENTS.md");
  ExpectContains(active_docs, "plan/archive/2026-06-baseline/");
  ExpectContains(active_docs, "docs/archive/2026-06-baseline/");
  ExpectContains(active_docs, "archived");
  ExpectContains(active_docs, "historical");

  ExpectNotContains(active_docs, "Use archived PR plans as active scope");
  ExpectNotContains(active_docs, "Archived PR plans are active scope");
  ExpectNotContains(active_docs, "Historical roadmaps are active scope");
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
