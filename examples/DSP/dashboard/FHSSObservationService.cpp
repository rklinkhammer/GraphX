// SPDX-License-Identifier: MIT
#include "FHSSObservationService.hpp"

#include "dsp/fhss/FHSSAcquisitionPulseDetectorNode.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "dsp/fhss/FHSSProductionCandidateChannelizerNode.hpp"
#include "dsp/fhss/FHSSProtocol.hpp"
#include "dsp/fhss/FHSSReceiverObservationSource.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterSpecializations.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace dsp::fhss::dashboard {
namespace {

class Sha256 {
public:
  void Update(std::span<const std::byte> bytes) {
    bit_count_ += static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (const auto byte : bytes) {
      buffer_[buffer_size_++] = std::to_integer<std::uint8_t>(byte);
      if (buffer_size_ == buffer_.size()) {
        Transform(buffer_);
        buffer_size_ = 0;
      }
    }
  }
  [[nodiscard]] std::string Finish() {
    const auto message_bits = bit_count_;
    buffer_[buffer_size_++] = 0x80u;
    if (buffer_size_ > 56u) {
      while (buffer_size_ < buffer_.size())
        buffer_[buffer_size_++] = 0u;
      Transform(buffer_);
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56u)
      buffer_[buffer_size_++] = 0u;
    for (int shift = 56; shift >= 0; shift -= 8)
      buffer_[buffer_size_++] =
          static_cast<std::uint8_t>(message_bits >> shift);
    Transform(buffer_);
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (const auto word : state_)
      result << std::setw(8) << word;
    return result.str();
  }

private:
  static constexpr std::array<std::uint32_t, 64> kRoundConstants{
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  static std::uint32_t RotateRight(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
  }
  void Transform(const std::array<std::uint8_t, 64> &block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i)
      words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16u) |
                 (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8u) |
                 static_cast<std::uint32_t>(block[i * 4 + 3]);
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto s0 = RotateRight(words[i - 15], 7) ^
                      RotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3u);
      const auto s1 = RotateRight(words[i - 2], 17) ^
                      RotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10u);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const auto s1 =
          RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
      const auto choose = (e & f) ^ ((~e) & g);
      const auto temp1 = h + s1 + choose + kRoundConstants[i] + words[i];
      const auto s0 =
          RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
      const auto temp2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
  std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                      0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                      0x1f83d9abu, 0x5be0cd19u};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_size_ = 0;
  std::uint64_t bit_count_ = 0;
};

std::string HashJson(const nlohmann::json &value) {
  const auto serialized = value.dump();
  Sha256 hash;
  hash.Update(std::as_bytes(std::span(serialized.data(), serialized.size())));
  return hash.Finish();
}

nlohmann::json Available() {
  return FHSSObservableAvailability{FHSSAvailabilityState::available, ""}
      .ToJson();
}
nlohmann::json Unavailable(std::string reason) {
  return FHSSObservableAvailability{FHSSAvailabilityState::unavailable,
                                    std::move(reason)}
      .ToJson();
}

std::shared_ptr<dsp::fhss::IFHSSReceiverObservationSource>
TryGetObservationSource(const std::shared_ptr<graph::INode> &node) {
  if (const auto wrapper =
          std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node)) {
    if (auto channelizer =
            wrapper
                ->GetNode<dsp::fhss::FHSSProductionCandidateChannelizerNode>())
      return channelizer;
    if (auto detector =
            wrapper->GetNode<dsp::fhss::FHSSAcquisitionPulseDetectorNode>())
      return detector;
    if (auto sink = wrapper->GetNode<dsp::fhss::FHSSMessageSinkNode>())
      return sink;
  }
  return std::dynamic_pointer_cast<dsp::fhss::IFHSSReceiverObservationSource>(
      node);
}

std::string NodeName(const std::shared_ptr<graph::INode> &node,
                     std::size_t index) {
  if (const auto wrapper =
          std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node)) {
    const auto name = wrapper->GetName();
    if (!name.empty())
      return name;
  }
  return "node_" + std::to_string(index);
}
std::string NodeClass(const std::shared_ptr<graph::INode> &node) {
  if (const auto wrapper =
          std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node)) {
    const auto type = wrapper->GetType();
    if (!type.empty())
      return type;
  }
  return "unknown";
}

nlohmann::json ScenarioConfig(const nlohmann::json &scenario) {
  if (scenario.contains("messages"))
    return scenario;
  if (scenario.contains("receiver_input") &&
      scenario.at("receiver_input").is_object())
    return scenario.at("receiver_input");
  if (scenario.contains("nodes") && scenario.at("nodes").is_array()) {
    for (const auto &node : scenario.at("nodes")) {
      if (node.value("id", std::string{}) == "source" &&
          node.contains("node_config"))
        return node.at("node_config");
    }
  }
  return nlohmann::json::object();
}

bool IsPowerOfTwo(std::size_t value) {
  return value != 0 && std::has_single_bit(value);
}

} // namespace

nlohmann::json FHSSObservableAvailability::ToJson() const {
  return {{"state", state == FHSSAvailabilityState::available ? "available"
                                                              : "unavailable"},
          {"reason",
           reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(reason)}};
}

nlohmann::json FHSSObservationProvenance::ToJson() const {
  return {{"generation", generation},
          {"run_epoch", run_epoch},
          {"node_id", node_id},
          {"node_class", node_class},
          {"source_schema", source_schema},
          {"packet_field", packet_field},
          {"sample_interval_availability",
           sample_interval.empty()
               ? Unavailable("not_carried_by_receiver_product")
               : Available()},
          {"sample_interval", sample_interval.empty()
                                  ? nlohmann::json(nullptr)
                                  : nlohmann::json(sample_interval)},
          {"unit", unit},
          {"capture_time_availability",
           capture_time.empty() ? Unavailable("not_carried_by_receiver_product")
                                : Available()},
          {"capture_time", capture_time.empty() ? nlohmann::json(nullptr)
                                                : nlohmann::json(capture_time)},
          {"transformation", transformation}};
}

FHSSObservationService::FHSSObservationService(
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session)
    : configuration_service_(std::move(configuration_service)),
      runtime_session_(std::move(runtime_session)) {}

FHSSExpectedTruth FHSSObservationService::ExpectedTruth() const {
  const auto response = configuration_service_->GetScenarioResponse();
  const auto config =
      ScenarioConfig(response.value("scenario", nlohmann::json::object()));
  nlohmann::json pulses = nlohmann::json::array();
  nlohmann::json messages = nlohmann::json::array();
  const FHSSTimingConfig timing_config{
      .sample_rate_hz =
          config.value("sample_rate_hz", FHSSProtocolConstants::kSampleRateHz),
      .bit_rate_hz =
          config.value("bit_rate_hz", FHSSProtocolConstants::kBitRateHz),
      .bits_per_pulse =
          config.value("bits_per_pulse", FHSSProtocolConstants::kBitsPerPulse),
      .pulse_gap_seconds = config.value(
          "pulse_gap_seconds", FHSSProtocolConstants::kPulseGapSeconds)};
  const auto timing = DeriveTimingModel(timing_config);
  if (!timing) {
    throw std::invalid_argument("invalid evaluator FHSS timing: " +
                                timing.error().message);
  }
  std::size_t original_pulse_count = 0;
  const auto scheduled = config.value("messages", nlohmann::json::array());
  for (std::size_t message_index = 0; message_index < scheduled.size();
       ++message_index) {
    const auto &message = scheduled.at(message_index);
    const auto start = message.value("transmit_start_sample", std::uint64_t{0});
    const auto message_id =
        message.value("message_id", static_cast<std::uint64_t>(message_index));
    const auto message_pulses =
        message.value("pulses", nlohmann::json::array());
    if (messages.size() < kMaxExpectedMessages) {
      messages.push_back({{"message_id", message_id},
                          {"transmit_start_sample", start},
                          {"pulse_count", message_pulses.size()}});
    }
    for (std::size_t pulse_index = 0; pulse_index < message_pulses.size();
         ++pulse_index) {
      const auto &pulse = message_pulses.at(pulse_index);
      ++original_pulse_count;
      if (pulses.size() >= kMaxObservedPulses)
        continue;
      if (pulse_index > (std::numeric_limits<std::uint64_t>::max() - start) /
                            timing->pulse_period_samples) {
        throw std::overflow_error("expected pulse start exceeds uint64 range");
      }
      const auto pulse_start =
          start + pulse_index * timing->pulse_period_samples;
      pulses.push_back(
          {{"expected_index", pulses.size()},
           {"message_id", message_id},
           {"pulse_index", pulse_index},
           {"global_start_sample", pulse_start},
           {"duration_samples", timing->pulse_width_samples},
           {"logical_frequency_index",
            pulse.value("frequency_index", std::uint32_t{0})},
           {"transmitted_word", pulse.value("value", std::uint32_t{0})},
           {"role", pulse.value("role", std::string{"body"})}});
    }
  }
  const auto returned_message_count = messages.size();
  const auto returned_pulse_count = pulses.size();
  nlohmann::json document{
      {"schema", "graphx.dashboard.fhss_expected_truth.v1"},
      {"semantic_class", "expected"},
      {"scenario_id", "config-revision-" + std::to_string(response.value(
                                               "config_revision", 0u))},
      {"config_revision", response.value("config_revision", 0u)},
      {"config_etag", response.value("etag", std::string{})},
      {"timing_basis",
       {{"unit", "input_samples"},
        {"sample_rate_hz", timing_config.sample_rate_hz},
        {"bit_rate_hz", timing_config.bit_rate_hz},
        {"bits_per_pulse", timing_config.bits_per_pulse},
        {"pulse_gap_seconds", timing_config.pulse_gap_seconds},
        {"pulse_duration_samples", timing->pulse_width_samples},
        {"inter_pulse_gap_samples", timing->pulse_gap_samples},
        {"slot_samples", timing->pulse_period_samples},
        {"derivation", "dsp::fhss::DeriveTimingModel"},
        {"architecture_rule", "fhss-pr1-timing"}}},
      {"messages", std::move(messages)},
      {"pulses", std::move(pulses)},
      {"expected_receiver_message",
       {{"accepted", original_pulse_count > 0},
        {"decoded_pulse_count", returned_pulse_count},
        {"status_class", original_pulse_count > 0 ? "accepted_message"
                                                  : "no_message_expected"}}},
      {"synthetic_impairments",
       {{"noise_enabled", config.value("enable_noise", false)},
        {"doppler_enabled", config.value("enable_doppler", false)},
        {"multipath_enabled", config.value("enable_multipath", false)},
        {"declared_only", true}}},
      {"bounds",
       {{"max_pulses", kMaxObservedPulses},
        {"max_messages", kMaxExpectedMessages},
        {"original_message_count", scheduled.size()},
        {"returned_message_count", returned_message_count},
        {"messages_truncated", scheduled.size() > returned_message_count},
        {"original_pulse_count", original_pulse_count},
        {"returned_pulse_count", returned_pulse_count},
        {"pulses_truncated", original_pulse_count > returned_pulse_count}}}};
  document["truth_sha256"] = HashJson(document);
  return {std::move(document)};
}

FHSSReceiverObservation FHSSObservationService::ReceiverObservation() const {
  const auto generation = runtime_session_->SnapshotGeneration();
  const auto status = runtime_session_->SnapshotStatus();
  const auto scenario_response = configuration_service_->GetScenarioResponse();
  const auto scenario_document =
      scenario_response.value("scenario", nlohmann::json::object());
  const auto receiver_config =
      scenario_document.contains("receiver_input") &&
              scenario_document.at("receiver_input").is_object()
          ? scenario_document.at("receiver_input")
          : ScenarioConfig(scenario_document);
  const bool observation_export_disabled =
      scenario_response.value("config_revision", std::uint64_t{0}) ==
          generation.config_revision &&
      scenario_response.value("etag", std::string{}) ==
          generation.config_etag &&
      !receiver_config.value("dashboard_observation_enabled", true);
  const auto missing_source_reason = [&] {
    if (observation_export_disabled)
      return std::string("observation_export_disabled");
    return generation.graph_manager ? std::string("source_not_diagnosable")
                                    : std::string("generation_not_available");
  };
  nlohmann::json pulses = nlohmann::json::array();
  nlohmann::json provenance = nlohmann::json::array();
  nlohmann::json sources = nlohmann::json::array();
  nlohmann::json active_frequencies{
      {"availability",
       Unavailable(generation.graph_manager ? "no_candidate_detected"
                                            : "generation_not_available")},
      {"indices", nlohmann::json::array()}};
  std::set<std::string> rejection_reason_codes;
  nlohmann::json preamble{
      {"availability", Unavailable(missing_source_reason())}};
  nlohmann::json assembler{
      {"availability", Unavailable(missing_source_reason())}};
  nlohmann::json receiver_message_result{
      {"availability", Unavailable(missing_source_reason())}};
  std::optional<std::uint64_t> sink_detected_count;
  std::optional<std::uint64_t> sink_rejected_count;
  std::uint64_t detector_detected_count = 0;
  std::uint64_t detector_rejected_count = 0;
  bool detector_counts_available = false;
  std::optional<double> receiver_sample_rate_hz;
  std::optional<double> global_input_sample_rate_hz;
  std::optional<std::uint32_t> input_samples_per_capture_sample;
  bool receiver_sample_rate_conflict = false;
  std::size_t original_pulse_count = 0;
  bool any_source = false;
  // Receiver packets currently carry exact sample-time products but no wall
  // clock capture timestamp. Do not substitute runtime lifecycle timestamps.
  const std::string capture_time;

  if (generation.graph_manager && !observation_export_disabled) {
    const auto &nodes = generation.graph_manager->GetNodes();
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      const auto source = TryGetObservationSource(nodes[index]);
      if (!source)
        continue;
      const auto snapshot = source->SnapshotReceiverObservation();
      if (!snapshot)
        continue;
      any_source = true;
      const auto node_id = NodeName(nodes[index], index);
      const auto node_class = NodeClass(nodes[index]);
      if (sources.size() < kMaxObservationSources) {
        sources.push_back({{"node_id", node_id},
                           {"node_class", node_class},
                           {"source_schema", snapshot->source_schema},
                           {"source_kind", snapshot->source_kind}});
      }
      if (snapshot->source_kind == "message_sink") {
        if (snapshot->detected_pulse_count)
          sink_detected_count = *snapshot->detected_pulse_count;
        if (snapshot->rejected_count)
          sink_rejected_count = *snapshot->rejected_count;
      } else if (snapshot->source_kind == "acquisition_detector") {
        detector_counts_available = true;
        if (snapshot->detected_pulse_count)
          detector_detected_count += *snapshot->detected_pulse_count;
        if (snapshot->rejected_count)
          detector_rejected_count += *snapshot->rejected_count;
      }
      rejection_reason_codes.insert(snapshot->rejection_reason_codes.begin(),
                                    snapshot->rejection_reason_codes.end());
      if (snapshot->preamble_lock) {
        preamble = {{"availability", Available()},
                    {"locked", *snapshot->preamble_lock}};
      }
      if (snapshot->assembler_status) {
        assembler = {{"availability", Available()},
                     {"status", *snapshot->assembler_status}};
      }
      if (snapshot->receiver_message_status &&
          snapshot->receiver_message_accepted) {
        receiver_message_result = {
            {"availability", Available()},
            {"status", *snapshot->receiver_message_status},
            {"accepted", *snapshot->receiver_message_accepted},
            {"decoded_pulse_count", snapshot->decoded_pulses.size()}};
      }
      if (!snapshot->receiver_derived_active_frequencies.empty()) {
        active_frequencies = {
            {"availability", Available()},
            {"indices", snapshot->receiver_derived_active_frequencies}};
      }
      original_pulse_count += snapshot->decoded_pulses.size();
      for (const auto &pulse : snapshot->decoded_pulses) {
        if (pulses.size() >= kMaxObservedPulses)
          break;
        const auto observed_index = pulses.size();
        pulses.push_back(
            {{"observed_index", observed_index},
             {"global_start_sample", pulse.global_start_sample},
             {"duration_samples", pulse.duration_samples},
             {"logical_frequency_index", pulse.logical_frequency_index},
             {"physical_channel_index", pulse.physical_channel_index},
             {"rf_frequency_hz", pulse.rf_frequency_hz},
             {"iq_offset_frequency_hz", pulse.iq_offset_frequency_hz},
             {"estimated_center_frequency_hz",
              pulse.estimated_center_frequency_hz},
             {"detector_frequency_error_hz_unqualified",
              pulse.detector_frequency_error_hz_unqualified},
             {"confidence_score_uncalibrated",
              pulse.confidence_score_uncalibrated},
             {"viterbi_path_metric", pulse.viterbi_path_metric},
             {"viterbi_second_best_path_metric",
              pulse.viterbi_second_best_path_metric},
             {"decoded_value", pulse.decoded_value},
             {"source_node_id", node_id}});
      }
      const auto record = [&](std::string field, std::string interval,
                              std::string unit, std::string transform) {
        if (provenance.size() >= kMaxProvenanceRecords)
          return;
        provenance.push_back(FHSSObservationProvenance{
            generation.generation, generation.run_epoch, node_id, node_class,
            snapshot->source_schema, std::move(field),
            (interval.starts_with("every ") ||
             interval.starts_with("per pulse: "))
                ? std::move(interval)
                : std::string{},
            std::move(unit), capture_time, std::move(transform)}
                                 .ToJson());
      };
      record("sources[]", "snapshot", "1",
             "typed immutable IFHSSReceiverObservationSource snapshot");
      if (snapshot->detected_pulse_count)
        record("detected_count", "snapshot", "pulse",
               snapshot->source_kind == "message_sink"
                   ? "terminal sink count; preferred over detector sum"
                   : "summed once across physical acquisition detectors");
      if (snapshot->rejected_count)
        record("rejected_count", "snapshot", "pulse",
               snapshot->source_kind == "message_sink"
                   ? "terminal sink count; preferred over detector sum"
                   : "summed once across physical acquisition detectors");
      if (!snapshot->rejection_reason_codes.empty())
        record(
            "rejection_reason_codes[]", "snapshot", "code",
            "stable receiver diagnostic reason code; no evaluator inference");
      if (snapshot->preamble_lock)
        record("preamble.locked", "snapshot", "boolean",
               "copied from receiver preamble-lock diagnostic");
      if (!snapshot->receiver_derived_active_frequencies.empty())
        record("receiver_derived_active_frequencies[]", "snapshot", "index",
               "copied from receiver-derived preamble result");
      if (snapshot->assembler_status)
        record("assembler.status", "snapshot", "status",
               "copied from receiver message assembler result");
      if (snapshot->receiver_message_status) {
        record("receiver_message_result.status", "snapshot", "status",
               "copied from terminal receiver message sidecar");
        record("receiver_message_result.accepted", "snapshot", "boolean",
               "true only for an accepted assembled receiver message");
        record("receiver_message_result.decoded_pulse_count", "snapshot",
               "pulse", "counted from terminal receiver message sidecar");
      }
      if (!snapshot->decoded_pulses.empty()) {
        const std::string pulse_interval =
            "per pulse: [global_start_sample, global_start_sample + "
            "duration_samples) input samples";
        const std::array<
            std::tuple<std::string, std::string, std::string, std::string>, 12>
            fields{
                {{"observed_pulses[].global_start_sample", pulse_interval,
                  "sample", "copied from receiver timing map"},
                 {"observed_pulses[].duration_samples", pulse_interval,
                  "sample", "copied from receiver pulse timing"},
                 {"observed_pulses[].logical_frequency_index", pulse_interval,
                  "index", "copied without schedule correlation"},
                 {"observed_pulses[].physical_channel_index", pulse_interval,
                  "index", "copied from receiver channel_id"},
                 {"observed_pulses[].rf_frequency_hz", pulse_interval, "Hz",
                  "copied from receiver frequency product"},
                 {"observed_pulses[].iq_offset_frequency_hz", pulse_interval,
                  "Hz", "copied from receiver IQ-frequency product"},
                 {"observed_pulses[].estimated_center_frequency_hz",
                  pulse_interval, "Hz", "copied from detector output"},
                 {"observed_pulses[].detector_frequency_error_hz_unqualified",
                  pulse_interval, "Hz",
                  "raw detector frequency error; not qualified CFO"},
                 {"observed_pulses[].confidence_score_uncalibrated",
                  pulse_interval, "score",
                  "receiver score; not calibrated probability"},
                 {"observed_pulses[].viterbi_path_metric", pulse_interval,
                  "metric", "raw decoder best-path metric"},
                 {"observed_pulses[].viterbi_second_best_path_metric",
                  pulse_interval, "metric",
                  "raw decoder second-best-path metric"},
                 {"observed_pulses[].decoded_value", pulse_interval, "uint32",
                  "copied from receiver pulse-word decoder"}}};
        for (const auto &[field, interval, unit, transform] : fields)
          record(field, interval, unit, transform);
      }
      if (!snapshot->sample_captures.empty())
        record(
            "spectrum.source.sample_captures",
            "every " +
                std::to_string(
                    snapshot->sample_captures.front().input_sample_interval) +
                " input samples",
            "complex sample unit",
            "bounded receiver-side channelizer capture; raw IQ not serialized");
      for (const auto &capture : snapshot->sample_captures) {
        if (!std::isfinite(capture.sample_rate_hz) ||
            capture.sample_rate_hz <= 0.0) {
          receiver_sample_rate_conflict = true;
          continue;
        }
        if (!receiver_sample_rate_hz)
          receiver_sample_rate_hz = capture.sample_rate_hz;
        else if (*receiver_sample_rate_hz != capture.sample_rate_hz)
          receiver_sample_rate_conflict = true;
        const auto input_rate =
            capture.sample_rate_hz *
            static_cast<double>(capture.input_sample_interval);
        if (capture.input_sample_interval == 0 || !std::isfinite(input_rate) ||
            input_rate <= 0.0) {
          receiver_sample_rate_conflict = true;
        } else if (!global_input_sample_rate_hz) {
          global_input_sample_rate_hz = input_rate;
          input_samples_per_capture_sample = capture.input_sample_interval;
        } else if (*global_input_sample_rate_hz != input_rate ||
                   *input_samples_per_capture_sample !=
                       capture.input_sample_interval) {
          receiver_sample_rate_conflict = true;
        }
      }
      if (!snapshot->sample_captures.empty())
        record(
            "sample_rate.receiver_capture_sample_rate_hz",
            "every " +
                std::to_string(
                    snapshot->sample_captures.front().input_sample_interval) +
                " input samples",
            "Hz", "copied from typed receiver channel capture metadata");
      if (!snapshot->sample_captures.empty())
        record(
            "sample_rate.global_input_sample_rate_hz", "every 1 input sample",
            "Hz",
            "receiver capture rate multiplied by exact input sample interval");
    }
  }

  const bool counts_available =
      sink_detected_count.has_value() || detector_counts_available;
  const nlohmann::json detected_count =
      counts_available ? nlohmann::json(sink_detected_count.value_or(
                             detector_detected_count))
                       : nlohmann::json(nullptr);
  const nlohmann::json rejected_count =
      counts_available ? nlohmann::json(sink_rejected_count.value_or(
                             detector_rejected_count))
                       : nlohmann::json(nullptr);
  nlohmann::json rejection_reasons = nlohmann::json::array();
  for (const auto &reason : rejection_reason_codes)
    rejection_reasons.push_back(reason);
  const bool terminal_available =
      generation.generation > 0 &&
      status.terminal_generation == generation.generation;
  if (terminal_available && provenance.size() < kMaxProvenanceRecords) {
    provenance.push_back(FHSSObservationProvenance{
        generation.generation, generation.run_epoch, "graph_runtime_session",
        "GraphRuntimeSession", "graphx.dashboard.runtime_status.v1",
        "terminal_result", "", "status", status.terminal_at,
        "correlated by generation and run epoch; no receiver truth"}
                             .ToJson());
  }
  const auto availability =
      generation.graph_manager
          ? (any_source ? Available() : Unavailable(missing_source_reason()))
          : Unavailable("generation_not_available");
  if (any_source && preamble.at("availability").at("state") == "unavailable")
    preamble = {
        {"availability", Unavailable("decoder_or_assembler_not_reached")}};
  if (any_source && assembler.at("availability").at("state") == "unavailable")
    assembler = {
        {"availability", Unavailable("decoder_or_assembler_not_reached")}};
  if (any_source &&
      receiver_message_result.at("availability").at("state") == "unavailable")
    receiver_message_result = {
        {"availability", Unavailable("decoder_or_assembler_not_reached")}};
  if (any_source &&
      active_frequencies.at("availability").at("state") == "unavailable")
    active_frequencies = {{"availability", Unavailable("no_preamble_lock")},
                          {"indices", nlohmann::json::array()}};
  nlohmann::json document{
      {"schema", "graphx.dashboard.fhss_receiver_observation.v1"},
      {"semantic_class", "observed"},
      {"generation", generation.generation},
      {"run_epoch", generation.run_epoch},
      {"config_revision", generation.config_revision},
      {"config_etag", generation.config_etag},
      {"observation_id", "observation-g" +
                             std::to_string(generation.generation) + "-r" +
                             std::to_string(generation.run_epoch)},
      {"availability", availability},
      {"timing_basis", {{"unit", "input_samples"}, {"global", true}}},
      {"sample_rate",
       receiver_sample_rate_hz && !receiver_sample_rate_conflict
           ? nlohmann::json{{"availability", Available()},
                            {"global_input_sample_rate_hz",
                             *global_input_sample_rate_hz},
                            {"receiver_capture_sample_rate_hz",
                             *receiver_sample_rate_hz},
                            {"input_samples_per_capture_sample",
                             *input_samples_per_capture_sample}}
           : nlohmann::json{{"availability",
                             Unavailable(receiver_sample_rate_conflict
                                             ? "invalid_capture"
                                             : "no_receiver_samples")}}},
      {"observed_pulses", std::move(pulses)},
      {"detected_count", detected_count},
      {"rejected_count", rejected_count},
      {"count_availability",
       sink_detected_count || detector_counts_available
           ? Available()
           : Unavailable(any_source ? "no_candidate_detected"
                                    : missing_source_reason())},
      {"count_semantics",
       {{"detected", sink_detected_count
                         ? "terminal_message_sink_count"
                         : (detector_counts_available
                                ? "sum_of_distinct_acquisition_detector_counts"
                                : "unavailable")},
        {"rejected", sink_rejected_count
                         ? "terminal_message_sink_count"
                         : (detector_counts_available
                                ? "sum_of_distinct_acquisition_detector_counts"
                                : "unavailable")},
        {"deduplication_rule",
         "terminal sink counts supersede upstream detector counts; source "
         "kinds are never added together"}}},
      {"rejection_reason_codes", std::move(rejection_reasons)},
      {"preamble", std::move(preamble)},
      {"receiver_derived_active_frequencies", std::move(active_frequencies)},
      {"assembler", std::move(assembler)},
      {"receiver_message_result", std::move(receiver_message_result)},
      {"terminal_result",
       terminal_available
           ? nlohmann::json{{"availability", Available()},
                            {"code", status.terminal_result_code},
                            {"message", status.terminal_result_message},
                            {"terminal_at", status.terminal_at}}
           : nlohmann::json{{"availability",
                             Unavailable("generation_not_available")}}},
      {"sources", std::move(sources)},
      {"provenance", std::move(provenance)},
      {"truncation",
       {{"truncated", original_pulse_count > kMaxObservedPulses},
        {"original_pulse_count", original_pulse_count},
        {"returned_pulse_count",
         std::min(original_pulse_count, kMaxObservedPulses)},
        {"max_pulses", kMaxObservedPulses},
        {"max_response_bytes", kMaxResponseBytes}}}};
  document["observation_sha256"] = HashJson(document);
  return {std::move(document)};
}

FHSSComparisonResult FHSSObservationService::Comparison() const {
  const auto expected = ExpectedTruth().document;
  const auto observed = ReceiverObservation().document;
  return CompareDocuments(expected, observed);
}

FHSSComparisonResult
FHSSObservationService::CompareDocuments(const nlohmann::json &expected,
                                         const nlohmann::json &observed) {
  const auto algorithm = nlohmann::json{
      {"name", "bounded_one_to_one_timing_channel_match"},
      {"version", "1.1.0"},
      {"timing_tolerance_samples", kTimingToleranceSamples},
      {"channel_rule", "logical_frequency_index_exact"},
      {"tie_rule", "equal_distance_is_ambiguous_no_assignment"},
      {"duplicate_rule", "each_expected_and_observed_used_at_most_once"}};
  const auto value_or_null = [](const nlohmann::json &document,
                                std::string_view field,
                                nlohmann::json::value_t type) {
    if (!document.is_object() || !document.contains(field) ||
        document.at(field).type() != type)
      return nlohmann::json(nullptr);
    return document.at(field);
  };
  const auto unsigned_or_null = [](const nlohmann::json &document,
                                   std::string_view field) {
    if (!document.is_object() || !document.contains(field) ||
        (!document.at(field).is_number_unsigned() &&
         !(document.at(field).is_number_integer() &&
           document.at(field).get<std::int64_t>() >= 0)))
      return nlohmann::json(nullptr);
    return nlohmann::json(document.at(field).get<std::uint64_t>());
  };
  const auto is_bounded_unsigned = [](const nlohmann::json &value,
                                      std::uint64_t maximum) {
    if (value.is_number_unsigned())
      return value.get<std::uint64_t>() <= maximum;
    if (!value.is_number_integer())
      return false;
    const auto signed_value = value.get<std::int64_t>();
    return signed_value >= 0 &&
           static_cast<std::uint64_t>(signed_value) <= maximum;
  };
  const auto has_string = [](const nlohmann::json &document,
                             std::string_view field) {
    return document.is_object() && document.contains(field) &&
           document.at(field).is_string();
  };
  const auto identity = [&] {
    const auto expected_revision =
        unsigned_or_null(expected, "config_revision");
    const auto expected_etag =
        value_or_null(expected, "config_etag", nlohmann::json::value_t::string);
    const auto observed_revision =
        unsigned_or_null(observed, "config_revision");
    const auto observed_etag =
        value_or_null(observed, "config_etag", nlohmann::json::value_t::string);
    const bool complete =
        !expected_revision.is_null() && !expected_etag.is_null() &&
        !observed_revision.is_null() && !observed_etag.is_null();
    return nlohmann::json{
        {"expected_config_revision", expected_revision},
        {"expected_config_etag", expected_etag},
        {"observed_config_revision", observed_revision},
        {"observed_config_etag", observed_etag},
        {"agrees",
         complete ? nlohmann::json(expected_revision == observed_revision &&
                                   expected_etag == observed_etag)
                  : nlohmann::json(nullptr)}};
  }();
  const auto indeterminate = [&](std::string reason) {
    nlohmann::json document{
        {"schema", "graphx.dashboard.fhss_comparison_result.v1"},
        {"semantic_class", "comparison"},
        {"evaluation_state", "indeterminate"},
        {"expected_truth_sha256",
         value_or_null(expected, "truth_sha256",
                       nlohmann::json::value_t::string)},
        {"receiver_observation_sha256",
         value_or_null(observed, "observation_sha256",
                       nlohmann::json::value_t::string)},
        {"generation", unsigned_or_null(observed, "generation")},
        {"run_epoch", unsigned_or_null(observed, "run_epoch")},
        {"config_identity", identity},
        {"algorithm", algorithm},
        {"availability", {{"state", "unavailable"}, {"reason", reason}}},
        {"matches", nlohmann::json::array()},
        {"missed_expected_indices", nlohmann::json::array()},
        {"unexpected_observed_indices", nlohmann::json::array()},
        {"ambiguous", nlohmann::json::array()},
        {"terminal_result_agrees", nullptr},
        {"execution_lifecycle",
         {{"completed", nullptr},
          {"observed_code",
           observed.is_object() && observed.contains("terminal_result") &&
                   observed.at("terminal_result").is_object() &&
                   observed.at("terminal_result").contains("code") &&
                   observed.at("terminal_result").at("code").is_string()
               ? observed.at("terminal_result").at("code")
               : nlohmann::json(nullptr)},
          {"correlated_separately_from_receiver_message", true}}}};
    document["comparison_sha256"] = HashJson(document);
    return FHSSComparisonResult{std::move(document)};
  };
  if (!expected.is_object() || !has_string(expected, "semantic_class") ||
      expected.at("semantic_class") != "expected" ||
      !has_string(expected, "truth_sha256") || !expected.contains("pulses") ||
      !expected.at("pulses").is_array() ||
      expected.at("pulses").size() > kMaxObservedPulses ||
      !expected.contains("expected_receiver_message") ||
      !expected.at("expected_receiver_message").is_object() ||
      !expected.at("expected_receiver_message").contains("accepted") ||
      !expected.at("expected_receiver_message").at("accepted").is_boolean() ||
      !expected.at("expected_receiver_message")
           .contains("decoded_pulse_count") ||
      !is_bounded_unsigned(
          expected.at("expected_receiver_message").at("decoded_pulse_count"),
          kMaxObservedPulses) ||
      identity.at("expected_config_revision").is_null() ||
      identity.at("expected_config_etag").is_null())
    return indeterminate("expected_document_malformed");
  if (!observed.is_object() || !has_string(observed, "semantic_class") ||
      observed.at("semantic_class") != "observed" ||
      !has_string(observed, "observation_sha256") ||
      unsigned_or_null(observed, "generation").is_null() ||
      unsigned_or_null(observed, "run_epoch").is_null() ||
      !observed.contains("observed_pulses") ||
      !observed.at("observed_pulses").is_array() ||
      observed.at("observed_pulses").size() > kMaxObservedPulses ||
      identity.at("observed_config_revision").is_null() ||
      identity.at("observed_config_etag").is_null())
    return indeterminate("observed_document_malformed");
  if (identity.at("agrees") != true)
    return indeterminate("configuration_identity_mismatch");
  if (!observed.contains("availability") ||
      !observed.at("availability").is_object() ||
      !has_string(observed.at("availability"), "state") ||
      !observed.contains("terminal_result") ||
      !observed.at("terminal_result").is_object() ||
      !has_string(observed.at("terminal_result"), "code"))
    return indeterminate("observed_document_malformed");
  if (observed.at("availability").at("state") != "available")
    return indeterminate("receiver_observation_unavailable");
  if (observed.at("terminal_result").at("code") != "execution_completed")
    return indeterminate("receiver_execution_not_completed");
  if (!observed.contains("receiver_message_result") ||
      !observed.at("receiver_message_result").is_object() ||
      !observed.at("receiver_message_result").contains("availability") ||
      !observed.at("receiver_message_result").at("availability").is_object() ||
      !has_string(observed.at("receiver_message_result").at("availability"),
                  "state") ||
      !observed.at("receiver_message_result").contains("accepted") ||
      !observed.at("receiver_message_result").at("accepted").is_boolean() ||
      !observed.at("receiver_message_result").contains("decoded_pulse_count") ||
      !is_bounded_unsigned(
          observed.at("receiver_message_result").at("decoded_pulse_count"),
          kMaxObservedPulses))
    return indeterminate("observed_document_malformed");
  if (observed.at("receiver_message_result").at("availability").at("state") !=
      "available")
    return indeterminate("receiver_message_result_unavailable");
  const auto &expected_pulses = expected.at("pulses");
  const auto &observed_pulses = observed.at("observed_pulses");
  const auto expected_pulse_valid = [](const nlohmann::json &pulse) {
    const auto valid = [](const nlohmann::json &value, std::uint64_t maximum) {
      if (value.is_number_unsigned())
        return value.get<std::uint64_t>() <= maximum;
      if (!value.is_number_integer())
        return false;
      const auto signed_value = value.get<std::int64_t>();
      return signed_value >= 0 &&
             static_cast<std::uint64_t>(signed_value) <= maximum;
    };
    return pulse.is_object() && pulse.contains("logical_frequency_index") &&
           valid(pulse.at("logical_frequency_index"), 63) &&
           pulse.contains("global_start_sample") &&
           valid(pulse.at("global_start_sample"),
                 std::numeric_limits<std::uint64_t>::max()) &&
           pulse.contains("transmitted_word") &&
           valid(pulse.at("transmitted_word"),
                 std::numeric_limits<std::uint32_t>::max());
  };
  const auto observed_pulse_valid = [](const nlohmann::json &pulse) {
    const auto valid = [](const nlohmann::json &value, std::uint64_t maximum) {
      if (value.is_number_unsigned())
        return value.get<std::uint64_t>() <= maximum;
      if (!value.is_number_integer())
        return false;
      const auto signed_value = value.get<std::int64_t>();
      return signed_value >= 0 &&
             static_cast<std::uint64_t>(signed_value) <= maximum;
    };
    return pulse.is_object() && pulse.contains("logical_frequency_index") &&
           valid(pulse.at("logical_frequency_index"), 63) &&
           pulse.contains("global_start_sample") &&
           valid(pulse.at("global_start_sample"),
                 std::numeric_limits<std::uint64_t>::max()) &&
           pulse.contains("decoded_value") &&
           valid(pulse.at("decoded_value"),
                 std::numeric_limits<std::uint32_t>::max());
  };
  if (!std::ranges::all_of(expected_pulses, expected_pulse_valid))
    return indeterminate("expected_document_malformed");
  if (!std::ranges::all_of(observed_pulses, observed_pulse_valid))
    return indeterminate("observed_document_malformed");
  std::set<std::size_t> assigned;
  nlohmann::json matches = nlohmann::json::array();
  nlohmann::json unexpected = nlohmann::json::array();
  nlohmann::json ambiguous = nlohmann::json::array();
  for (std::size_t oi = 0; oi < observed_pulses.size(); ++oi) {
    const auto &observation = observed_pulses.at(oi);
    std::vector<std::pair<std::uint64_t, std::size_t>> candidates;
    for (std::size_t ei = 0; ei < expected_pulses.size(); ++ei) {
      if (assigned.contains(ei) ||
          expected_pulses.at(ei).at("logical_frequency_index") !=
              observation.at("logical_frequency_index"))
        continue;
      const auto expected_start =
          expected_pulses.at(ei).at("global_start_sample").get<std::uint64_t>();
      const auto observed_start =
          observation.at("global_start_sample").get<std::uint64_t>();
      const auto delta = expected_start > observed_start
                             ? expected_start - observed_start
                             : observed_start - expected_start;
      if (delta <= kTimingToleranceSamples)
        candidates.emplace_back(delta, ei);
    }
    std::sort(candidates.begin(), candidates.end());
    if (candidates.empty()) {
      unexpected.push_back(oi);
      continue;
    }
    if (candidates.size() > 1 && candidates[0].first == candidates[1].first) {
      nlohmann::json equal_best = nlohmann::json::array();
      for (const auto &[distance, candidate_index] : candidates) {
        if (distance != candidates.front().first)
          break;
        equal_best.push_back(candidate_index);
      }
      ambiguous.push_back(
          {{"observed_index", oi},
           {"candidate_expected_indices", std::move(equal_best)},
           {"reason", "equal_distance_candidates"}});
      continue;
    }
    const auto ei = candidates.front().second;
    assigned.insert(ei);
    const auto expected_start =
        expected_pulses.at(ei).at("global_start_sample").get<std::uint64_t>();
    const auto observed_start =
        observation.at("global_start_sample").get<std::uint64_t>();
    const auto delta = expected_start > observed_start
                           ? expected_start - observed_start
                           : observed_start - expected_start;
    // Candidate selection has already bounded delta to 64 samples, so this
    // signed conversion cannot overflow even when either absolute sample
    // index is in the upper half of the uint64 range.
    const auto signed_delta = observed_start >= expected_start
                                  ? static_cast<std::int64_t>(delta)
                                  : -static_cast<std::int64_t>(delta);
    matches.push_back({{"expected_index", ei},
                       {"observed_index", oi},
                       {"timing_delta_samples", signed_delta},
                       {"channel_delta", 0},
                       {"decoded_value_agrees",
                        expected_pulses.at(ei).at("transmitted_word") ==
                            observation.at("decoded_value")}});
  }
  nlohmann::json missed = nlohmann::json::array();
  for (std::size_t ei = 0; ei < expected_pulses.size(); ++ei)
    if (!assigned.contains(ei))
      missed.push_back(ei);
  nlohmann::json document{
      {"schema", "graphx.dashboard.fhss_comparison_result.v1"},
      {"semantic_class", "comparison"},
      {"evaluation_state", "evaluated"},
      {"expected_truth_sha256", expected.at("truth_sha256")},
      {"receiver_observation_sha256", observed.at("observation_sha256")},
      {"generation", observed.at("generation")},
      {"run_epoch", observed.at("run_epoch")},
      {"config_identity", identity},
      {"algorithm", algorithm},
      {"availability", {{"state", "available"}, {"reason", nullptr}}},
      {"matches", std::move(matches)},
      {"missed_expected_indices", std::move(missed)},
      {"unexpected_observed_indices", std::move(unexpected)},
      {"ambiguous", std::move(ambiguous)},
      {"terminal_result_agrees",
       expected.at("expected_receiver_message").at("accepted") ==
               observed.at("receiver_message_result").at("accepted") &&
           expected.at("expected_receiver_message").at("decoded_pulse_count") ==
               observed.at("receiver_message_result")
                   .at("decoded_pulse_count")},
      {"execution_lifecycle",
       {{"completed", true},
        {"observed_code", "execution_completed"},
        {"correlated_separately_from_receiver_message", true}}}};
  document["comparison_sha256"] = HashJson(document);
  return {std::move(document)};
}

nlohmann::json FHSSObservationService::SpectrumFromCapture(
    const dsp::fhss::FHSSReceiverSampleCapture &capture, std::size_t fft_size) {
  if (!IsPowerOfTwo(fft_size) || fft_size < 16 || fft_size > kMaxSpectrumBins ||
      capture.samples.size() < fft_size ||
      !std::isfinite(capture.sample_rate_hz) || capture.sample_rate_hz <= 0.0 ||
      capture.input_sample_interval == 0 ||
      !std::ranges::all_of(
          capture.samples.begin(), capture.samples.begin() + fft_size,
          [](const auto &sample) {
            return std::isfinite(sample.real()) && std::isfinite(sample.imag());
          })) {
    return nlohmann::json{{"availability", Unavailable("invalid_capture")},
                          {"bins", nlohmann::json::array()}};
  }
  constexpr double kPi = 3.1415926535897932384626433832795;
  std::vector<double> window(fft_size);
  double window_sum = 0.0;
  double window_square_sum = 0.0;
  for (std::size_t i = 0; i < fft_size; ++i) {
    window[i] = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                       static_cast<double>(fft_size - 1));
    window_sum += window[i];
    window_square_sum += window[i] * window[i];
  }
  nlohmann::json bins = nlohmann::json::array();
  for (std::size_t shifted = 0; shifted < fft_size; ++shifted) {
    const auto signed_bin = static_cast<std::int64_t>(shifted) -
                            static_cast<std::int64_t>(fft_size / 2);
    const auto raw_bin = static_cast<std::size_t>(
        (signed_bin + static_cast<std::int64_t>(fft_size)) %
        static_cast<std::int64_t>(fft_size));
    std::complex<double> sum{};
    for (std::size_t sample = 0; sample < fft_size; ++sample) {
      const auto phase = -2.0 * kPi * static_cast<double>(raw_bin * sample) /
                         static_cast<double>(fft_size);
      sum += capture.samples[sample] * window[sample] *
             std::complex<double>(std::cos(phase), std::sin(phase));
    }
    const auto magnitude = std::abs(sum) / window_sum;
    const auto db =
        20.0 *
        std::log10(std::max(magnitude, std::numeric_limits<double>::min()));
    bins.push_back({{"bin", signed_bin},
                    {"baseband_frequency_hz",
                     static_cast<double>(signed_bin) * capture.sample_rate_hz /
                         static_cast<double>(fft_size)},
                    {"magnitude_linear_re_1_complex_unit", magnitude},
                    {"magnitude_db_re_1_complex_unit", db}});
  }
  return {{"availability", Available()},
          {"fft_size", fft_size},
          {"window",
           {{"name", "symmetric_hamming"},
            {"version", "graphx-1"},
            {"coherent_gain", window_sum / static_cast<double>(fft_size)},
            {"noise_gain",
             std::sqrt(window_square_sum / static_cast<double>(fft_size))}}},
          {"ordering", "two_sided_fftshift_negative_to_positive"},
          {"scaling",
           {{"quantity", "complex_magnitude"},
            {"linear_reference", "1 complex sample unit"},
            {"decibel_definition", "20*log10(magnitude/1_complex_unit)"},
            {"units", "dB re 1 complex-unit"},
            {"calibrated_power", false}}},
          {"averaging", "single_frame_none"},
          {"bins", std::move(bins)}};
}

nlohmann::json FHSSObservationService::Spectrum(
    std::optional<std::uint32_t> physical_channel_index,
    std::size_t fft_size) const {
  const auto generation = runtime_session_->SnapshotGeneration();
  const auto unavailable = [&](std::string reason) {
    return nlohmann::json{
        {"schema", "graphx.dashboard.fhss_receiver_spectrum.v1"},
        {"semantic_class", "unavailable"},
        {"generation", generation.generation},
        {"run_epoch", generation.run_epoch},
        {"config_revision", generation.config_revision},
        {"config_etag", generation.config_etag},
        {"channel_index", physical_channel_index
                              ? nlohmann::json(*physical_channel_index)
                              : nlohmann::json(nullptr)},
        {"availability", Unavailable(std::move(reason))},
        {"bins", nlohmann::json::array()}};
  };
  if (!IsPowerOfTwo(fft_size) || fft_size < 16 || fft_size > kMaxSpectrumBins ||
      (physical_channel_index && *physical_channel_index >= 64))
    return unavailable("invalid_spectrum_request");
  if (!generation.graph_manager)
    return unavailable("generation_not_available");
  const auto observation_availability =
      ReceiverObservation().document.at("availability");
  if (observation_availability.contains("reason") &&
      observation_availability.at("reason").is_string() &&
      observation_availability.at("reason") == "observation_export_disabled")
    return unavailable("observation_export_disabled");
  std::optional<dsp::fhss::FHSSReceiverSampleCapture> selected;
  std::string node_id;
  std::string node_class;
  std::string source_schema;
  const auto &nodes = generation.graph_manager->GetNodes();
  if (!physical_channel_index) {
    for (std::size_t index = 0; index < nodes.size() && !physical_channel_index;
         ++index) {
      const auto source = TryGetObservationSource(nodes[index]);
      if (!source)
        continue;
      const auto snapshot = source->SnapshotReceiverObservation();
      if (snapshot && !snapshot->decoded_pulses.empty())
        physical_channel_index =
            snapshot->decoded_pulses.front().physical_channel_index;
    }
    if (!physical_channel_index)
      return unavailable("no_candidate_detected");
  }
  for (std::size_t index = 0; index < nodes.size() && !selected; ++index) {
    const auto source = TryGetObservationSource(nodes[index]);
    if (!source)
      continue;
    const auto snapshot = source->SnapshotReceiverObservation();
    if (!snapshot)
      continue;
    for (const auto &capture : snapshot->sample_captures) {
      if (capture.physical_channel_index == *physical_channel_index) {
        selected = capture;
        node_id = NodeName(nodes[index], index);
        node_class = NodeClass(nodes[index]);
        source_schema = snapshot->source_schema;
        break;
      }
    }
  }
  if (!selected || selected->samples.empty())
    return unavailable("no_receiver_samples");
  if (selected->samples.size() < fft_size)
    return unavailable("short_receiver_capture");
  auto result = SpectrumFromCapture(*selected, fft_size);
  if (result.at("availability").at("state") == "unavailable")
    return unavailable(
        result.at("availability").at("reason").get<std::string>());
  result.update({{"schema", "graphx.dashboard.fhss_receiver_spectrum.v1"},
                 {"semantic_class", "observed"},
                 {"generation", generation.generation},
                 {"run_epoch", generation.run_epoch},
                 {"config_revision", generation.config_revision},
                 {"config_etag", generation.config_etag},
                 {"availability", Available()},
                 {"source",
                  {{"node_id", node_id},
                   {"node_class", node_class},
                   {"source_schema", source_schema},
                   {"field", "sample_captures.samples"}}},
                 {"channel_index", *physical_channel_index},
                 {"logical_frequency_index", selected->logical_frequency_index},
                 {"rf_frequency_hz", selected->rf_frequency_hz},
                 {"iq_offset_frequency_hz", selected->iq_offset_frequency_hz},
                 {"sample_rate_hz", selected->sample_rate_hz},
                 {"global_start_sample", selected->global_start_sample},
                 {"input_sample_interval", selected->input_sample_interval},
                 {"captured_sample_count", selected->samples.size()},
                 {"original_sample_count", selected->original_sample_count},
                 {"capture_truncated", selected->truncated},
                 {"bounds",
                  {{"max_fft_size", kMaxSpectrumBins},
                   {"max_channels_per_response", 1},
                   {"max_response_bytes", kMaxResponseBytes}}}});
  if (result.dump().size() > kMaxResponseBytes)
    return unavailable("response_size_limit");
  return result;
}

nlohmann::json FHSSObservationService::Provenance() const {
  const auto observation = ReceiverObservation().document;
  return {{"schema", "graphx.dashboard.fhss_observation_provenance.v1"},
          {"semantic_class", "observed"},
          {"generation", observation.at("generation")},
          {"run_epoch", observation.at("run_epoch")},
          {"config_revision", observation.at("config_revision")},
          {"config_etag", observation.at("config_etag")},
          {"observation_id", observation.at("observation_id")},
          {"records", observation.at("provenance")}};
}

nlohmann::json FHSSObservationService::History() const {
  const auto observation = ReceiverObservation().document;
  return {{"schema", "graphx.dashboard.fhss_observation_history.v1"},
          {"semantic_class", "observed"},
          {"retention",
           {{"max_entries", kMaxHistoryEntries},
            {"max_age_seconds", 3600},
            {"max_bytes", kMaxResponseBytes},
            {"policy", "active_generation_current_run_only"}}},
          {"entries",
           {{{"generation", observation.at("generation")},
             {"run_epoch", observation.at("run_epoch")},
             {"config_revision", observation.at("config_revision")},
             {"config_etag", observation.at("config_etag")},
             {"observation_id", observation.at("observation_id")},
             {"observation_sha256", observation.at("observation_sha256")},
             {"availability", observation.at("availability")}}}}};
}

} // namespace dsp::fhss::dashboard
