// SPDX-License-Identifier: MIT

#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef DSP_FHSS_CHANNELIZED_CONFIG_PATH
#define DSP_FHSS_CHANNELIZED_CONFIG_PATH                                             \
  "libdsp/config/fhss_cpsm_channelized_fixture_500msps.json"
#endif

namespace {

struct CliOptions {
  std::filesystem::path config_path{DSP_FHSS_CHANNELIZED_CONFIG_PATH};
  std::filesystem::path plugin_directory{DSP_PLUGIN_OUTPUT_DIRECTORY};
  std::filesystem::path message_json_path;
  std::filesystem::path summary_json_path;
  std::filesystem::path effective_config_path;
  std::filesystem::path channel_iq_directory;
  std::string channel_iq_indices = "active";
  int executor_timeout_s = 12;
  std::size_t decoded_pulse_limit = 8;
  bool print_effective_config = false;
};

std::uint64_t ParseUint64Option(const std::string &name, const char *raw) {
  try {
    return std::stoull(raw);
  } catch (const std::exception &ex) {
    throw std::invalid_argument(name + " requires a non-negative integer: " +
                                ex.what());
  }
}

int ParsePositiveIntOption(const std::string &name, const char *raw) {
  try {
    const int value = std::stoi(raw);
    if (value <= 0) {
      throw std::invalid_argument("value must be positive");
    }
    return value;
  } catch (const std::exception &ex) {
    throw std::invalid_argument(name + " requires a positive integer: " +
                                ex.what());
  }
}

void PrintUsage() {
  std::cout
      << "Usage: graphx-dsp-fhss-demo [--graph-config path] [--message-json path]\n"
         "                             [--plugin-dir path] [--summary-json path]\n"
         "                             [--effective-config-json path]\n"
         "                             [--channel-iq-dir path]\n"
         "                             [--channel-iq-indices active|all|csv]\n"
         "                             [--executor-timeout-s n]\n"
         "                             [--decoded-pulse-limit n]\n\n"
         "Message JSON may be either a full FHSS source node_config object or an\n"
         "object with a node_config field. It must include messages[]. The demo\n"
         "patches source/decoder graph node configs and then runs the real GraphX\n"
         "FHSS graph through GraphExecutorBuilder. Channel IQ capture writes\n"
         "SigMF cf32_le data and metadata at the ChannelizerNode output.\n";
}

CliOptions ParseArgs(int argc, char **argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      PrintUsage();
      std::exit(0);
    } else if (arg == "--graph-config") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--graph-config requires a path");
      }
      options.config_path = argv[++i];
    } else if (arg == "--message-json") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--message-json requires a path");
      }
      options.message_json_path = argv[++i];
    } else if (arg == "--plugin-dir") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--plugin-dir requires a path");
      }
      options.plugin_directory = argv[++i];
    } else if (arg == "--summary-json") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--summary-json requires a path");
      }
      options.summary_json_path = argv[++i];
    } else if (arg == "--effective-config-json") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--effective-config-json requires a path");
      }
      options.effective_config_path = argv[++i];
    } else if (arg == "--channel-iq-dir") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--channel-iq-dir requires a path");
      }
      options.channel_iq_directory = argv[++i];
    } else if (arg == "--channel-iq-indices") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--channel-iq-indices requires a value");
      }
      options.channel_iq_indices = argv[++i];
    } else if (arg == "--executor-timeout-s") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--executor-timeout-s requires a value");
      }
      options.executor_timeout_s = ParsePositiveIntOption(arg, argv[++i]);
    } else if (arg == "--decoded-pulse-limit") {
      if (i + 1 >= argc) {
        throw std::invalid_argument("--decoded-pulse-limit requires a value");
      }
      options.decoded_pulse_limit =
          static_cast<std::size_t>(ParseUint64Option(arg, argv[++i]));
    } else if (arg == "--print-effective-config") {
      options.print_effective_config = true;
    } else {
      throw std::invalid_argument("unknown argument: " + arg);
    }
  }
  return options;
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

void WriteJson(const std::filesystem::path &path, const nlohmann::json &json) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream output(path, std::ios::trunc);
  if (!output.good()) {
    throw std::runtime_error("failed to open JSON output: " + path.string());
  }
  output << std::setw(2) << json << '\n';
}

nlohmann::json SourceConfigFromMessageJson(const nlohmann::json &message_json) {
  const auto &raw =
      message_json.contains("node_config") ? message_json.at("node_config")
                                           : message_json;
  static const std::set<std::string> allowed_keys{
      "active_frequency_indices",
      "iq_center_frequency_hz",
      "iq_offsets",
      "messages",
      "idle_mode",
      "idle_duration_samples",
      "occupied_bandwidth_hz",
      "max_abs_cfo_hz",
      "enable_noise",
      "enable_doppler",
      "enable_multipath",
      "allow_overlap",
  };

  nlohmann::json filtered = nlohmann::json::object();
  for (const auto &[key, value] : raw.items()) {
    if (allowed_keys.contains(key)) {
      filtered[key] = value;
    }
  }
  return filtered;
}

nlohmann::json DerivePreamblePulses(const nlohmann::json &source_config) {
  const auto &messages = source_config.at("messages");
  if (!messages.is_array() || messages.empty()) {
    throw std::invalid_argument("message JSON must include at least one message");
  }
  const auto &pulses = messages.at(0).at("pulses");
  if (!pulses.is_array() || pulses.size() < 16u) {
    throw std::invalid_argument(
        "first message must include at least 16 preamble pulses");
  }

  nlohmann::json preamble = nlohmann::json::array();
  for (std::size_t i = 0; i < 16u; ++i) {
    const auto &pulse = pulses.at(i);
    preamble.push_back({{"frequency_index", pulse.at("frequency_index")},
                        {"word_value", pulse.at("value")}});
  }
  return preamble;
}

nlohmann::json DeriveActiveFrequencyIndices(const nlohmann::json &preamble) {
  std::set<std::uint32_t> unique;
  for (const auto &pulse : preamble) {
    unique.insert(pulse.at("frequency_index").get<std::uint32_t>());
  }
  nlohmann::json active = nlohmann::json::array();
  for (const auto index : unique) {
    active.push_back(index);
  }
  return active;
}

void CopyIfPresent(nlohmann::json &dst, const nlohmann::json &src,
                   const char *key) {
  if (src.contains(key)) {
    dst[key] = src.at(key);
  }
}

void PatchNodeConfigs(nlohmann::json &graph_config,
                      const nlohmann::json &raw_message_json) {
  auto source_config = SourceConfigFromMessageJson(raw_message_json);
  const auto preamble = DerivePreamblePulses(source_config);
  if (!source_config.contains("active_frequency_indices")) {
    source_config["active_frequency_indices"] =
        DeriveActiveFrequencyIndices(preamble);
  }

  for (auto &node : graph_config.at("nodes")) {
    const auto type = node.at("type").get<std::string>();
    if (type == "FHSSSyntheticIqSourceNode") {
      auto &node_config = node["node_config"];
      node_config = source_config;
    } else if (type == "FHSSPreambleDetectorNode") {
      auto &node_config = node["node_config"];
      if (!node_config.is_object()) {
        node_config = nlohmann::json::object();
      }
      node_config["active_frequency_indices"] =
          source_config.at("active_frequency_indices");
      node_config["preamble_pulses"] = preamble;
      CopyIfPresent(node_config, source_config, "iq_center_frequency_hz");
      CopyIfPresent(node_config, source_config, "occupied_bandwidth_hz");
      CopyIfPresent(node_config, source_config, "max_abs_cfo_hz");
      CopyIfPresent(node_config, source_config, "allow_overlap");
    } else if (type == "FHSSMessageAssemblerNode") {
      auto &node_config = node["node_config"];
      node_config = source_config;
      node_config["preamble_pulses"] = preamble;
      node_config["truth_from_fixture"] = true;
    }
  }
}

std::vector<std::uint32_t>
ParseChannelIqIndices(const std::string &selection,
                      const nlohmann::json &source_config) {
  if (selection == "all") {
    return {};
  }
  if (selection == "active") {
    return source_config.at("active_frequency_indices")
        .get<std::vector<std::uint32_t>>();
  }

  std::vector<std::uint32_t> indices;
  std::size_t begin = 0;
  while (begin < selection.size()) {
    const auto end = selection.find(',', begin);
    const auto token = selection.substr(begin, end - begin);
    const auto value = ParseUint64Option("--channel-iq-indices", token.c_str());
    if (value >= 64) {
      throw std::invalid_argument(
          "--channel-iq-indices values must be in [0,63]");
    }
    indices.push_back(static_cast<std::uint32_t>(value));
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  if (indices.empty()) {
    throw std::invalid_argument(
        "--channel-iq-indices requires active, all, or a CSV list");
  }
  return indices;
}

void PatchChannelIqCapture(nlohmann::json &graph_config,
                           const CliOptions &options) {
  if (options.channel_iq_directory.empty()) {
    return;
  }

  const auto source_it =
      std::ranges::find_if(graph_config.at("nodes"), [](const auto &node) {
        return node.at("type") == "FHSSSyntheticIqSourceNode";
      });
  if (source_it == graph_config.at("nodes").end()) {
    throw std::invalid_argument("FHSS graph has no synthetic IQ source");
  }
  const auto indices = ParseChannelIqIndices(
      options.channel_iq_indices, source_it->at("node_config"));

  bool patched = false;
  for (auto &node : graph_config.at("nodes")) {
    if (node.at("type") != "ChannelizerNode") {
      continue;
    }
    node["node_config"]["iq_capture"] = {
        {"enabled", true},
        {"output_directory", options.channel_iq_directory.string()},
        {"frequency_indices", indices},
        {"overwrite", true}};
    patched = true;
  }
  if (!patched) {
    throw std::invalid_argument("FHSS graph has no ChannelizerNode");
  }
}

std::filesystem::path DefaultEffectiveConfigPath() {
  return std::filesystem::temp_directory_path() /
         "graphx_fhss_demo_effective_config.json";
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

nlohmann::json ExecutionResultJson(const graph::ExecutionResult &result) {
  return nlohmann::json{{"success", result.success},
                        {"message", result.message},
                        {"elapsed_time_ms", result.elapsed_time_ms},
                        {"init_elapsed_time_ms", result.init_elapsed_time_ms},
                        {"start_elapsed_time_ms", result.start_elapsed_time_ms},
                        {"run_elapsed_time_ms", result.run_elapsed_time_ms},
                        {"stop_elapsed_time_ms", result.stop_elapsed_time_ms},
                        {"join_elapsed_time_ms", result.join_elapsed_time_ms},
                        {"error_details", result.error_details}};
}

nlohmann::json GraphMetricsJson(const graph::GraphMetrics &metrics) {
  return nlohmann::json{
      {"total_items_processed", metrics.total_items_processed.load()},
      {"total_items_rejected", metrics.total_items_rejected.load()},
      {"total_messages_processed", metrics.total_messages_processed.load()},
      {"graph_total_enqueued", metrics.graph_total_enqueued.load()},
      {"graph_total_dequeued", metrics.graph_total_dequeued.load()},
      {"total_queue_time_ns", metrics.total_queue_time_ns.load()},
      {"total_process_time_ns", metrics.total_process_time_ns.load()},
      {"total_thread_time_ns", metrics.total_thread_time_ns.load()},
      {"backpressure_events", metrics.backpressure_events.load()},
      {"peak_queue_depth", metrics.peak_queue_depth.load()},
      {"peak_active_threads", metrics.peak_active_threads.load()}};
}

nlohmann::json BuildSummary(const CliOptions &options,
                            const std::filesystem::path &effective_config_path,
                            const nlohmann::json &effective_config,
                            const graph::ExecutionResult &result,
                            const graph::GraphManager &manager,
                            const nlohmann::json &diagnostics) {
  nlohmann::json summary{
      {"schema", "graphx.dsp.fhss_demo_summary.v1"},
      {"graph_config_path", options.config_path.string()},
      {"effective_config_path", effective_config_path.string()},
      {"message_json_path", options.message_json_path.string()},
      {"plugin_directory", options.plugin_directory.string()},
      {"canonical_fhss_graph",
       effective_config.value("canonical_fhss_graph", false)},
      {"fhss_graph_role", effective_config.value("fhss_graph_role", "")},
      {"completion_signaled", result.success},
      {"execution_result", ExecutionResultJson(result)},
      {"graph", {{"node_count", manager.GetNodes().size()},
                 {"edge_count", manager.GetEdges().size()}}},
      {"graph_metrics", GraphMetricsJson(manager.GetMetrics())},
      {"fhss_diagnostics", diagnostics}};
  if (!options.channel_iq_directory.empty()) {
    std::vector<std::string> artifact_paths;
    if (std::filesystem::exists(options.channel_iq_directory)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(options.channel_iq_directory)) {
        if (entry.path().extension() == ".sigmf-meta") {
          artifact_paths.push_back(entry.path().string());
        }
      }
    }
    std::ranges::sort(artifact_paths);
    summary["channel_iq_capture"] = {
        {"enabled", true},
        {"output_directory", options.channel_iq_directory.string()},
        {"selection", options.channel_iq_indices},
        {"format", "SigMF cf32_le"},
        {"metadata_files", std::move(artifact_paths)}};
  } else {
    summary["channel_iq_capture"] = {{"enabled", false}};
  }
  return summary;
}

void PrintPulseTable(const nlohmann::json &diagnostics, std::size_t limit) {
  const auto &pulses = diagnostics.at("decoded_pulses");
  const auto count = std::min(limit, pulses.size());
  std::cout << "Decoded pulse preview (" << count << " of " << pulses.size()
            << "):\n";
  for (std::size_t i = 0; i < count; ++i) {
    const auto &pulse = pulses.at(i);
    std::cout << "  [" << i << "] start="
              << pulse.at("global_start_sample").get<std::uint64_t>()
              << " freq_index=" << pulse.at("frequency_index").get<std::uint32_t>()
              << " value=0x" << std::hex << std::setw(8) << std::setfill('0')
              << pulse.at("decoded_value").get<std::uint32_t>() << std::dec
              << std::setfill(' ')
              << " confidence=" << pulse.at("confidence").get<double>()
              << '\n';
  }
}

void PrintConsoleSummary(const nlohmann::json &summary) {
  const auto &result = summary.at("execution_result");
  const auto &diagnostics = summary.at("fhss_diagnostics");
  const auto &metrics = summary.at("graph_metrics");
  std::cout << "GraphX DSP FHSS demo runtime\n";
  std::cout << "Graph role: " << summary.at("fhss_graph_role").get<std::string>()
            << '\n';
  std::cout << "Canonical FHSS graph: "
            << (summary.at("canonical_fhss_graph").get<bool>() ? "true"
                                                               : "false")
            << '\n';
  std::cout << "Execution success: "
            << (result.at("success").get<bool>() ? "true" : "false") << '\n';
  std::cout << "Elapsed (ms): " << result.at("elapsed_time_ms").get<std::uint64_t>()
            << '\n';
  std::cout << "Nodes/edges: "
            << summary.at("graph").at("node_count").get<std::size_t>() << '/'
            << summary.at("graph").at("edge_count").get<std::size_t>() << '\n';
  std::cout << "Graph messages enqueued/dequeued: "
            << metrics.at("graph_total_enqueued").get<std::uint64_t>() << '/'
            << metrics.at("graph_total_dequeued").get<std::uint64_t>() << '\n';
  std::cout << "Pulse count: "
            << diagnostics.at("pulse_count").get<std::size_t>() << '\n';
  std::cout << "Rejected count: "
            << diagnostics.at("rejected_count").get<std::size_t>() << '\n';
  std::cout << "Preamble lock: "
            << (diagnostics.at("preamble_lock").get<bool>() ? "true" : "false")
            << '\n';
  std::cout << "Truth mismatches: "
            << diagnostics.at("truth_mismatch_count").get<std::size_t>()
            << '\n';
  if (diagnostics.contains("active_frequency_indices")) {
    std::cout << "Active frequencies:";
    for (const auto &index : diagnostics.at("active_frequency_indices")) {
      std::cout << ' ' << index.get<std::uint32_t>();
    }
    std::cout << '\n';
  }
  PrintPulseTable(diagnostics,
                  summary.value("decoded_pulse_limit", std::size_t{8}));
}

} // namespace

int main(int argc, char **argv) {
  try {
    auto options = ParseArgs(argc, argv);
    auto graph_config = LoadJson(options.config_path);
    if (!options.message_json_path.empty()) {
      PatchNodeConfigs(graph_config, LoadJson(options.message_json_path));
    }
    PatchChannelIqCapture(graph_config, options);

    auto effective_config_path = options.effective_config_path.empty()
                                     ? DefaultEffectiveConfigPath()
                                     : options.effective_config_path;
    WriteJson(effective_config_path, graph_config);
    if (options.print_effective_config) {
      std::cout << std::setw(2) << graph_config << '\n';
    }

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(effective_config_path.string())
                        .WithPluginDirectory(options.plugin_directory.string())
                        .WithExecutorTimeout(
                            std::chrono::seconds(options.executor_timeout_s))
                        .Build();
    if (!executor) {
      throw std::runtime_error("failed to build FHSS graph executor");
    }

    auto manager = executor->GetGraphManager();
    if (!manager) {
      throw std::runtime_error("executor did not expose a GraphManager");
    }
    manager->EnableMetrics(true);

    const auto result = executor->Execute();
    auto sink = ResolveFHSSMessageSink(manager);
    if (!sink) {
      throw std::runtime_error("failed to resolve FHSSMessageSinkNode");
    }
    const auto diagnostics = sink->GetDiagnostics().Raw();
    auto summary = BuildSummary(options, effective_config_path, graph_config,
                                result, *manager, diagnostics);
    summary["completion_signaled"] = executor->IsCompletionSignaled();
    summary["decoded_pulse_limit"] = options.decoded_pulse_limit;

    PrintConsoleSummary(summary);
    if (!options.summary_json_path.empty()) {
      WriteJson(options.summary_json_path, summary);
      std::cout << "Summary JSON: " << options.summary_json_path << '\n';
    }

    return result.success && executor->IsCompletionSignaled() ? 0 : 2;
  } catch (const std::exception &ex) {
    std::cerr << "FHSS demo failed: " << ex.what() << '\n';
    return 1;
  }
}
