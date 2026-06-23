// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_BASELINE_SUBSTITUTION_EXPERIMENT_PATH
#define SAR_BASELINE_SUBSTITUTION_EXPERIMENT_PATH                         \
  "examples/SAR/tools/sar_baseline_substitution_experiment.py"
#endif

#ifndef SAR_GRAPHX_VS_BASELINE_HARNESS_PATH
#define SAR_GRAPHX_VS_BASELINE_HARNESS_PATH                               \
  "examples/SAR/tools/sar_graphx_vs_baseline_harness.py"
#endif

std::string Quote(const std::filesystem::path &path) {
  return "'" + path.string() + "'";
}

void WriteFloat32Raster(const std::filesystem::path &path,
                        const std::vector<float> &values) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(output.good());
  for (const auto value : values) {
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
  }
}

void WriteJson(const std::filesystem::path &path,
               const nlohmann::json &value) {
  std::ofstream output(path);
  ASSERT_TRUE(output.good());
  output << value.dump(2) << '\n';
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good());
  nlohmann::json value;
  input >> value;
  return value;
}

nlohmann::json Contract(const std::filesystem::path &raw_path,
                        const std::string &source_tool,
                        const std::string &provenance_class) {
  return {{"source_tool", source_tool},
          {"provenance_class", provenance_class},
          {"scenario_id", "scenario_001"},
          {"format", "float32_raster"},
          {"layout", "row_major"},
          {"artifact_kind", "materialized_image"},
          {"dtype", "float32"},
          {"width", 4},
          {"height", 4},
          {"byte_count", 64},
          {"raw_path", raw_path.string()}};
}

} // namespace

TEST(SarBaselineSubstitutionExperimentTest,
     LocalSubstitutionSkipsWithoutExplicitOptIn) {
  const auto root = std::filesystem::temp_directory_path() /
                    "graphx_sar_substitution_skip";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  const auto output = root / "result.json";
  const std::string command =
      "GRAPHX_SAR_BASELINE_SUBSTITUTION_ENABLE=0 python3 " +
      Quote(SAR_BASELINE_SUBSTITUTION_EXPERIMENT_PATH) +
      " run-local-substitution --graphx-stage-contract " +
      Quote(root / "missing_graphx.json") +
      " --baseline-reference-contract " +
      Quote(root / "missing_baseline.json") + " --output-json " +
      Quote(output) + " > /dev/null";

  ASSERT_EQ(std::system(command.c_str()), 0);
  const auto report = LoadJson(output);
  EXPECT_EQ(report.at("status"), "skipped");
  EXPECT_EQ(report.at("reason"), "local_opt_in_not_enabled");
  EXPECT_TRUE(report.at("local_only").get<bool>());
  EXPECT_FALSE(report.at("canonical_sar_gpu_path_changed").get<bool>());
}

TEST(SarBaselineSubstitutionExperimentTest,
     EnabledSubstitutionReportsComparisonMetrics) {
  const auto root = std::filesystem::temp_directory_path() /
                    "graphx_sar_substitution_enabled";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  const std::vector<float> pixels{
      0.0f, 0.1f, 0.0f, 0.0f, 0.2f, 1.0f, 0.2f, 0.0f,
      0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const auto graphx_raw = root / "graphx.bin";
  const auto baseline_raw = root / "sarpy.bin";
  WriteFloat32Raster(graphx_raw, pixels);
  WriteFloat32Raster(baseline_raw, pixels);

  const auto graphx_contract = root / "graphx.json";
  const auto baseline_contract = root / "sarpy.json";
  WriteJson(graphx_contract,
            Contract(graphx_raw, "graphx", "graphx_runtime"));
  WriteJson(baseline_contract,
            Contract(baseline_raw, "sarpy", "external_baseline"));

  const auto output = root / "result.json";
  const std::string command =
      "GRAPHX_SAR_BASELINE_SUBSTITUTION_ENABLE=1 python3 " +
      Quote(SAR_BASELINE_SUBSTITUTION_EXPERIMENT_PATH) +
      " run-local-substitution --graphx-stage-contract " +
      Quote(graphx_contract) + " --baseline-reference-contract " +
      Quote(baseline_contract) + " --output-json " + Quote(output) +
      " --strict > /dev/null";

  ASSERT_EQ(std::system(command.c_str()), 0);
  const auto report = LoadJson(output);
  EXPECT_EQ(report.at("status"), "pass");
  EXPECT_EQ(report.at("selected_baseline"), "SarPy");
  EXPECT_EQ(report.at("substituted_stage"), "image_formation");
  EXPECT_EQ(report.at("graphx_replacement"),
            "CrsdFocusedImageTransformNode");
  EXPECT_EQ(report.at("comparison_report").at("verdict"), "pass");
  EXPECT_FALSE(report.at("production_sar_claim").get<bool>());
  EXPECT_FALSE(report.at("canonical_sar_gpu_path_changed").get<bool>());
}

TEST(SarBaselineSubstitutionExperimentTest,
     ExistingComparisonHarnessStillRunsCiTinyFixture) {
  const auto root = std::filesystem::temp_directory_path() /
                    "graphx_sar_substitution_regression";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  const auto output = root / "result.json";
  const std::string command =
      "python3 " + Quote(SAR_GRAPHX_VS_BASELINE_HARNESS_PATH) +
      " run-ci-tiny-fixture --output-dir " + Quote(root / "artifacts") +
      " --output-json " + Quote(output) + " --strict > /dev/null";
  ASSERT_EQ(std::system(command.c_str()), 0);
  EXPECT_EQ(LoadJson(output).at("status"), "pass");
}
