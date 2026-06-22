// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

#ifndef SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON
#define SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON                              \
  "examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json"
#endif

namespace {

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "unable to read " << path;
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "unable to read " << path;
  nlohmann::json json;
  input >> json;
  return json;
}

} // namespace

TEST(SarBaselineGuardrailTest, DocsNameExactlyOneCanonicalGpuPathCandidate) {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto active_docs =
      ReadFile(root / "README.md") + "\n" +
      ReadFile(root / "plan" / "BASELINE.md");

  const std::string candidate =
      "examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json";
  EXPECT_NE(active_docs.find("Current canonical SAR GPU-path candidate"),
            std::string::npos);
  EXPECT_NE(active_docs.find(candidate), std::string::npos);
  EXPECT_NE(active_docs.find("experimental/incomplete"), std::string::npos);
  EXPECT_NE(active_docs.find("They are not a second canonical"),
            std::string::npos);

  EXPECT_EQ(active_docs.find("second canonical SAR GPU path is supported"),
            std::string::npos);
  EXPECT_EQ(active_docs.find("multiple canonical SAR GPU paths"),
            std::string::npos);
}

TEST(SarBaselineGuardrailTest, CanonicalGpuPathCandidateUsesAccelTokenContract) {
  const auto config =
      LoadJson(std::filesystem::path(SAR_CRSD_TINY_FOCUSED_IMAGE_METAL_CONFIG_JSON));

  EXPECT_EQ(config.at("name").get<std::string>(),
            "sar_crsd_tiny_fixture_focused_image_metal");
  EXPECT_EQ(config.at("execution_backend").get<std::string>(), "metal");
  EXPECT_EQ(config.at("edge_contract").get<std::string>(), "accel-token");
  ASSERT_TRUE(config.contains("resolver_mappings"));
  ASSERT_FALSE(config.at("resolver_mappings").empty());

  for (const auto &mapping : config.at("resolver_mappings")) {
    EXPECT_EQ(mapping.at("input_token_type").get<std::string>(),
              "SarAccelControlToken");
    EXPECT_EQ(mapping.at("output_token_type").get<std::string>(),
              "SarAccelControlToken");
  }
}
