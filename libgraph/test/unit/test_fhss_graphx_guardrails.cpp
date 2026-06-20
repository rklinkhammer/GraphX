// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::vector<std::filesystem::path>
FilesMatching(const std::filesystem::path &root, const std::regex &pattern) {
  std::vector<std::filesystem::path> files;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    if (std::regex_match(path.filename().string(), pattern)) {
      files.push_back(path);
    }
  }
  return files;
}

std::vector<std::string> ClassNamesMatching(const std::string &text,
                                            const std::regex &pattern) {
  std::vector<std::string> names;
  for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
       it != std::sregex_iterator(); ++it) {
    names.push_back((*it)[1].str());
  }
  return names;
}

TEST(FHSSGraphXGuardrailTest,
     UnifiedFhssGraphXNodeDefinitionHeaderWasDeleted) {
  const auto root = RepositoryRoot();
  const auto fhss_include = root / "libdsp" / "include" / "dsp" / "fhss";

  EXPECT_FALSE(std::filesystem::exists(fhss_include / "FHSSGraphXNodes.hpp"));
  EXPECT_FALSE(std::filesystem::exists(root / "libdsp" / "src" / "dsp" /
                                       "FHSSGraphXNodes.cpp"));
}

TEST(FHSSGraphXGuardrailTest, EachFhssGraphXNodeHasOwnHeaderAndSource) {
  const auto root = RepositoryRoot();
  const auto fhss_include = root / "libdsp" / "include" / "dsp" / "fhss";
  const auto dsp_src = root / "libdsp" / "src" / "dsp";
  const std::unordered_map<std::string, std::string> expected_headers{
      {"FHSSSyntheticIqSourceNode", "FHSSSyntheticIqSourceNode.hpp"},
      {"FHSSCorrelatorBankDetectorNode",
       "FHSSCorrelatorBankDetectorNode.hpp"},
      {"FHSSPulseMergeNode", "FHSSPulseMergeNode.hpp"},
      {"FHSSPulseCandidateNode", "FHSSPulseCandidateNode.hpp"},
      {"CPSMBranchMetricNode", "CPSMBranchMetricNode.hpp"},
      {"CPSMViterbiDecoderNode", "CPSMViterbiDecoderNode.hpp"},
      {"FHSSPulseWordDecoderNode", "FHSSPulseWordDecoderNode.hpp"},
      {"FHSSPreambleDetectorNode", "FHSSPreambleDetectorNode.hpp"},
      {"FHSSMessageAssemblerNode", "FHSSMessageAssemblerNode.hpp"},
      {"FHSSMessageSinkNode", "FHSSMessageSinkNode.hpp"},
  };
  const std::regex node_class_regex(
      R"(\b(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*Node)\b)");

  for (const auto &[class_name, header_name] : expected_headers) {
    const auto header = fhss_include / header_name;
    const auto source = dsp_src / (class_name + ".cpp");
    ASSERT_TRUE(std::filesystem::exists(header)) << header;
    ASSERT_TRUE(std::filesystem::exists(source)) << source;
    const auto text = ReadFile(header);
    const auto node_classes = ClassNamesMatching(text, node_class_regex);
    ASSERT_EQ(node_classes.size(), 1u) << header;
    EXPECT_EQ(node_classes.front(), class_name) << header;
  }
}

TEST(FHSSGraphXGuardrailTest,
     PublicFhssNodeClassesExistOnlyInPerNodeHeaders) {
  const auto root = RepositoryRoot();
  const auto fhss_include = root / "libdsp" / "include" / "dsp" / "fhss";
  const std::unordered_set<std::string> allowed_node_headers{
      "FHSSSyntheticIqSourceNode.hpp",
      "FHSSCorrelatorBankDetectorNode.hpp",
      "FHSSPulseMergeNode.hpp",
      "FHSSPulseCandidateNode.hpp",
      "CPSMBranchMetricNode.hpp",
      "CPSMViterbiDecoderNode.hpp",
      "FHSSPulseWordDecoderNode.hpp",
      "FHSSPreambleDetectorNode.hpp",
      "FHSSMessageAssemblerNode.hpp",
      "FHSSMessageSinkNode.hpp",
  };
  const std::regex node_class_regex(
      R"(\b(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*Node)\b)");

  for (const auto &header :
       FilesMatching(fhss_include, std::regex(R"(.*\.hpp)"))) {
    const auto text = ReadFile(header);
    const auto node_classes = ClassNamesMatching(text, node_class_regex);
    if (allowed_node_headers.contains(header.filename().string())) {
      EXPECT_FALSE(node_classes.empty())
          << header << " should define exactly one FHSS GraphX node";
      continue;
    }
    EXPECT_TRUE(node_classes.empty())
        << header << " must not define public FHSS GraphX node classes";
  }
}

TEST(FHSSGraphXGuardrailTest, FhssNodeClassesInheritGraphXNodeBases) {
  const auto root = RepositoryRoot();
  const auto fhss_include = root / "libdsp" / "include" / "dsp" / "fhss";
  const std::unordered_set<std::string> node_headers{
      "FHSSSyntheticIqSourceNode.hpp",
      "FHSSCorrelatorBankDetectorNode.hpp",
      "FHSSPulseMergeNode.hpp",
      "FHSSPulseCandidateNode.hpp",
      "CPSMBranchMetricNode.hpp",
      "CPSMViterbiDecoderNode.hpp",
      "FHSSPulseWordDecoderNode.hpp",
      "FHSSPreambleDetectorNode.hpp",
      "FHSSMessageAssemblerNode.hpp",
      "FHSSMessageSinkNode.hpp",
  };
  const std::regex node_class_regex(R"(\bclass\s+([A-Za-z_][A-Za-z0-9_]*Node)\b)");

  for (const auto &header_name : node_headers) {
    const auto header = fhss_include / header_name;
    const auto text = ReadFile(header);
    const auto node_classes = ClassNamesMatching(text, node_class_regex);
    ASSERT_EQ(node_classes.size(), 1u) << header;
    const auto &name = node_classes.front();
    const std::regex real_graphx_node_regex(
        "class\\s+" + name +
            R"(\s*:[\s\S]*public\s+graph::Named(?:Source|Interior|Sink)Node)",
        std::regex::ECMAScript);
    EXPECT_TRUE(std::regex_search(text, real_graphx_node_regex))
        << name << " must inherit a repository GraphX node base";
  }
}

TEST(FHSSGraphXGuardrailTest, SharedFhssGraphXUtilityDefinesNoNodeClasses) {
  const auto root = RepositoryRoot();
  const auto util =
      root / "libdsp" / "include" / "dsp" / "fhss" /
      "FHSSGraphXNodeUtils.hpp";
  ASSERT_TRUE(std::filesystem::exists(util));
  const auto text = ReadFile(util);
  const std::regex node_class_regex(
      R"(\b(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*Node)\b)");
  EXPECT_TRUE(ClassNamesMatching(text, node_class_regex).empty());
}

TEST(FHSSGraphXGuardrailTest, FhssTestsDoNotCallDeletedPseudoNodeApis) {
  const auto root = RepositoryRoot();
  const auto unit_tests = root / "libgraph" / "test" / "unit";
  const std::regex fhss_test_regex(R"(test_fhss_.*\.cpp)");
  const std::regex deleted_pseudo_node_api_regex(
      R"(\b(?:FHSS|CPSM)[A-Za-z0-9_]*Node::(?:Detect|Merge|Decode|Assemble|Diagnostics|ScoreBranch|Compute)\b)");
  const std::regex deleted_pseudo_node_header_regex(
      R"(#include\s+"dsp/fhss/FHSSGraphXNodes\.hpp")");

  for (const auto &test_file : FilesMatching(unit_tests, fhss_test_regex)) {
    const auto text = ReadFile(test_file);
    EXPECT_FALSE(std::regex_search(text, deleted_pseudo_node_api_regex))
        << test_file << " must not call deleted pre-GraphX pseudo-node APIs";
    EXPECT_FALSE(std::regex_search(text, deleted_pseudo_node_header_regex))
        << test_file << " must not include the deleted unified FHSS GraphX "
        << "node header";
  }
}

} // namespace
