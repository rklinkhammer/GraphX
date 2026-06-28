// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
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

constexpr std::size_t kCanonicalFixturePulseCount = 72u;

std::filesystem::path FHSSChannelizedConfigPath() {
  return std::filesystem::path(GRAPHX_SOURCE_ROOT) /
         "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json";
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

std::vector<std::string> ExpectedChannelizedFHSSNodeTypes() {
  return {
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
}

std::size_t CountNodeType(const nlohmann::json &config,
                          const std::string &type) {
  std::size_t count = 0;
  for (const auto &node : config.at("nodes")) {
    if (node.at("type").get<std::string>() == type) {
      ++count;
    }
  }
  return count;
}

std::vector<std::uint32_t> ActiveFrequenciesFromSource(
    const nlohmann::json &config) {
  for (const auto &node : config.at("nodes")) {
    if (node.at("id").get<std::string>() == "source") {
      return node.at("node_config")
          .at("active_frequency_indices")
          .get<std::vector<std::uint32_t>>();
    }
  }
  return {};
}

class ScopedJsonFile {
public:
  ScopedJsonFile(const std::string &name, const nlohmann::json &json)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::ofstream output(path_);
    if (!output.good()) {
      throw std::runtime_error("failed to create JSON file: " + path_.string());
    }
    output << json.dump(2);
  }

  ~ScopedJsonFile() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  ScopedJsonFile(const ScopedJsonFile &) = delete;
  ScopedJsonFile &operator=(const ScopedJsonFile &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

TEST(FHSSGraphXExecutorTest,
     ChannelizedJsonTopologyUsesDownconverterAndOneDetectorPerFrequency) {
  const auto config_path = FHSSChannelizedConfigPath();
  ASSERT_TRUE(std::filesystem::exists(config_path));

  const auto config = LoadJson(config_path);
  EXPECT_EQ(config.value("name", ""),
            "fhss_cpsm_channelized_fixture_500msps");
  ASSERT_TRUE(config.at("nodes").is_array());
  ASSERT_TRUE(config.at("edges").is_array());

  EXPECT_EQ(CountNodeType(config, "FHSSSyntheticIqSourceNode"), 1u);
  EXPECT_EQ(CountNodeType(config, "FHSSDownconverterNode"), 1u);
  EXPECT_EQ(CountNodeType(config, "ChannelizerNode"), 1u);
  EXPECT_EQ(CountNodeType(config, "PerChannelPulseDetectorNode"), 64u);
  EXPECT_EQ(CountNodeType(config, "FHSSCorrelatorBankDetectorNode"), 0u);
  EXPECT_EQ(config.at("nodes").size(), 75u);
  EXPECT_EQ(config.at("edges").size(), 137u);

  bool source_to_downconverter = false;
  bool downconverter_to_channelizer = false;
  std::set<std::uint32_t> channelizer_output_ports;
  std::size_t detector_to_merge_edges = 0;
  for (const auto &edge : config.at("edges")) {
    const auto src = edge.at("source_node_id").get<std::string>();
    const auto dst = edge.at("target_node_id").get<std::string>();
    if (src == "source" && dst == "downconverter") {
      source_to_downconverter = true;
    }
    if (src == "downconverter" && dst == "channelizer") {
      downconverter_to_channelizer = true;
    }
    if (src == "channelizer" && dst.starts_with("detector_")) {
      channelizer_output_ports.insert(
          edge.at("source_port").get<std::uint32_t>());
    }
    if (src.starts_with("detector_") && dst == "merge") {
      const auto detector_index =
          static_cast<std::uint32_t>(std::stoul(src.substr(9)));
      EXPECT_EQ(edge.at("target_port").get<std::uint32_t>(),
                detector_index + 1u);
      ++detector_to_merge_edges;
    }
  }
  EXPECT_TRUE(source_to_downconverter);
  EXPECT_TRUE(downconverter_to_channelizer);
  EXPECT_EQ(channelizer_output_ports.size(), 64u);
  EXPECT_TRUE(channelizer_output_ports.contains(0u));
  EXPECT_TRUE(channelizer_output_ports.contains(63u));
  EXPECT_EQ(detector_to_merge_edges, 64u);

  for (const auto &node : config.at("nodes")) {
    const auto type = node.at("type").get<std::string>();
    EXPECT_NE(type.find("Metal"), 0u) << type;
    EXPECT_NE(type.find("Gpu"), 0u) << type;
  }
}

TEST(FHSSGraphXExecutorTest,
     ChannelizedJsonTopologyRunsThroughGraphExecutorBuilderAndMatchesTruth) {
  using namespace dsp::fhss;

  const auto config_path = FHSSChannelizedConfigPath();
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
  for (const auto &type : ExpectedChannelizedFHSSNodeTypes()) {
    EXPECT_TRUE(available_types.contains(type)) << type;
  }

  auto config_json = LoadJson(config_path);
  EXPECT_EQ(CountNodeType(config_json, "PerChannelPulseDetectorNode"), 64u);
  const auto active = ActiveFrequenciesFromSource(config_json);
  EXPECT_EQ(active, (std::vector<std::uint32_t>{24, 28, 32, 36}));
  const auto &source_config =
      config_json.at("nodes").at(0).at("node_config");
  const auto expected_fixture = GenerateSyntheticIqFixture(
      FHSSSyntheticIqGeneratorConfigFromJson(graph::JsonView(source_config)));
  ASSERT_TRUE(expected_fixture) << expected_fixture.error().message;
  ASSERT_EQ(expected_fixture->truth_pulses.size(), kCanonicalFixturePulseCount)
      << "canonical source stage must emit all 72 fixture pulses";

  auto executor = graph::GraphExecutorBuilder()
                      .WithJsonConfig(config_path.string())
                      .WithPluginDirectory(plugin_dir.string())
                      .WithExecutorTimeout(std::chrono::seconds(12))
                      .Build();

  ASSERT_NE(executor, nullptr);
  auto graph_manager = executor->GetGraphManager();
  ASSERT_NE(graph_manager, nullptr);
  EXPECT_EQ(graph_manager->GetNodes().size(), 75u);
  EXPECT_EQ(graph_manager->GetEdges().size(), 137u);

  const auto run_result = executor->Execute();
  ASSERT_TRUE(run_result.success)
      << run_result.message << " " << run_result.error_details;
  ASSERT_TRUE(executor->IsCompletionSignaled());

  auto sink = ResolveFHSSMessageSink(graph_manager);
  ASSERT_NE(sink, nullptr);
  const auto diagnostics = sink->GetDiagnostics().Raw();

  EXPECT_EQ(diagnostics.at("schema").get<std::string>(),
            "graphx.fhss.message_sink.diagnostics.v1");
  const std::array<const char*, 8> required_keys{{
      "schema",
      "pulse_count",
      "rejected_count",
      "preamble_lock",
      "unsupported_overlap_rejected",
      "unsupported_impairments_rejected",
      "active_frequency_indices",
      "decoded_pulses",
  }};
  for (const char* key : required_keys) {
    EXPECT_TRUE(diagnostics.contains(key)) << key;
  }

  EXPECT_EQ(diagnostics.at("pulse_count").get<std::size_t>(),
            kCanonicalFixturePulseCount)
      << "canonical assembled-message stage must retain all 72 fixture pulses";
  EXPECT_EQ(diagnostics.at("rejected_count").get<std::size_t>(), 0u);
  EXPECT_TRUE(diagnostics.at("preamble_lock").get<bool>());
  EXPECT_TRUE(diagnostics.at("unsupported_overlap_rejected").get<bool>());
  EXPECT_TRUE(diagnostics.at("unsupported_impairments_rejected").get<bool>());
  EXPECT_EQ(diagnostics.at("synchronization_assumption").get<std::string>(),
            "known message_start_sample = 0");

  const auto locked_active =
      diagnostics.at("active_frequency_indices").get<std::vector<std::uint32_t>>();
  EXPECT_EQ(locked_active, active);

  ASSERT_TRUE(diagnostics.at("decoded_pulses").is_array());
  ASSERT_EQ(diagnostics.at("decoded_pulses").size(),
            kCanonicalFixturePulseCount)
      << "canonical decoder stage must retain all 72 fixture pulses";
  std::set<std::uint32_t> channel_ids;
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
    EXPECT_EQ(decoded.at("channel_id").get<std::uint32_t>(),
              truth.frequency_index);
    channel_ids.insert(decoded.at("channel_id").get<std::uint32_t>());
    EXPECT_TRUE(decoded.at("downconverter_passthrough").get<bool>());
    EXPECT_DOUBLE_EQ(
        decoded.at("downconverter_translation_frequency_hz").get<double>(),
        0.0);
    ASSERT_TRUE(decoded.contains("sample_time_mapping"));
    const auto &mapping = decoded.at("sample_time_mapping");
    EXPECT_EQ(mapping.at("decimation_factor").get<std::uint32_t>(), 1u);
    EXPECT_EQ(mapping.at("group_delay_input_samples").get<std::int64_t>(), 0);
  }
  EXPECT_EQ(channel_ids, std::set<std::uint32_t>({24, 28, 32, 36}));
}

TEST(FHSSGraphXExecutorTest,
     CanonicalJsonExecutorRetainsAll72PulsesAcrossSchedulingOrders) {
  using namespace dsp::fhss;

  const auto config_path = FHSSChannelizedConfigPath();
  ASSERT_TRUE(std::filesystem::exists(config_path));
  const auto plugin_dir = PluginDirectory();
  ASSERT_TRUE(std::filesystem::exists(plugin_dir));

  const auto config_json = LoadJson(config_path);
  const auto &source_config = config_json.at("nodes").at(0).at("node_config");
  const auto expected_fixture = GenerateSyntheticIqFixture(
      FHSSSyntheticIqGeneratorConfigFromJson(graph::JsonView(source_config)));
  ASSERT_TRUE(expected_fixture) << expected_fixture.error().message;
  ASSERT_EQ(expected_fixture->truth_pulses.size(), kCanonicalFixturePulseCount);

  auto reversed_config = config_json;
  std::ranges::reverse(reversed_config.at("nodes"));
  std::ranges::reverse(reversed_config.at("edges"));
  const ScopedJsonFile reversed_config_file(
      "graphx_pr1_fhss_reversed_insertion_order.json", reversed_config);
  const std::array<std::filesystem::path, 3> run_configs{
      config_path, reversed_config_file.path(), config_path};

  nlohmann::json first_decoded_pulses;
  for (std::size_t run = 0; run < run_configs.size(); ++run) {
    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(run_configs[run].string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(12))
                        .Build();

    ASSERT_NE(executor, nullptr) << "canonical run " << run;
    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr) << "canonical run " << run;

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success)
        << "canonical run " << run << ": " << run_result.message << " "
        << run_result.error_details;
    ASSERT_TRUE(executor->IsCompletionSignaled()) << "canonical run " << run;

    auto sink = ResolveFHSSMessageSink(graph_manager);
    ASSERT_NE(sink, nullptr) << "canonical run " << run;
    const auto diagnostics = sink->GetDiagnostics().Raw();

    EXPECT_EQ(diagnostics.at("pulse_count").get<std::size_t>(),
              kCanonicalFixturePulseCount)
        << "assembled-message stage truncated canonical run " << run;
    ASSERT_TRUE(diagnostics.at("decoded_pulses").is_array())
        << "canonical run " << run;
    ASSERT_EQ(diagnostics.at("decoded_pulses").size(),
              kCanonicalFixturePulseCount)
        << "decoder stage truncated canonical run " << run;
    EXPECT_EQ(diagnostics.at("rejected_count").get<std::size_t>(), 0u)
        << "canonical run " << run;

    if (run == 0u) {
      first_decoded_pulses = diagnostics.at("decoded_pulses");
    } else {
      EXPECT_EQ(diagnostics.at("decoded_pulses"), first_decoded_pulses)
          << "decoded pulse order changed in canonical run " << run;
    }
  }
}
