// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                           \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

namespace {

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "unable to read " << path;
  nlohmann::json json;
  input >> json;
  return json;
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  EXPECT_TRUE(input.good()) << "unable to read " << path;
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

std::vector<std::uint32_t> JsonUint32Vector(const nlohmann::json &json) {
  std::vector<std::uint32_t> out;
  out.reserve(json.size());
  for (const auto &value : json) {
    out.push_back(value.get<std::uint32_t>());
  }
  return out;
}

const nlohmann::json &FindNode(const nlohmann::json &graph,
                               const std::string &id) {
  for (const auto &node : graph.at("nodes")) {
    if (node.at("id").get<std::string>() == id) {
      return node;
    }
  }
  throw std::runtime_error("missing node: " + id);
}

std::size_t CountNodesOfType(const nlohmann::json &graph,
                             const std::string &type) {
  std::size_t count = 0;
  for (const auto &node : graph.at("nodes")) {
    if (node.at("type").get<std::string>() == type) {
      ++count;
    }
  }
  return count;
}

std::size_t CountEdgesFromNode(const nlohmann::json &graph,
                               const std::string &source_id) {
  std::size_t count = 0;
  for (const auto &edge : graph.at("edges")) {
    if (edge.at("source_node_id").get<std::string>() == source_id) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST(DspFhssBaselineGuardrailTest,
     CanonicalChannelizedGraphHasOnePortAndDetectorPerFrequency) {
  const auto graph =
      LoadJson(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  ASSERT_EQ(graph.at("fhss_graph_role").get<std::string>(),
            "canonical_channelized_fixture");
  ASSERT_TRUE(graph.at("canonical_fhss_graph").get<bool>());
  ASSERT_FALSE(graph.at("reference_only").get<bool>());

  const auto &channelizer = FindNode(graph, "channelizer");
  ASSERT_EQ(channelizer.at("type").get<std::string>(), "ChannelizerNode");
  const auto &config = channelizer.at("node_config");
  const auto receiver_indices =
      JsonUint32Vector(config.at("receiver_frequency_indices"));
  const auto channel_ids = JsonUint32Vector(config.at("channel_ids"));

  ASSERT_EQ(receiver_indices.size(), 64u);
  ASSERT_EQ(channel_ids.size(), 64u);
  EXPECT_EQ(receiver_indices.front(), 0u);
  EXPECT_EQ(receiver_indices.back(), 63u);
  EXPECT_EQ(channel_ids.front(), 0u);
  EXPECT_EQ(channel_ids.back(), 63u);
  EXPECT_EQ(std::set(receiver_indices.begin(), receiver_indices.end()).size(),
            64u);
  EXPECT_EQ(std::set(channel_ids.begin(), channel_ids.end()).size(), 64u);
  for (std::size_t i = 0; i < receiver_indices.size(); ++i) {
    EXPECT_EQ(receiver_indices[i], i);
    EXPECT_EQ(channel_ids[i], i);
  }

  EXPECT_EQ(CountNodesOfType(graph, "PerChannelPulseDetectorNode"), 64u);
  EXPECT_EQ(CountEdgesFromNode(graph, "channelizer"), 64u);
}

TEST(DspFhssBaselineGuardrailTest,
     CanonicalChannelizedGraphUsesTokenReadyRealGraphXNodeNames) {
  const auto graph =
      LoadJson(std::filesystem::path(DSP_FHSS_CHANNELIZED_CONFIG_PATH));
  const std::vector<std::string> required_node_types{
      "FHSSSyntheticIqSourceNode",
      "FHSSDownconverterNode",
      "ChannelizerNode",
      "PerChannelPulseDetectorNode",
      "FHSSPulseMergeNode",
      "FHSSPulseCandidateNode",
      "CPSMBranchMetricNode",
      "CPSMViterbiDecoderNode",
      "FHSSPulseWordDecoderNode",
      "FHSSPreambleDetectorNode",
      "FHSSMessageAssemblerNode",
      "FHSSMessageSinkNode",
  };

  for (const auto &type : required_node_types) {
    EXPECT_GE(CountNodesOfType(graph, type), 1u) << type;
  }
  EXPECT_EQ(CountNodesOfType(graph, "FHSSCorrelatorBankDetectorNode"), 0u);

  const auto docs =
      ReadFile(std::filesystem::path(GRAPHX_SOURCE_ROOT) / "README.md") +
      "\n" +
      ReadFile(std::filesystem::path(GRAPHX_SOURCE_ROOT) / "plan" /
               "BASELINE.md");
  EXPECT_NE(docs.find("graph::gpu::accel::ControlToken<"), std::string::npos);
  EXPECT_NE(docs.find("canonical FHSS implementation uses real GraphX nodes"),
            std::string::npos);
}

TEST(DspFhssBaselineGuardrailTest,
     DemoDoesNotExposeDeletedReferenceCorrelatorSurface) {
  const auto root = std::filesystem::path(GRAPHX_SOURCE_ROOT);
  const auto demo_source = ReadFile(root / "examples" / "DSP" / "src" /
                                    "fhss_demo.cpp");
  const auto demo_cmake = ReadFile(root / "examples" / "DSP" /
                                   "CMakeLists.txt");
  const auto combined = demo_source + "\n" + demo_cmake;

  EXPECT_EQ(combined.find("--reference-correlator-graph"),
            std::string::npos);
  EXPECT_EQ(combined.find("DSP_FHSS_REFERENCE_CONFIG_PATH"),
            std::string::npos);
  EXPECT_EQ(combined.find("fhss_cpsm_fixture_500msps.json"),
            std::string::npos);
}
