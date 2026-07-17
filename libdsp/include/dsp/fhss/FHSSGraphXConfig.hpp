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
#include "dsp/fhss/FHSSMessageAssembly.hpp"
#include "dsp/fhss/FHSSSyntheticIqGenerator.hpp"

#include <cstdint>
#include <string>
#include <utility>
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

inline std::string FHSSJsonString(const nlohmann::json &json,
                                  const std::string &field,
                                  std::string fallback = {}) {
  if (!json.contains(field)) {
    return fallback;
  }
  if (!json.at(field).is_string()) {
    throw graph::ConfigError("FHSS config field '" + field +
                             "' must be a string");
  }
  return json.at(field).get<std::string>();
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

inline FHSSFrequencyConfig
FHSSFrequencyConfigFromJson(const nlohmann::json &json) {
  FHSSFrequencyConfig frequency{};
  frequency.occupied_bandwidth_hz =
      FHSSJsonDouble(json, "occupied_bandwidth_hz", 5'000'000.0);
  frequency.max_abs_cfo_hz = FHSSJsonDouble(json, "max_abs_cfo_hz", 1'000.0);

  if (json.contains("iq_center_frequency_hz")) {
    const double center = FHSSJsonDouble(json, "iq_center_frequency_hz");
    for (std::uint32_t index = 0;
         index < FHSSProtocolConstants::kFrequencyCount; ++index) {
      frequency.iq_offset_frequency_hz[index] =
          RfFrequencyHz(index, frequency) - center;
    }
  }

  if (json.contains("iq_offsets")) {
    if (!json.at("iq_offsets").is_array()) {
      throw graph::ConfigError(
          "FHSS config field 'iq_offsets' must be an array");
    }
    for (const auto &entry : json.at("iq_offsets")) {
      if (!entry.is_object()) {
        throw graph::ConfigError("FHSS iq_offsets entries must be objects");
      }
      const auto index = FHSSJsonUint32(entry, "index");
      if (index >= frequency.iq_offset_frequency_hz.size()) {
        throw graph::ConfigError("FHSS iq offset index is outside [0, 63]");
      }
      frequency.iq_offset_frequency_hz[index] =
          FHSSJsonDouble(entry, "iq_offset_frequency_hz");
    }
  }

  return frequency;
}

inline std::vector<FHSSPreamblePulseSpec>
FHSSPreamblePulseSpecsFromJsonArray(const nlohmann::json &json) {
  if (!json.is_array()) {
    throw graph::ConfigError(
        "FHSS config field 'preamble_pulses' must be an array");
  }

  std::vector<FHSSPreamblePulseSpec> preamble_pulses;
  preamble_pulses.reserve(json.size());
  for (const auto &pulse : json) {
    if (!pulse.is_object()) {
      throw graph::ConfigError("FHSS preamble_pulses entries must be objects");
    }
    preamble_pulses.push_back(FHSSPreamblePulseSpec{
        .frequency_index = FHSSJsonUint32(pulse, "frequency_index"),
        .word_value = FHSSJsonUint32(pulse, "word_value")});
  }
  return preamble_pulses;
}

inline FHSSMessagePulseRole
FHSSMessagePulseRoleFromJson(const nlohmann::json &json) {
  if (json.contains("role")) {
    const auto role = FHSSJsonString(json, "role");
    if (role == "preamble") {
      return FHSSMessagePulseRole::Preamble;
    }
    if (role == "body" || role == "payload") {
      return FHSSMessagePulseRole::Body;
    }
    throw graph::ConfigError(
        "FHSS message pulse role must be 'preamble' or 'body'");
  }
  if (json.contains("is_preamble")) {
    return FHSSJsonBool(json, "is_preamble") ? FHSSMessagePulseRole::Preamble
                                             : FHSSMessagePulseRole::Body;
  }
  throw graph::ConfigError(
      "FHSS message pulse must specify 'role' or 'is_preamble'");
}

inline std::vector<FHSSScheduledMessageSpec>
FHSSMessagesFromJson(const nlohmann::json &json) {
  if (!json.contains("messages") || !json.at("messages").is_array()) {
    throw graph::ConfigError("FHSS config field 'messages' must be an array");
  }

  std::vector<FHSSScheduledMessageSpec> messages;
  messages.reserve(json.at("messages").size());
  for (const auto &message_json : json.at("messages")) {
    if (!message_json.is_object()) {
      throw graph::ConfigError("FHSS messages entries must be objects");
    }
    if (!message_json.contains("pulses") ||
        !message_json.at("pulses").is_array()) {
      throw graph::ConfigError("FHSS message field 'pulses' must be an array");
    }
    FHSSScheduledMessageSpec message{};
    message.message_id = FHSSJsonUint64(message_json, "message_id");
    message.transmit_start_sample =
        FHSSJsonUint64(message_json, "transmit_start_sample", 0);
    message.pulses.reserve(message_json.at("pulses").size());
    for (const auto &pulse_json : message_json.at("pulses")) {
      if (!pulse_json.is_object()) {
        throw graph::ConfigError("FHSS message pulse entries must be objects");
      }
      message.pulses.push_back(FHSSMessagePulseSpec{
          .frequency_index = FHSSJsonUint32(pulse_json, "frequency_index"),
          .value = FHSSJsonUint32(pulse_json, "value"),
          .role = FHSSMessagePulseRoleFromJson(pulse_json)});
    }
    messages.push_back(std::move(message));
  }
  return messages;
}

inline FHSSVector3Meters FHSSVector3MetersFromJson(const nlohmann::json &json) {
  if (!json.is_object()) {
    throw graph::ConfigError("FHSS vector3 entries must be objects");
  }
  return FHSSVector3Meters{.x = FHSSJsonDouble(json, "x", 0.0),
                           .y = FHSSJsonDouble(json, "y", 0.0),
                           .z = FHSSJsonDouble(json, "z", 0.0)};
}

inline FHSSRealisticIqConfig
FHSSRealisticIqConfigFromJson(const nlohmann::json &json) {
  FHSSRealisticIqConfig config{};
  if (!json.contains("realistic")) {
    return config;
  }
  const auto &realistic = json.at("realistic");
  if (!realistic.is_object()) {
    throw graph::ConfigError("FHSS config field 'realistic' must be an object");
  }
  config.enabled = FHSSJsonBool(realistic, "enabled", false);
  config.rng_seed = FHSSJsonUint64(realistic, "rng_seed", 0);
  config.missing_pulse_probability =
      FHSSJsonDouble(realistic, "missing_pulse_probability", 0.0);
  config.timing_jitter_stddev_samples =
      FHSSJsonDouble(realistic, "timing_jitter_stddev_samples", 0.0);
  config.apply_propagation_delay =
      FHSSJsonBool(realistic, "apply_propagation_delay", true);
  config.apply_path_loss = FHSSJsonBool(realistic, "apply_path_loss", true);
  config.apply_doppler = FHSSJsonBool(realistic, "apply_doppler", true);
  config.reference_range_m =
      FHSSJsonDouble(realistic, "reference_range_m", config.reference_range_m);
  config.minimum_range_m =
      FHSSJsonDouble(realistic, "minimum_range_m", config.minimum_range_m);

  if (realistic.contains("receiver")) {
    const auto &receiver = realistic.at("receiver");
    if (!receiver.is_object()) {
      throw graph::ConfigError("FHSS realistic.receiver must be an object");
    }
    if (receiver.contains("position_m")) {
      config.receiver.position_m =
          FHSSVector3MetersFromJson(receiver.at("position_m"));
    }
  }

  if (realistic.contains("transmitter_paths")) {
    const auto &paths = realistic.at("transmitter_paths");
    if (!paths.is_array()) {
      throw graph::ConfigError(
          "FHSS realistic.transmitter_paths must be an array");
    }
    config.transmitter_paths.reserve(paths.size());
    for (const auto &path_json : paths) {
      if (!path_json.is_object()) {
        throw graph::ConfigError(
            "FHSS realistic transmitter path entries must be objects");
      }
      FHSSRealisticTransmitterPath path{};
      path.message_id = FHSSJsonUint64(path_json, "message_id", 0);
      if (path_json.contains("waypoints")) {
        const auto &waypoints = path_json.at("waypoints");
        if (!waypoints.is_array()) {
          throw graph::ConfigError(
              "FHSS realistic transmitter path waypoints must be an array");
        }
        path.waypoints.reserve(waypoints.size());
        for (const auto &waypoint_json : waypoints) {
          if (!waypoint_json.is_object()) {
            throw graph::ConfigError(
                "FHSS realistic waypoint entries must be objects");
          }
          FHSSMotionWaypoint waypoint{};
          waypoint.time_seconds =
              FHSSJsonDouble(waypoint_json, "time_seconds", 0.0);
          if (waypoint_json.contains("position_m")) {
            waypoint.position_m =
                FHSSVector3MetersFromJson(waypoint_json.at("position_m"));
          }
          path.waypoints.push_back(std::move(waypoint));
        }
      }
      config.transmitter_paths.push_back(std::move(path));
    }
  }

  return config;
}

inline FHSSDecodeConfig FHSSDecodeConfigFromJson(const graph::JsonView &cfg) {
  const auto &json = cfg.Raw();
  FHSSDecodeConfig config{};
  config.frequency = FHSSFrequencyConfigFromJson(json);

  config.active_frequency_indices =
      FHSSJsonUint32Array(json, "active_frequency_indices");

  if (!json.contains("preamble_pulses")) {
    throw graph::ConfigError(
        "FHSS config field 'preamble_pulses' must be an array");
  }
  config.preamble_pulses =
      FHSSPreamblePulseSpecsFromJsonArray(json.at("preamble_pulses"));

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
  config.decode_config.frequency = FHSSFrequencyConfigFromJson(json);
  config.decode_config.active_frequency_indices =
      FHSSJsonUint32Array(json, "active_frequency_indices");
  config.messages = FHSSMessagesFromJson(json);
  if (!config.messages.empty()) {
    config.decode_config.preamble_pulses =
        PreamblePatternFromMessage(config.messages.front());
  }
  const auto idle_mode = FHSSJsonString(json, "idle_mode", "zero");
  if (idle_mode != "zero" && idle_mode != "null") {
    throw graph::ConfigError(
        "FHSS PR10 source idle_mode must be 'zero' or 'null'");
  }
  config.idle_duration_samples =
      FHSSJsonUint64(json, "idle_duration_samples", 0);
  config.enable_noise = FHSSJsonBool(json, "enable_noise", false);
  config.enable_doppler = FHSSJsonBool(json, "enable_doppler", false);
  config.enable_multipath = FHSSJsonBool(json, "enable_multipath", false);
  config.allow_overlap = FHSSJsonBool(json, "allow_overlap", false);
  config.realistic = FHSSRealisticIqConfigFromJson(json);
  return config;
}

inline std::vector<FHSSPreamblePulseSpec>
FHSSPreamblePulseSpecsFromJson(const graph::JsonView &cfg) {
  return FHSSDecodeConfigFromJson(cfg).preamble_pulses;
}

inline FHSSMessageAssemblerConfig
FHSSMessageAssemblerConfigFromJson(const graph::JsonView &cfg) {
  FHSSMessageAssemblerConfig config{};
  config.preamble_pulses = FHSSDecodeConfigFromJson(cfg).preamble_pulses;
  return config;
}

inline const std::vector<std::string> &FHSSFixtureParameterNames() {
  static const std::vector<std::string> names{
      "active_frequency_indices",
      "preamble_pulses",
      "messages",
      "idle_mode",
      "idle_duration_samples",
      "iq_center_frequency_hz",
      "iq_offsets",
      "occupied_bandwidth_hz",
      "max_abs_cfo_hz",
      "enable_noise",
      "enable_doppler",
      "enable_multipath",
      "allow_overlap",
      "realistic",
      "input_packet_global_start_sample",
      "message_start_sample",
      "detector_id",
      "packet_sequence",
  };
  return names;
}

inline graph::JsonView FHSSFixtureParametersJson() {
  static thread_local nlohmann::json params;
  params = nlohmann::json::object({
      {"active_frequency_indices", nlohmann::json::array()},
      {"preamble_pulses", nlohmann::json::array()},
      {"messages", nlohmann::json::array()},
      {"idle_mode", "zero"},
      {"idle_duration_samples", 0},
      {"iq_center_frequency_hz", 0.0},
      {"iq_offsets", nlohmann::json::array()},
      {"occupied_bandwidth_hz", 5'000'000.0},
      {"max_abs_cfo_hz", 1'000.0},
      {"enable_noise", false},
      {"enable_doppler", false},
      {"enable_multipath", false},
      {"allow_overlap", false},
      {"realistic", nlohmann::json::object()},
      {"input_packet_global_start_sample", 0},
      {"message_start_sample", 0},
      {"detector_id", 0},
      {"packet_sequence", 0},
  });
  return graph::JsonView(params);
}

inline graph::JsonView
FHSSFixtureParameterDescription(const std::string &param_name) {
  static thread_local nlohmann::json description;
  const bool is_array =
      param_name == "active_frequency_indices" || param_name == "messages" ||
      param_name == "preamble_pulses" || param_name == "iq_offsets";
  const bool is_object = param_name == "realistic";
  const bool is_bool =
      param_name == "enable_noise" || param_name == "enable_doppler" ||
      param_name == "enable_multipath" || param_name == "allow_overlap";
  const bool is_number = param_name == "occupied_bandwidth_hz" ||
                         param_name == "max_abs_cfo_hz" ||
                         param_name == "iq_center_frequency_hz";
  const bool is_string = param_name == "idle_mode";
  description = {
      {"type",
       is_array
           ? "array"
           : (is_object ? "object"
                        : (is_bool ? "boolean"
                                   : (is_number ? "number"
                                                : (is_string ? "string"
                                                             : "integer"))))},
      {"required", false},
      {"description", "FHSS deterministic fixture GraphX node configuration"},
  };
  return graph::JsonView(description);
}

} // namespace dsp::fhss
