// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

TEST(PR11DashboardBoundaryGuardrailTest,
     CoreDashboardSurfaceHasNoFhssNamedHeadersOrSchemas) {
  const auto root = RepositoryRoot();
  const auto dashboard_include =
      root / "libgraph" / "include" / "graph" / "dashboard";
  const auto dashboard_src = root / "libgraph" / "src" / "dashboard";

  ASSERT_TRUE(std::filesystem::exists(dashboard_include));
  ASSERT_TRUE(std::filesystem::exists(dashboard_src));

  EXPECT_FALSE(std::filesystem::exists(dashboard_include / "FHSSScenarioController.hpp"));
  EXPECT_FALSE(std::filesystem::exists(dashboard_include / "FHSSStepping.hpp"));
  EXPECT_FALSE(std::filesystem::exists(dashboard_src / "FHSSScenarioController.cpp"));

  for (const auto &entry : std::filesystem::directory_iterator(dashboard_include)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto text = ReadFile(entry.path());
    ExpectNotContains(text, "FHSS");
    ExpectNotContains(text, "graphx.dashboard.fhss");
    ExpectNotContains(text, "/api/v1/fhss");
  }

  for (const auto &entry : std::filesystem::directory_iterator(dashboard_src)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto text = ReadFile(entry.path());
    ExpectNotContains(text, "FHSS");
    ExpectNotContains(text, "graphx.dashboard.fhss");
    ExpectNotContains(text, "/api/v1/fhss");
  }
}

TEST(PR11DashboardBoundaryGuardrailTest,
     FhssDashboardApplicationUsesGraphExecutorBuilderPath) {
  const auto root = RepositoryRoot();
  const auto fhss_demo = root / "examples" / "DSP" / "src" / "fhss_demo.cpp";

  ASSERT_TRUE(std::filesystem::exists(fhss_demo));
  const auto text = ReadFile(fhss_demo);

  ExpectContains(text, "GraphExecutorBuilder");
  ExpectContains(text, ".Build()");
}

TEST(PR11DashboardBoundaryGuardrailTest,
     CoreRuntimeSessionDoesNotOwnSeparateExecutorLifecycleHooks) {
  const auto root = RepositoryRoot();
  const auto runtime_session =
      root / "libgraph" / "include" / "graph" / "dashboard" /
      "GraphRuntimeSession.hpp";

  ASSERT_TRUE(std::filesystem::exists(runtime_session));
  const auto text = ReadFile(runtime_session);

  ExpectNotContains(text, "SetStartHandler");
  ExpectNotContains(text, "SetStopHandler");
  ExpectNotContains(text, "CommandHandler");
}
