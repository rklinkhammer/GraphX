// SPDX-License-Identifier: MIT

#include "FHSSDashboardConfigurationPolicy.hpp"
#include "FHSSReceiverTemplate.hpp"
#include "dsp/fhss/FHSSGraphXConfig.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dsp::fhss::dashboard {
namespace {

constexpr std::uint64_t kPreamblePulseCount = 16;
constexpr std::uint64_t kPulseSamples = 3200;
constexpr std::uint64_t kGapSamples = 3300;
constexpr std::uint64_t kPulsePeriodSamples = kPulseSamples + kGapSamples;

nlohmann::json *FindNode(nlohmann::json &graph, std::string_view id) {
  if (!graph.contains("nodes") || !graph.at("nodes").is_array())
    return nullptr;
  for (auto &node : graph["nodes"]) {
    if (node.value("id", std::string{}) == id)
      return &node;
  }
  return nullptr;
}

const nlohmann::json *FindSource(const nlohmann::json &graph) {
  if (!graph.contains("nodes") || !graph.at("nodes").is_array())
    return nullptr;
  for (const auto &node : graph.at("nodes")) {
    if (node.value("id", std::string{}) == "source" ||
        node.value("type", std::string{}) == "FHSSSyntheticIqSourceNode" ||
        node.value("type", std::string{}) == "FHSSBinaryIqFileSourceNode") {
      return &node;
    }
  }
  return nullptr;
}

nlohmann::json Preamble(const nlohmann::json &authoritative) {
  nlohmann::json result = nlohmann::json::array();
  if (!authoritative.contains("messages") ||
      !authoritative.at("messages").is_array() ||
      authoritative.at("messages").empty())
    return result;
  const auto &message = authoritative.at("messages").front();
  if (!message.contains("pulses") || !message.at("pulses").is_array())
    return result;
  const auto count =
      std::min<std::size_t>(kPreamblePulseCount, message.at("pulses").size());
  for (std::size_t index = 0; index < count; ++index) {
    const auto &pulse = message.at("pulses").at(index);
    result.push_back({{"frequency_index", pulse.value("frequency_index", 0u)},
                      {"word_value", pulse.value("value", 0u)}});
  }
  return result;
}

nlohmann::json ActiveSet(const nlohmann::json &authoritative) {
  std::set<std::uint32_t> seen;
  nlohmann::json unique = nlohmann::json::array();
  for (const auto &pulse : Preamble(authoritative)) {
    const auto frequency = pulse.value("frequency_index", 0u);
    if (seen.insert(frequency).second)
      unique.push_back(frequency);
  }
  return unique;
}

void StripTruth(nlohmann::json &value) {
  if (value.is_array()) {
    for (auto &entry : value)
      StripTruth(entry);
    return;
  }
  if (!value.is_object())
    return;
  static constexpr std::string_view forbidden[] = {
      "messages",
      "truth_from_fixture",
      "truth_path",
      "truth_file",
      "generator_metadata",
      "transmitted_active_frequency_indices",
      "transmitted_pulse_frequency_indices"};
  for (const auto key : forbidden)
    value.erase(std::string(key));
  for (auto &entry : value)
    StripTruth(entry);
}

nlohmann::json Error(std::string pointer, std::string code, std::string message,
                     std::string level = "semantic") {
  if (pointer.empty())
    pointer = "/fhss/scenario";
  else if (pointer.starts_with('/'))
    pointer = "/fhss/scenario" + pointer;
  return {{"level", std::move(level)},
          {"node_id", "fhss"},
          {"pointer", std::move(pointer)},
          {"code", std::move(code)},
          {"message", std::move(message)}};
}

} // namespace

FHSSDashboardConfigurationPolicy::FHSSDashboardConfigurationPolicy()
    : FHSSDashboardConfigurationPolicy(
          nlohmann::json::parse(kEmbeddedReceiverTemplate)) {}

FHSSDashboardConfigurationPolicy::FHSSDashboardConfigurationPolicy(
    nlohmann::json receiver_template)
    : receiver_template_(std::move(receiver_template)) {
  if (!receiver_template_.is_object() ||
      !receiver_template_.contains("nodes") ||
      !receiver_template_.at("nodes").is_array() ||
      !receiver_template_.contains("edges") ||
      !receiver_template_.at("edges").is_array()) {
    throw std::invalid_argument("FHSS receiver template requires node and edge arrays");
  }
}

nlohmann::json FHSSDashboardConfigurationPolicy::ExtractAuthoritative(
    const nlohmann::json &document) const {
  nlohmann::json authoritative = nlohmann::json::object();
  if (document.contains("fhss") && document.at("fhss").is_object() &&
      document.at("fhss").contains("scenario")) {
    authoritative = document.at("fhss").at("scenario");
  } else if (const auto *source = FindSource(document);
             source && source->contains("node_config") &&
             source->at("node_config").is_object()) {
    authoritative = source->at("node_config");
  }
  if (!authoritative.is_object())
    authoritative = nlohmann::json::object();
  authoritative.erase("active_frequency_indices");
  authoritative.erase("preamble_pulses");
  return authoritative;
}

nlohmann::json FHSSDashboardConfigurationPolicy::DeriveEffective(
    const nlohmann::json &base_document,
    const nlohmann::json &authoritative) const {
  (void)base_document;
  auto graph = receiver_template_;
  const auto preamble = Preamble(authoritative);
  const auto active = ActiveSet(authoritative);
  if (auto *source = FindNode(graph, "source")) {
    auto &config = (*source)["node_config"];
    if (authoritative.contains("receiver_input") &&
        authoritative.at("receiver_input").is_object()) {
      const auto &input = authoritative.at("receiver_input");
      for (const auto key : {"file_path", "sample_format",
                             "first_complex_sample", "max_complex_samples",
                             "max_read_complex_samples"}) {
        if (input.contains(key))
          config[key] = input.at(key);
      }
    }
  }
  for (const auto id : {"preamble", "assembler"}) {
    if (auto *node = FindNode(graph, id)) {
      auto &config = (*node)["node_config"];
      if (!config.is_object())
        config = nlohmann::json::object();
      config["preamble_pulses"] = preamble;
      config.erase("active_frequency_indices");
      config.erase("messages");
      config.erase("truth_from_fixture");
    }
  }
  if (auto *channelizer = FindNode(graph, "channelizer")) {
    auto &config = (*channelizer)["node_config"];
    config.erase("transmitted_active_frequency_indices");
    config.erase("transmitted_pulse_frequency_indices");
  }
  graph["dashboard_derived"] = {
      {"active_frequency_indices", active},
      {"preamble_pulses", preamble},
      {"timing",
       {{"pulse_samples", kPulseSamples},
        {"gap_samples", kGapSamples},
        {"pulse_period_samples", kPulsePeriodSamples}}}};
  graph["active_frequency_indices"] = active;
  graph["preamble_pulses"] = preamble;
  return graph;
}

nlohmann::json FHSSDashboardConfigurationPolicy::Validate(
    const nlohmann::json &authoritative) const {
  nlohmann::json errors = nlohmann::json::array();
  const nlohmann::json levels = {"syntax", "structure", "semantic",
                                 "descriptor"};
  if (!authoritative.is_object()) {
    errors.push_back(Error("", "scenario_not_object",
                           "authoritative configuration must be an object",
                           "structure"));
    return {{"valid", false}, {"levels", levels}, {"errors", errors}};
  }
  if (!authoritative.contains("messages")) {
    errors.push_back(Error("/messages", "missing_preamble",
                           "messages with a 16-pulse preamble are required",
                           "structure"));
    return {{"valid", false}, {"levels", levels}, {"errors", errors}};
  }
  if (!authoritative.at("messages").is_array() ||
      authoritative.at("messages").empty()) {
    errors.push_back(Error("/messages", "missing_messages",
                           "messages must be a non-empty array", "structure"));
    return {{"valid", false}, {"levels", levels}, {"errors", errors}};
  }

  const auto active = ActiveSet(authoritative);
  if (active.size() != 4) {
    errors.push_back(Error(
        "/messages/0/pulses/0/frequency_index", "invalid_active_frequency_set",
        "the 16-pulse preamble must identify exactly four frequencies"));
  }
  const auto exact_number = [&](std::string_view key, double expected,
                                std::string_view code) {
    if (!authoritative.contains(key))
      return;
    const auto &value = authoritative.at(key);
    if (!value.is_number() || value.get<double>() != expected) {
      errors.push_back(Error("/" + std::string(key), std::string(code),
                             std::string(key) + " must equal the architecture "
                             "constant " + std::to_string(expected)));
    }
  };
  exact_number("sample_rate_hz", 500'000'000.0,
               "fhss.unsupported_sample_rate");
  exact_number("bit_rate_hz", 5'000'000.0, "fhss.unsupported_bit_rate");
  exact_number("bits_per_pulse", 32.0, "fhss.unsupported_bits_per_pulse");
  exact_number("pulse_gap_seconds", 6.6e-6, "fhss.unsupported_pulse_gap");
  for (const auto key : {"enable_noise", "enable_multipath"}) {
    if (authoritative.contains(key) &&
        (!authoritative.at(key).is_boolean() ||
         authoritative.at(key).get<bool>())) {
      errors.push_back(Error("/" + std::string(key),
                             "fhss.unsupported_feature_flag",
                             std::string(key) + " is not supported"));
    }
  }
  std::set<std::uint64_t> ids;
  struct Window {
    std::uint64_t begin;
    std::uint64_t end;
    std::size_t index;
  };
  std::vector<Window> windows;
  const auto &messages = authoritative.at("messages");
  try {
    auto generator_validation = authoritative;
    generator_validation["active_frequency_indices"] = active;
    const auto config = dsp::fhss::FHSSSyntheticIqGeneratorConfigFromJson(
        graph::JsonView(generator_validation));
    const auto timing = dsp::fhss::DeriveTimingModel(config.decode_config.timing);
    if (!timing) {
      errors.push_back(Error("", "architecture_timing_invalid",
                             timing.error().message));
    } else {
      const auto checks = {
          dsp::fhss::ValidateGeneratorFeatureFlags(config),
          dsp::fhss::ValidateActiveFrequencySet(
              config.decode_config.active_frequency_indices),
          dsp::fhss::ValidateFrequencyConfig(config.decode_config.frequency),
          dsp::fhss::ValidateIqOffsets(
              config.decode_config.frequency,
              config.decode_config.active_frequency_indices),
          dsp::fhss::ValidateMessageSchedule(
              config.messages, config.decode_config, *timing,
              config.allow_overlap)};
      for (const auto &check : checks) {
        if (!check)
          errors.push_back(Error("", "architecture_validation_failed",
                                 check.error().message));
      }
    }
  } catch (const std::exception &error) {
    errors.push_back(Error("", "generator_config_invalid", error.what(),
                           "structure"));
  }

  const auto golden_preamble = Preamble(authoritative);
  std::map<std::uint32_t, std::uint64_t> preamble_words;
  for (std::size_t mi = 0; mi < messages.size(); ++mi) {
    const auto prefix = "/messages/" + std::to_string(mi);
    const auto &message = messages.at(mi);
    if (!message.is_object() || !message.contains("pulses") ||
        !message.at("pulses").is_array()) {
      errors.push_back(Error(prefix, "message_structure_invalid",
                             "each message requires a pulses array",
                             "structure"));
      continue;
    }
    const auto id = message.value("message_id", std::uint64_t{0});
    if (!ids.insert(id).second) {
      errors.push_back(Error(prefix + "/message_id", "duplicate_message_id",
                             "message_id values must be unique"));
    }
    const auto &pulses = message.at("pulses");
    if (pulses.size() < kPreamblePulseCount || pulses.size() > 256) {
      errors.push_back(Error(prefix + "/pulses", "invalid_pulse_count",
                             "each message must contain 16-256 pulses"));
    }
    if (pulses.size() < kPreamblePulseCount) {
      errors.push_back(Error(prefix + "/pulses", "invalid_preamble_length",
                             "the preamble must contain exactly 16 leading "
                             "pulses", "structure"));
    }
    if (pulses.size() >= kPreamblePulseCount) {
      nlohmann::json actual = nlohmann::json::array();
      for (std::size_t pi = 0; pi < kPreamblePulseCount; ++pi) {
        const auto &pulse = pulses.at(pi);
        actual.push_back({{"frequency_index", pulse.value("frequency_index", 0u)},
                          {"word_value", pulse.value("value", 0u)}});
      }
      if (actual != golden_preamble) {
        errors.push_back(Error(prefix + "/pulses",
                               "preamble_pattern_mismatch",
                               "every message must repeat the canonical "
                               "16-pulse preamble exactly"));
      }
    }
    for (std::size_t pi = 0; pi < pulses.size(); ++pi) {
      const auto pointer = prefix + "/pulses/" + std::to_string(pi);
      const auto &pulse = pulses.at(pi);
      if (!pulse.is_object()) {
        errors.push_back(Error(pointer, "pulse_not_object",
                               "each pulse must be an object", "structure"));
        continue;
      }
      const auto frequency = pulse.value("frequency_index", 0u);
      if (frequency < 1 || frequency > 62) {
        errors.push_back(Error(pointer + "/frequency_index",
                               "fhss.frequency_index_out_of_range",
                               "frequency_index must be in [1,62]"));
      }
      if (pi < kPreamblePulseCount) {
        const auto word = pulse.value("value", std::uint64_t{0});
        const auto [entry, inserted] = preamble_words.emplace(frequency, word);
        if (!inserted && entry->second != word) {
          errors.push_back(Error(pointer + "/value",
                                 "preamble_word_inconsistent",
                                 "a repeated preamble frequency must use the "
                                 "same word value"));
        }
      } else if (std::find(active.begin(), active.end(), frequency) ==
                 active.end()) {
        errors.push_back(Error(pointer + "/frequency_index",
                               "payload_frequency_not_active",
                               "payload pulses must select a frequency "
                               "derived from the preamble"));
      }
      const auto expected_role = pi < kPreamblePulseCount ? "preamble" : "body";
      if (pulse.value("role", std::string{}) != expected_role) {
        errors.push_back(
            Error(pointer + "/role", "invalid_pulse_role",
                  "pulse role does not match its architecture position"));
      }
    }
    const auto start = message.value("transmit_start_sample", std::uint64_t{0});
    if (pulses.size() >
        std::numeric_limits<std::uint64_t>::max() / kPulsePeriodSamples) {
      errors.push_back(Error(prefix + "/pulses", "message_window_overflow",
                             "message sample window overflows uint64"));
      continue;
    }
    const auto duration =
        static_cast<std::uint64_t>(pulses.size()) * kPulsePeriodSamples;
    if (start > std::numeric_limits<std::uint64_t>::max() - duration) {
      errors.push_back(Error(prefix + "/transmit_start_sample",
                             "message_window_overflow",
                             "message sample window overflows uint64"));
      continue;
    }
    windows.push_back({start, start + duration, mi});
  }
  if (!authoritative.value("allow_overlap", false)) {
    std::sort(windows.begin(), windows.end(),
              [](const auto &left, const auto &right) {
                return left.begin < right.begin;
              });
    for (std::size_t i = 1; i < windows.size(); ++i) {
      if (windows[i].begin < windows[i - 1].end) {
        errors.push_back(Error("/messages/" + std::to_string(windows[i].index) +
                                   "/transmit_start_sample",
                               "scheduled_messages_overlap",
                               "message windows overlap; set allow_overlap to "
                               "true to permit summation"));
      }
    }
  }
  return {{"valid", errors.empty()}, {"levels", levels}, {"errors", errors}};
}

nlohmann::json FHSSDashboardConfigurationPolicy::GeneratedPaths() const {
  return nlohmann::json::array({"/active_frequency_indices", "/preamble_pulses",
                                "/nodes/72/node_config/preamble_pulses",
                                "/nodes/73/node_config/preamble_pulses",
                                "/dashboard_derived/active_frequency_indices",
                                "/dashboard_derived/preamble_pulses",
                                "/dashboard_derived/timing"});
}

nlohmann::json FHSSDashboardConfigurationPolicy::Provenance() const {
  nlohmann::json frequency_sources = nlohmann::json::array();
  nlohmann::json preamble_sources = nlohmann::json::array();
  for (std::size_t index = 0; index < kPreamblePulseCount; ++index) {
    const auto root = "/messages/0/pulses/" + std::to_string(index);
    frequency_sources.push_back(root + "/frequency_index");
    preamble_sources.push_back(root + "/frequency_index");
    preamble_sources.push_back(root + "/value");
  }
  nlohmann::json result = nlohmann::json::array();
  const auto append = [&](std::string target, std::string rule_id,
                          const nlohmann::json &sources, std::string units,
                          std::string rule,
                          std::string source_classification = "authoritative") {
    result.push_back(
        {{"architecture_version", "docs/dsp/fhss_architecture.md"},
         {"rule_id", std::move(rule_id)},
         {"rule", std::move(rule)},
         {"source_pointers", sources},
         {"target_pointer", target},
         {"units", std::move(units)},
         {"classification",
          {{"source", std::move(source_classification)},
           {"target", "generated"},
           {"mutability", "read-only"}}},
         {"warnings", nlohmann::json::array()}});
  };
  append("/active_frequency_indices", "fhss.active-set.root.v1",
         frequency_sources, "frequency_index",
         "stable first-occurrence de-duplication of the 16 preamble indices");
  append("/preamble_pulses", "fhss.preamble.root.v1", preamble_sources,
         "frequency_index+uint32_word",
         "project the first 16 frequency_index/value pairs");
  append("/nodes/72/node_config/preamble_pulses",
         "fhss.preamble.detector-config.v1", preamble_sources,
         "frequency_index+uint32_word",
         "project the canonical preamble into the detector configuration");
  append("/nodes/73/node_config/preamble_pulses",
         "fhss.preamble.assembler-config.v1", preamble_sources,
         "frequency_index+uint32_word",
         "project the canonical preamble into the assembler configuration");
  append("/dashboard_derived/active_frequency_indices",
         "fhss.active-set.dashboard.v1", frequency_sources,
         "frequency_index",
         "publish stable first-occurrence preamble frequency de-duplication");
  append("/dashboard_derived/preamble_pulses",
         "fhss.preamble.dashboard.v1", preamble_sources,
         "frequency_index+uint32_word",
         "publish the canonical 16-pulse preamble projection");
  append("/dashboard_derived/timing", "fhss.timing.architecture.v1",
         nlohmann::json::array(), "complex_samples",
         "apply architecture constants: 500 Msps, 5 Mbps, 32 bits, 3300-sample gap",
         "architecture");
  return result;
}

nlohmann::json FHSSDashboardConfigurationPolicy::ReceiverMinimalGraph(
    const nlohmann::json &effective) const {
  auto graph = effective;
  graph.erase("dashboard_derived");
  graph.erase("active_frequency_indices");
  graph.erase("preamble_pulses");
  StripTruth(graph);
  for (const auto id : {"preamble", "assembler"}) {
    if (auto *node = FindNode(graph, id)) {
      (*node)["node_config"].erase("active_frequency_indices");
    }
  }
  return graph;
}

std::optional<std::string> FHSSDashboardConfigurationPolicy::NormalizePatchPath(
    std::string_view path) const {
  constexpr std::string_view root = "/fhss/scenario";
  constexpr std::string_view alias = "/nodes/source/node_config";
  if (path.starts_with(root) &&
      (path.size() == root.size() || path[root.size()] == '/')) {
    return std::string(path.substr(root.size()));
  }
  if (path.starts_with(alias) &&
      (path.size() == alias.size() || path[alias.size()] == '/')) {
    return std::string(path.substr(alias.size()));
  }
  if (path.empty() || path.front() == '/')
    return std::string(path);
  return std::nullopt;
}

std::string FHSSDashboardConfigurationPolicy::AuthoritativeRootPointer() const {
  return "/fhss/scenario";
}

} // namespace dsp::fhss::dashboard
