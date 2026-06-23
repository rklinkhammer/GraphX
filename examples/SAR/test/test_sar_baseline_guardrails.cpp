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

TEST(SarBaselineGuardrailTest, DocsNameExactlyOneCanonicalGpuPath) {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto active_docs =
      ReadFile(root / "README.md") + "\n" +
      ReadFile(root / "plan" / "BASELINE.md");

  const std::string canonical_path =
      "examples/SAR/config/sar_crsd_tiny_fixture_focused_image_metal.json";
  EXPECT_NE(active_docs.find("Current canonical SAR GPU path"),
            std::string::npos);
  EXPECT_NE(active_docs.find(canonical_path), std::string::npos);
  EXPECT_NE(active_docs.find("experimental"), std::string::npos);
  
  // PR7: After consolidation, verify no second canonical GPU path is mentioned
  EXPECT_EQ(active_docs.find("second canonical SAR GPU path is supported"),
            std::string::npos);
  EXPECT_EQ(active_docs.find("Other SAR Metal configs are development"),
            std::string::npos);
}

TEST(SarBaselineGuardrailTest, CanonicalGpuPathUsesAccelTokenContract) {
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

TEST(SarBaselineGuardrailTest, PR7_ConfigSetConsolidation) {
  // PR7: Verify SAR config set consolidation.
  // This test ensures that:
  // 1. Only active, named canonical configs remain in examples/SAR/config/
  // 2. Stale, duplicate, or experimental configs have been deleted
  // 3. The remaining configs are documented in README.md and BASELINE.md
  
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto config_dir = root / "examples" / "SAR" / "config";
  
  // Count actual configs on disk
  int config_count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(config_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      config_count++;
    }
  }
  
  // PR7: After consolidation, expect exactly 13 canonical/active configs
  // (down from 21 in the pre-PR7 state)
  EXPECT_EQ(config_count, 13) 
      << "SAR config consolidation should keep exactly 13 active configs; "
      << "deleted configs: sar_gotcha_external_manual, "
      << "sar_crsd_focused_image_tiny_fixture, sar_crsd_tiny_fixture_with_sink, "
      << "sar_crsd_real_directory_input_smoke, sar_crsd_real_paths_input_smoke, "
      << "sar_stripmap_metal_window, sar_stripmap_metal_compression, "
      << "sar_stripmap_metal_fanout";
  
  // Verify the canonical configs still exist
  const std::vector<std::string> canonical_configs{
      "sar_stripmap_simulated.json",
      "sar_crsd_tiny_fixture_focused_image_cpu.json",
      "sar_crsd_tiny_fixture_focused_image_metal.json",
      "sar_crsd_gotcha_local_validation.json",
  };
  
  for (const auto& config : canonical_configs) {
    const auto config_path = config_dir / config;
    EXPECT_TRUE(std::filesystem::exists(config_path))
        << "Canonical config must be present: " << config;
  }
  
  // Verify documentation references the consolidated set
  const auto readme = ReadFile(root / "README.md");
  const auto baseline = ReadFile(root / "plan" / "BASELINE.md");
  const auto docs = readme + "\n" + baseline;
  
  EXPECT_NE(docs.find("sar_stripmap_simulated.json"), std::string::npos);
  EXPECT_NE(docs.find("sar_crsd_tiny_fixture_focused_image_cpu.json"), 
            std::string::npos);
  EXPECT_NE(docs.find("sar_crsd_tiny_fixture_focused_image_metal.json"), 
            std::string::npos);
  EXPECT_NE(docs.find("sar_crsd_gotcha_local_validation.json"), 
            std::string::npos);
}

TEST(SarBaselineGuardrailTest, PR13_ExternalBaselineSurveyRemainsPlanningOnly) {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto baseline = ReadFile(root / "plan" / "BASELINE.md");

  EXPECT_NE(baseline.find("PR13 external SAR baseline survey status"),
            std::string::npos);
  EXPECT_NE(baseline.find("no external baseline package is integrated"),
            std::string::npos);
  EXPECT_NE(baseline.find("Recommendation status: clear deferral"),
            std::string::npos);
  EXPECT_NE(baseline.find("Any external baseline execution remains local-only"),
            std::string::npos);
  EXPECT_NE(baseline.find("Default CI remains external-dependency-free"),
            std::string::npos);
}

TEST(SarBaselineGuardrailTest,
     PR16_SubstitutionRemainsLocalOnlyAndGpuPathStaysSingular) {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto active_docs =
      ReadFile(root / "README.md") + "\n" +
      ReadFile(root / "plan" / "BASELINE.md");

  EXPECT_NE(active_docs.find("sar_baseline_substitution_experiment.py"),
            std::string::npos);
  EXPECT_NE(active_docs.find("GRAPHX_SAR_BASELINE_SUBSTITUTION_ENABLE"),
            std::string::npos);
  EXPECT_NE(active_docs.find("not a production SAR claim"),
            std::string::npos);
  EXPECT_NE(active_docs.find("does not create a second"),
            std::string::npos);
  EXPECT_NE(active_docs.find("canonical SAR GPU path"),
            std::string::npos);
}
