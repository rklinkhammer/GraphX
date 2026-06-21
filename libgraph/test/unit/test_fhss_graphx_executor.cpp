// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dsp/fhss/FHSSGraphXConfig.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeProviderBootstrap.hpp"

#ifndef GRAPHX_SOURCE_ROOT
#define GRAPHX_SOURCE_ROOT "."
#endif

#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

std::filesystem::path FHSSConfigPath() {
  return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
         "libdsp/config/fhss_cpsm_fixture_500msps.json";
}

std::filesystem::path PluginDirectory() {
  return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

nlohmann::json LoadJson(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.good()) {
    throw std::runtime_error("failed to open JSON file: " + path.string());
  }
  nlohmann::json json;
  input >> json;
  return json;
}

std::shared_ptr<dsp::fhss::FHSSMessageSinkNode>
ResolveFHSSMessageSink(const std::shared_ptr<graph::GraphManager> &manager) {
  if (!manager) {
    return nullptr;
  }
  for (const auto &node : manager->GetNodes()) {
    auto wrapper =
        std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
    if (!wrapper) {
      continue;
    }
    auto sink = wrapper->GetNode<dsp::fhss::FHSSMessageSinkNode>();
    if (sink) {
      return sink;
    }
  }
  return nullptr;
}

std::vector<std::string> ExpectedFHSSNodeTypes() {
  return {
      "FHSSSyntheticIqSourceNode",
      "FHSSCorrelatorBankDetectorNode",
      "FHSSPulseMergeNode",
      "FHSSPulseCandidateNode",
      "CPSMBranchMetricNode",
      "CPSMViterbiDecoderNode",
      "FHSSPulseWordDecoderNode",
      "FHSSPreambleDetectorNode",
      "FHSSMessageAssemblerNode",
      "FHSSMessageSinkNode",
  };
}

} // namespace

TEST(FHSSGraphXExecutorTest, JsonTopologyNamesOnlyRealFHSSGraphXNodes) {
  const auto config_path = FHSSConfigPath();
  ASSERT_TRUE(std::filesystem::exists(config_path));

  const auto config = LoadJson(config_path);
  EXPECT_EQ(config.value("name", ""), "fhss_cpsm_fixture_500msps");
  ASSERT_TRUE(config.at("nodes").is_array());
  ASSERT_TRUE(config.at("edges").is_array());
  EXPECT_EQ(config.at("nodes").size(), 10u);
  EXPECT_EQ(config.at("edges").size(), 9u);

  std::set<std::string> actual_types;
  for (const auto &node : config.at("nodes")) {
    actual_types.insert(node.at("type").get<std::string>());
  }
  const auto expected = ExpectedFHSSNodeTypes();
  EXPECT_EQ(actual_types, std::set<std::string>(expected.begin(), expected.end()));
}

TEST(FHSSGraphXExecutorTest,
     JsonTopologyRunsThroughGraphExecutorBuilderAndEmitsDiagnostics) {
  using namespace dsp::fhss;

  const auto config_path = FHSSConfigPath();
  ASSERT_TRUE(std::filesystem::exists(config_path));
  const auto plugin_dir = PluginDirectory();
  ASSERT_TRUE(std::filesystem::exists(plugin_dir));

  auto bootstrap =
      app::NodeProviderBootstrap::CreateProviderExpected(plugin_dir.string());
  ASSERT_TRUE(bootstrap);
  ASSERT_NE(bootstrap->provider, nullptr);

  auto available =
      app::NodeProviderBootstrap::GetAvailableNodeTypesExpected(
          bootstrap->provider);
  ASSERT_TRUE(available);
  const std::set<std::string> available_types(available->begin(),
                                              available->end());
  for (const auto &type : ExpectedFHSSNodeTypes()) {
    EXPECT_TRUE(available_types.contains(type)) << type;
  }

  auto config_json = LoadJson(config_path);
  const auto &source_config =
      config_json.at("nodes").at(0).at("node_config");
  const auto expected_fixture = GenerateSyntheticIqFixture(
      FHSSSyntheticIqGeneratorConfigFromJson(graph::JsonView(source_config)));
  ASSERT_TRUE(expected_fixture) << expected_fixture.error().message;

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(config_path.string())
                      .WithPluginDirectory(plugin_dir.string())
                      .WithExecutorTimeout(std::chrono::seconds(8))
                      .Build();

  ASSERT_NE(executor, nullptr);
  auto graph_manager = executor->GetGraphManager();
  ASSERT_NE(graph_manager, nullptr);
  EXPECT_EQ(graph_manager->GetNodes().size(), 10u);
  EXPECT_EQ(graph_manager->GetEdges().size(), 9u);

  const auto run_result = executor->Execute();
  ASSERT_TRUE(run_result.success)
      << run_result.message << " " << run_result.error_details;
  ASSERT_TRUE(executor->IsCompletionSignaled());

  auto sink = ResolveFHSSMessageSink(graph_manager);
  ASSERT_NE(sink, nullptr);
  const auto diagnostics = sink->GetDiagnostics().Raw();

  EXPECT_EQ(diagnostics.at("pulse_count").get<std::size_t>(),
            expected_fixture->truth_pulses.size());
  EXPECT_EQ(diagnostics.at("rejected_count").get<std::size_t>(), 0u);
  EXPECT_TRUE(diagnostics.at("preamble_lock").get<bool>());
  EXPECT_EQ(diagnostics.at("truth_mismatch_count").get<std::size_t>(), 0u);
  EXPECT_TRUE(diagnostics.at("unsupported_overlap_rejected").get<bool>());
  EXPECT_TRUE(diagnostics.at("unsupported_impairments_rejected").get<bool>());
  EXPECT_EQ(diagnostics.at("synchronization_assumption").get<std::string>(),
            "known message_start_sample = 0");

  const auto active =
      diagnostics.at("active_frequency_indices").get<std::vector<std::uint32_t>>();
  EXPECT_EQ(active, (std::vector<std::uint32_t>{24, 28, 32, 36}));

  ASSERT_TRUE(diagnostics.at("decoded_pulses").is_array());
  ASSERT_EQ(diagnostics.at("decoded_pulses").size(),
            expected_fixture->truth_pulses.size());
  for (std::size_t i = 0; i < expected_fixture->truth_pulses.size(); ++i) {
    const auto &truth = expected_fixture->truth_pulses[i];
    const auto &decoded = diagnostics.at("decoded_pulses").at(i);
    EXPECT_EQ(decoded.at("global_start_sample").get<std::uint64_t>(),
              truth.global_start_sample);
    EXPECT_EQ(decoded.at("duration_samples").get<std::uint64_t>(),
              truth.duration_samples);
    EXPECT_EQ(decoded.at("frequency_index").get<std::uint32_t>(),
              truth.frequency_index);
    EXPECT_EQ(decoded.at("decoded_value").get<std::uint32_t>(), truth.value);
    EXPECT_DOUBLE_EQ(decoded.at("rf_frequency_hz").get<double>(),
                     truth.rf_frequency_hz);
    EXPECT_DOUBLE_EQ(decoded.at("iq_offset_frequency_hz").get<double>(),
                     truth.iq_offset_frequency_hz);
    EXPECT_TRUE(decoded.contains("sample_time_mapping"));
  }

  EXPECT_EQ(diagnostics.at("global_start_sample").get<std::uint64_t>(),
            expected_fixture->truth_pulses.front().global_start_sample);
  EXPECT_EQ(diagnostics.at("frequency_index").get<std::uint32_t>(),
            expected_fixture->truth_pulses.front().frequency_index);
  EXPECT_EQ(diagnostics.at("decoded_value").get<std::uint32_t>(),
            expected_fixture->truth_pulses.front().value);
  EXPECT_TRUE(diagnostics.contains("confidence"));
  EXPECT_TRUE(diagnostics.contains("viterbi_path_metric"));
}
