// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

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

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  nlohmann::json json;
  input >> json;
  return json;
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

std::string Lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return text;
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
      {"FHSSDownconverterNode", "FHSSDownconverterNode.hpp"},
      {"ChannelizerNode", "ChannelizerNode.hpp"},
      {"PerChannelPulseDetectorNode", "PerChannelPulseDetectorNode.hpp"},
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
      "FHSSDownconverterNode.hpp",
      "ChannelizerNode.hpp",
      "PerChannelPulseDetectorNode.hpp",
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
      "FHSSDownconverterNode.hpp",
      "ChannelizerNode.hpp",
      "PerChannelPulseDetectorNode.hpp",
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
    if (name == "ChannelizerNode") {
      EXPECT_NE(text.find("public graph::SinkNode"), std::string::npos)
          << name << " must consume a GraphX input port";
      EXPECT_NE(text.find("public FHSSChannelizerSourceBase"),
                std::string::npos)
          << name << " must expose GraphX output ports";
      continue;
    }
    if (name == "FHSSPulseMergeNode") {
      EXPECT_NE(text.find("public FHSSPulseMergeSinkBase"),
                std::string::npos)
          << name << " must consume GraphX input ports";
      EXPECT_NE(text.find("public graph::SourceNode"), std::string::npos)
          << name << " must expose GraphX output ports";
      continue;
    }
    const std::regex real_graphx_node_regex(
        "class\\s+" + name +
            R"(\s*:[\s\S]*public\s+graph::Named(?:Source|Interior|Sink)Node)",
        std::regex::ECMAScript);
    EXPECT_TRUE(std::regex_search(text, real_graphx_node_regex))
        << name << " must inherit a repository GraphX node base";
  }
}

TEST(FHSSGraphXGuardrailTest,
     ChannelizerDoesNotExposeAggregateChannelOutputContract) {
  const auto root = RepositoryRoot();
  const auto fhss_include = root / "libdsp" / "include" / "dsp" / "fhss";
  const auto packets = ReadFile(fhss_include / "FHSSGraphXPackets.hpp");
  const auto utils = ReadFile(fhss_include / "FHSSGraphXNodeUtils.hpp");
  const auto channelizer = ReadFile(fhss_include / "ChannelizerNode.hpp");

  EXPECT_EQ(packets.find("FHSSChannelizedIqStreamPacket"), std::string::npos);
  EXPECT_EQ(utils.find("FHSSChannelizedIqStreamToken"), std::string::npos);
  EXPECT_EQ(channelizer.find("FHSSChannelizedIqStream"), std::string::npos);
  EXPECT_EQ(channelizer.find("std::vector<FHSSChannelizedIqPacket>"),
            std::string::npos);
  EXPECT_NE(channelizer.find("FHSSProtocolConstants::kFrequencyCount"),
            std::string::npos);
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

TEST(FHSSGraphXGuardrailTest,
     FhssDocsDescribeBasebandOffsetRfMetadataBoundary) {
  const auto root = RepositoryRoot();
  const auto doc = root / "docs" / "dsp" / "fhss_decoder.md";
  ASSERT_TRUE(std::filesystem::exists(doc));
  const auto text = Lowercase(ReadFile(doc));

  EXPECT_NE(text.find("1 ghz rf frequencies are metadata"),
            std::string::npos);
  EXPECT_NE(text.find("baseband/if offset"), std::string::npos);
  EXPECT_NE(text.find("500 msps"), std::string::npos);
  EXPECT_NE(text.find("cannot represent the full 64-entry 1 ghz rf"),
            std::string::npos);
  EXPECT_NE(text.find("as direct sampled rf"), std::string::npos);
  EXPECT_EQ(text.find("uses direct 1 ghz rf sampling"), std::string::npos);
  EXPECT_EQ(text.find("implements direct 1 ghz rf sampling"),
            std::string::npos);
}

TEST(FHSSGraphXGuardrailTest,
     FhssDocsKeepCpuOnlyFixtureAndFutureBoundariesHonest) {
  const auto root = RepositoryRoot();
  const auto doc = root / "docs" / "dsp" / "fhss_decoder.md";
  ASSERT_TRUE(std::filesystem::exists(doc));
  const auto text = Lowercase(ReadFile(doc));

  EXPECT_NE(text.find("cpu-only"), std::string::npos);
  EXPECT_NE(text.find("production channelizer separation claims"),
            std::string::npos);
  EXPECT_NE(text.find("deterministic cpu fixture channelizer"),
            std::string::npos);
  EXPECT_NE(text.find("doppler, noise, multipath"), std::string::npos);
  EXPECT_NE(text.find("overlap is unsupported"), std::string::npos);
  EXPECT_NE(text.find("metal/gpu acceleration of the fhss lane"),
            std::string::npos);
  EXPECT_NE(text.find("pdw diagnostics as canonical decoder output"),
            std::string::npos);

  EXPECT_EQ(text.find("is a production rf receiver"), std::string::npos);
  EXPECT_EQ(text.find("provides production rf compatibility"),
            std::string::npos);
  EXPECT_EQ(text.find("is external waveform compatible"), std::string::npos);
  EXPECT_EQ(text.find("is gpu accelerated"), std::string::npos);
  EXPECT_EQ(text.find("is overlap-aware"), std::string::npos);
  EXPECT_EQ(text.find("is doppler/noise-capable"), std::string::npos);
  EXPECT_EQ(text.find("is pdw-driven"), std::string::npos);
}

TEST(FHSSGraphXGuardrailTest,
     FhssDocsIdentifyGraphXNodesAsCanonicalModel) {
  const auto root = RepositoryRoot();
  const auto doc = root / "docs" / "dsp" / "fhss_decoder.md";
  ASSERT_TRUE(std::filesystem::exists(doc));
  const auto text = Lowercase(ReadFile(doc));

  EXPECT_NE(text.find("canonical fhss implementation uses real graphx nodes"),
            std::string::npos);
  EXPECT_NE(text.find("controltoken"), std::string::npos);
  EXPECT_NE(text.find("deleted pre-graphx pseudo-node"), std::string::npos);
  EXPECT_NE(text.find("scaffolding is not the current node model"),
            std::string::npos);
  EXPECT_NE(text.find("not the current node model"), std::string::npos);
}

TEST(FHSSGraphXGuardrailTest, FhssCanonicalGraphConfigIsChannelized) {
  const auto root = RepositoryRoot();
  const auto config_dir = root / "libdsp" / "config";
  const auto canonical =
      config_dir / "fhss_cpsm_channelized_fixture_500msps.json";
  const auto reference = config_dir / "fhss_cpsm_fixture_500msps.json";
  ASSERT_TRUE(std::filesystem::exists(canonical));
  ASSERT_TRUE(std::filesystem::exists(reference));

  const auto canonical_json = LoadJson(canonical);
  EXPECT_EQ(canonical_json.at("fhss_graph_role").get<std::string>(),
            "canonical_channelized_fixture");
  EXPECT_TRUE(canonical_json.at("canonical_fhss_graph").get<bool>());
  EXPECT_FALSE(canonical_json.at("reference_only").get<bool>());

  const auto reference_json = LoadJson(reference);
  EXPECT_EQ(reference_json.at("fhss_graph_role").get<std::string>(),
            "reference_correlator_bank_fixture");
  EXPECT_FALSE(reference_json.at("canonical_fhss_graph").get<bool>());
  EXPECT_TRUE(reference_json.at("reference_only").get<bool>());
}

TEST(FHSSGraphXGuardrailTest,
     FhssDocsAndConfigsKeepCorrelatorBankReferenceOnly) {
  const auto root = RepositoryRoot();
  const std::vector<std::filesystem::path> paths{
      root / "docs" / "dsp" / "fhss_decoder.md",
      root / "libdsp" / "config" / "fhss_cpsm_fixture_500msps.json",
      root / "libdsp" / "config" /
          "fhss_cpsm_channelized_fixture_500msps.json",
  };
  const std::vector<std::string> forbidden_phrases{
      "correlator-bank production-like channelization",
      "correlator bank production-like channelization",
      "correlator-bank graph is canonical",
      "correlator bank graph is canonical",
      "correlator-bank topology is canonical",
      "correlator bank topology is canonical",
      "canonical correlator-bank",
      "canonical correlator bank",
      "production-like correlator-bank",
      "production-like correlator bank",
  };

  for (const auto &path : paths) {
    ASSERT_TRUE(std::filesystem::exists(path)) << path;
    const auto text = Lowercase(ReadFile(path));
    for (const auto &phrase : forbidden_phrases) {
      EXPECT_EQ(text.find(phrase), std::string::npos)
          << path << " must not label the correlator-bank graph as "
          << "canonical or production-like channelization";
    }
  }

  const auto doc =
      Lowercase(ReadFile(root / "docs" / "dsp" / "fhss_decoder.md"));
  EXPECT_NE(doc.find("canonical fhss fixture graph"), std::string::npos);
  EXPECT_NE(doc.find("channelized graph"), std::string::npos);
  EXPECT_NE(doc.find("compatibility and"), std::string::npos);
  EXPECT_NE(doc.find("reference topology"), std::string::npos);
  EXPECT_NE(doc.find("must not be described as production-like channelization"),
            std::string::npos);
}

TEST(FHSSGraphXGuardrailTest,
     FhssDocsRejectMagnitudeOnlyDftAsCanonicalDecoderInput) {
  const auto root = RepositoryRoot();
  const auto doc = root / "docs" / "dsp" / "fhss_decoder.md";
  ASSERT_TRUE(std::filesystem::exists(doc));
  const auto text = Lowercase(ReadFile(doc));

  EXPECT_NE(text.find("magnitude-only dft/fft output is not the canonical "
                      "decoder input"),
            std::string::npos);
  EXPECT_NE(text.find("complex iq evidence"), std::string::npos);
  EXPECT_NE(text.find("cpsm branch metrics"), std::string::npos);
  EXPECT_NE(text.find("viterbi/mlse"), std::string::npos);
}

TEST(FHSSGraphXGuardrailTest,
     FhssConfigRemainsCpuOnlyFixtureWithoutChannelizerOrImpairments) {
  const auto root = RepositoryRoot();
  const auto config =
      root / "libdsp" / "config" / "fhss_cpsm_fixture_500msps.json";
  ASSERT_TRUE(std::filesystem::exists(config));
  const auto text = Lowercase(ReadFile(config));

  EXPECT_EQ(text.find("metal"), std::string::npos);
  EXPECT_EQ(text.find("gpu"), std::string::npos);
  EXPECT_EQ(text.find("channelizer"), std::string::npos);
  EXPECT_NE(text.find("\"enable_doppler\": false"), std::string::npos);
  EXPECT_NE(text.find("\"enable_noise\": false"), std::string::npos);
  EXPECT_NE(text.find("\"enable_multipath\": false"), std::string::npos);
  EXPECT_NE(text.find("\"allow_overlap\": false"), std::string::npos);
}

} // namespace
