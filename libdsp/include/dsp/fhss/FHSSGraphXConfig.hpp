/**
 * @file FHSSGraphXConfig.hpp
 * @brief JSON configuration helpers for FHSS GraphX runtime nodes.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#pragma once

#include "config/ConfigError.hpp"
#include "config/JsonView.hpp"
#include "dsp/fhss/FHSSCorrelatorBankDetector.hpp"
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dsp::fhss {

inline std::uint32_t FHSSJsonUint32(const nlohmann::json &json,
                                    const std::string &field) {
  if (!json.contains(field) || !json.at(field).is_number_unsigned()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be an unsigned integer");
  }
  return json.at(field).get<std::uint32_t>();
}

inline std::uint64_t FHSSJsonUint64(const nlohmann::json &json,
                                    const std::string &field,
                                    std::uint64_t fallback = 0) {
  if (!json.contains(field)) {
    return fallback;
  }
  if (!json.at(field).is_number_unsigned()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be an unsigned integer");
  }
  return json.at(field).get<std::uint64_t>();
}

inline double FHSSJsonDouble(const nlohmann::json &json,
                             const std::string &field, double fallback = 0.0) {
  if (!json.contains(field)) {
    return fallback;
  }
  if (!json.at(field).is_number()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be a number");
  }
  return json.at(field).get<double>();
}

inline bool FHSSJsonBool(const nlohmann::json &json, const std::string &field,
                         bool fallback = false) {
  if (!json.contains(field)) {
    return fallback;
  }
  if (!json.at(field).is_boolean()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be a boolean");
  }
  return json.at(field).get<bool>();
}

inline std::vector<std::uint32_t>
FHSSJsonUint32Array(const nlohmann::json &json, const std::string &field) {
  if (!json.contains(field) || !json.at(field).is_array()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be an array");
  }

  std::vector<std::uint32_t> values;
  values.reserve(json.at(field).size());
  for (const auto &value : json.at(field)) {
    if (!value.is_number_unsigned()) {
      throw graph::ConfigError("FHSS config array '" + field +
                               "' must contain unsigned integers");
    }
    values.push_back(value.get<std::uint32_t>());
  }
  return values;
}

inline FHSSDecodeConfig
FHSSDecodeConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSDecodeConfig config{};
  config.frequency.occupied_bandwidth_hz =
      FHSSJsonDouble(json, "occupied_bandwidth_hz", 5'000'000.0);
  config.frequency.max_abs_cfo_hz =
      FHSSJsonDouble(json, "max_abs_cfo_hz", 1'000.0);

  if (json.contains("iq_offsets")) {
    if (!json.at("iq_offsets").is_array()) {
      throw graph::ConfigError("FHSS config field 'iq_offsets' must be an array");
    }
    for (const auto &entry : json.at("iq_offsets")) {
      if (!entry.is_object()) {
        throw graph::ConfigError("FHSS iq_offsets entries must be objects");
      }
      const auto index = FHSSJsonUint32(entry, "index");
      if (index >= config.frequency.iq_offset_frequency_hz.size()) {
        throw graph::ConfigError("FHSS iq offset index is outside [0, 63]");
      }
      config.frequency.iq_offset_frequency_hz[index] =
          FHSSJsonDouble(entry, "iq_offset_frequency_hz");
    }
  }

  config.active_frequency_indices =
      FHSSJsonUint32Array(json, "active_frequency_indices");

  if (!json.contains("preamble_pulses") ||
      !json.at("preamble_pulses").is_array()) {
    throw graph::ConfigError(
        "FHSS config field 'preamble_pulses' must be an array");
  }
  config.preamble_pulses.reserve(json.at("preamble_pulses").size());
  for (const auto &pulse : json.at("preamble_pulses")) {
    if (!pulse.is_object()) {
      throw graph::ConfigError("FHSS preamble_pulses entries must be objects");
    }
    config.preamble_pulses.push_back(FHSSPreamblePulseSpec{
        .frequency_index = FHSSJsonUint32(pulse, "frequency_index"),
        .word_value = FHSSJsonUint32(pulse, "word_value")});
  }

  config.payload_random.rng_seed =
      FHSSJsonUint64(json, "payload_random_seed", 0);
  config.payload_random.deterministic =
      FHSSJsonBool(json, "payload_random_deterministic", true);
  return config;
}

inline FHSSSyntheticIqGeneratorConfig
FHSSSyntheticIqGeneratorConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSSyntheticIqGeneratorConfig config{};
  config.decode_config = FHSSDecodeConfigFromJson(cfg);
  config.payload_values = FHSSJsonUint32Array(json, "payload_values");
  config.enable_noise = FHSSJsonBool(json, "enable_noise", false);
  config.enable_doppler = FHSSJsonBool(json, "enable_doppler", false);
  config.enable_multipath = FHSSJsonBool(json, "enable_multipath", false);
  config.allow_overlap = FHSSJsonBool(json, "allow_overlap", false);
  return config;
}

inline FHSSCorrelatorBankDetectorConfig
FHSSCorrelatorBankDetectorConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSCorrelatorBankDetectorConfig config{};
  config.decode_config = FHSSDecodeConfigFromJson(cfg);
  config.input_packet_global_start_sample =
      FHSSJsonUint64(json, "input_packet_global_start_sample", 0);
  config.message_start_sample =
      FHSSJsonUint64(json, "message_start_sample", 0);
  config.detector_id = FHSSJsonUint64(json, "detector_id", 0);
  config.packet_sequence = FHSSJsonUint64(json, "packet_sequence", 0);
  config.allow_overlap = FHSSJsonBool(json, "allow_overlap", false);
  return config;
}

inline std::vector<FHSSPreamblePulseSpec>
FHSSPreamblePulseSpecsFromJson(const graph::JsonView &cfg) {
  return FHSSDecodeConfigFromJson(cfg).preamble_pulses;
}

inline FHSSMessageAssemblerConfig
FHSSMessageAssemblerConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSMessageAssemblerConfig config{};
  config.preamble_pulses = FHSSDecodeConfigFromJson(cfg).preamble_pulses;
  if (json.contains("truth_from_fixture") &&
      FHSSJsonBool(json, "truth_from_fixture", false)) {
    auto generator_config = FHSSSyntheticIqGeneratorConfigFromJson(cfg);
    auto fixture = GenerateSyntheticIqFixture(generator_config);
    if (!fixture) {
      throw graph::ConfigError("FHSS truth_from_fixture generation failed: " +
                               fixture.error().message);
    }
    config.truth_pulses = std::move(fixture->truth_pulses);
  }
  return config;
}

inline const std::vector<std::string> &FHSSFixtureParameterNames() {
  static const std::vector<std::string> names{
      "active_frequency_indices",
      "preamble_pulses",
      "payload_values",
      "payload_random_seed",
      "payload_random_deterministic",
      "iq_offsets",
      "occupied_bandwidth_hz",
      "max_abs_cfo_hz",
      "enable_noise",
      "enable_doppler",
      "enable_multipath",
      "allow_overlap",
      "input_packet_global_start_sample",
      "message_start_sample",
      "detector_id",
      "packet_sequence",
      "truth_from_fixture",
  };
  return names;
}

inline graph::JsonView FHSSFixtureParametersJson() {
  static thread_local nlohmann::json params;
  params = nlohmann::json::object({
      {"active_frequency_indices", nlohmann::json::array()},
      {"preamble_pulses", nlohmann::json::array()},
      {"payload_values", nlohmann::json::array()},
      {"payload_random_seed", 0},
      {"payload_random_deterministic", true},
      {"iq_offsets", nlohmann::json::array()},
      {"occupied_bandwidth_hz", 5'000'000.0},
      {"max_abs_cfo_hz", 1'000.0},
      {"enable_noise", false},
      {"enable_doppler", false},
      {"enable_multipath", false},
      {"allow_overlap", false},
      {"input_packet_global_start_sample", 0},
      {"message_start_sample", 0},
      {"detector_id", 0},
      {"packet_sequence", 0},
      {"truth_from_fixture", false},
  });
  return graph::JsonView(params);
}

inline graph::JsonView
FHSSFixtureParameterDescription(const std::string &param_name) {
  static thread_local nlohmann::json description;
  const bool is_array = param_name == "active_frequency_indices" ||
                        param_name == "preamble_pulses" ||
                        param_name == "payload_values" ||
                        param_name == "iq_offsets";
  const bool is_bool = param_name == "payload_random_deterministic" ||
                       param_name == "enable_noise" ||
                       param_name == "enable_doppler" ||
                       param_name == "enable_multipath" ||
                       param_name == "allow_overlap" ||
                       param_name == "truth_from_fixture";
  const bool is_number = param_name == "occupied_bandwidth_hz" ||
                         param_name == "max_abs_cfo_hz";
  description = {
      {"type", is_array ? "array" : (is_bool ? "boolean" :
                   (is_number ? "number" : "integer"))},
      {"required", false},
      {"description", "FHSS deterministic fixture GraphX node configuration"},
  };
  return graph::JsonView(description);
}

} // namespace dsp::fhss
