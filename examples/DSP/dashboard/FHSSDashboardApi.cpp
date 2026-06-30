// SPDX-License-Identifier: MIT

#include "FHSSDashboardApi.hpp"

#include "graph/dashboard/GraphConfigurationService.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace dsp::fhss::dashboard {
namespace {

using ApiRequest = graph::dashboard::EmbeddedDashboardServer::ApiRequest;
using ApiResponse = graph::dashboard::EmbeddedDashboardServer::ApiResponse;

ApiResponse JsonResponse(int status, const nlohmann::json &body) {
  return {.status_code = status,
          .content_type = "application/json",
          .body = body.dump()};
}

ApiResponse ErrorResponse(int status, std::string code, std::string message) {
  return JsonResponse(status,
                      {{"schema", "graphx.dashboard.error.v1"},
                       {"status", status},
                       {"code", std::move(code)},
                       {"message", std::move(message)},
                       {"details", nullptr},
                       {"request_id", "fhss-dashboard"},
                       {"retriable", false}});
}

std::string QueryValue(const std::string &query, std::string_view name) {
  std::size_t offset = 0;
  while (offset <= query.size()) {
    const auto end = query.find('&', offset);
    const auto item = query.substr(offset, end == std::string::npos
                                               ? std::string::npos
                                               : end - offset);
    const auto equals = item.find('=');
    if (item.substr(0, equals) == name) {
      return equals == std::string::npos ? std::string{} : item.substr(equals + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    offset = end + 1;
  }
  return {};
}

std::optional<std::uint64_t> ParseUnsigned(const std::string &value) {
  if (value.empty()) {
    return std::nullopt;
  }
  std::uint64_t parsed = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed;
}

std::uint64_t BoundedQuery(const std::string &query,
                           std::string_view name,
                           std::uint64_t fallback,
                           std::uint64_t minimum,
                           std::uint64_t maximum) {
  const auto value = ParseUnsigned(QueryValue(query, name)).value_or(fallback);
  return std::clamp(value, minimum, maximum);
}

nlohmann::json BuildVisualization(const nlohmann::json &scenario,
                                  const std::string &query) {
  const auto messages = scenario.contains("messages") && scenario.at("messages").is_array()
                            ? scenario.at("messages")
                            : nlohmann::json::array();
  const auto message_offset = static_cast<std::size_t>(BoundedQuery(
      query, "message_offset", 0, 0, messages.size()));
  const auto message_limit = static_cast<std::size_t>(BoundedQuery(
      query, "message_limit", 16, 1, 64));
  const auto pulse_offset = static_cast<std::size_t>(BoundedQuery(
      query, "pulse_offset", 0, 0, std::numeric_limits<std::uint64_t>::max()));
  const auto pulse_limit = static_cast<std::size_t>(BoundedQuery(
      query, "pulse_limit", 128, 1, 512));
  const auto refresh_ms = BoundedQuery(query, "refresh_ms", 250, 100, 2000);
  const auto selected_channel = static_cast<std::size_t>(BoundedQuery(
      query, "selected_channel", 0, 0, 63));

  nlohmann::json schedule_messages = nlohmann::json::array();
  nlohmann::json timeline = nlohmann::json::array();
  nlohmann::json decoder_messages = nlohmann::json::array();
  std::array<std::uint64_t, 64> channel_counts{};
  std::size_t absolute_pulse = 0;

  for (std::size_t message_index = 0; message_index < messages.size(); ++message_index) {
    const auto &message = messages.at(message_index);
    const auto pulses = message.contains("pulses") && message.at("pulses").is_array()
                            ? message.at("pulses")
                            : nlohmann::json::array();
    std::uint64_t preamble_count = 0;
    nlohmann::json best_path = nlohmann::json::array();
    double path_weight = 0.0;

    for (std::size_t pulse_index = 0; pulse_index < pulses.size();
         ++pulse_index, ++absolute_pulse) {
      const auto &pulse = pulses.at(pulse_index);
      const auto channel = pulse.value("frequency_index", std::uint64_t{0});
      const auto role = pulse.value("role", std::string{"body"});
      if (channel < channel_counts.size()) {
        ++channel_counts[static_cast<std::size_t>(channel)];
      }
      if (role == "preamble") {
        ++preamble_count;
      }
      path_weight += static_cast<double>((channel % 17u) + 1u) / 10.0;
      if (best_path.size() < 16u) {
        best_path.push_back(channel);
      }
      if (absolute_pulse >= pulse_offset && timeline.size() < pulse_limit) {
        timeline.push_back({{"message_index", message_index},
                            {"pulse_index", pulse_index},
                            {"frequency_index", channel},
                            {"expected_sample_start",
                             message.value("transmit_start_sample", std::uint64_t{0}) +
                                 pulse_index},
                            {"detected_sample_start", nullptr},
                            {"confidence", channel < 64u ? 0.9 : 0.0},
                            {"rejected", channel >= 64u}});
      }
    }

    if (message_index >= message_offset &&
        message_index < message_offset + message_limit) {
      schedule_messages.push_back(
          {{"message_index", message_index},
           {"message_id", message.value("message_id", message_index + 1u)},
           {"pulse_count", pulses.size()},
           {"preamble_pulse_count", preamble_count}});
      decoder_messages.push_back(
          {{"message_index", message_index},
           {"message_id", message.value("message_id", message_index + 1u)},
           {"preamble_symbol_count", preamble_count},
           {"viterbi",
            {{"best_path", std::move(best_path)},
             {"path_margin_db", pulses.empty()
                                    ? 0.0
                                    : path_weight / static_cast<double>(pulses.size())},
             {"decoded_word_count", pulses.size()}}}});
    }
  }

  nlohmann::json channels = nlohmann::json::array();
  for (std::size_t channel = 0; channel < channel_counts.size(); ++channel) {
    channels.push_back({{"channel_index", channel},
                        {"expected_pulse_count", channel_counts[channel]},
                        {"detected_pulse_count", 0u},
                        {"rejected_pulse_count", 0u}});
  }
  nlohmann::json spectrum = nlohmann::json::array();
  for (std::size_t bin = 0; bin < 32u; ++bin) {
    spectrum.push_back({{"bin", bin},
                        {"magnitude", std::abs(std::sin(bin * 0.35))}});
  }

  nlohmann::json result{
      {"schema", "graphx.dashboard.fhss_visualization.v1"},
      {"fixture_label",
       "Deterministic GraphX CPU FHSS fixture. Not a production RF receiver."},
      {"schedule",
       {{"message_count_total", messages.size()},
        {"message_offset", message_offset},
        {"message_limit", message_limit},
        {"messages", std::move(schedule_messages)}}},
      {"heatmap", {{"channel_count", 64u}, {"channels", std::move(channels)}}},
      {"timeline",
       {{"pulse_offset", pulse_offset},
        {"pulse_limit", pulse_limit},
        {"pulses", std::move(timeline)}}},
      {"decoder",
       {{"schema", "graphx.dashboard.fhss_decoder.v1"},
        {"messages", std::move(decoder_messages)}}},
      {"selected_channel_preview",
       {{"schema", "graphx.dashboard.fhss_channel_preview.v1"},
        {"channel_index", selected_channel},
        {"raw_iq_included", false},
        {"spectrum_bins", std::move(spectrum)}}},
      {"bounds",
       {{"refresh_interval_ms", refresh_ms},
        {"max_message_limit", 64u},
        {"max_pulse_limit", 512u}}}};
  result["bounds"]["snapshot_bytes_estimate"] = result.dump().size();
  return result;
}

bool IsUnderRoot(const std::filesystem::path &root,
                 const std::filesystem::path &candidate) {
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(root, error);
  if (error) {
    return false;
  }
  const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return false;
  }
  const auto relative = canonical_candidate.lexically_relative(canonical_root).native();
  return !relative.empty() && relative != ".." && relative.rfind("..", 0) != 0;
}

ApiResponse ExportBundle(const ApiRequest &request,
                         const std::filesystem::path &artifact_root,
                         const nlohmann::json &scenario) {
  const auto body = nlohmann::json::parse(request.body, nullptr, false);
  if (body.is_discarded()) {
    return ErrorResponse(400, "invalid_json", "request body must be JSON");
  }
  const auto output = std::filesystem::path(body.value("output_path", std::string{}));
  if (output.empty() || !output.is_absolute() ||
      !IsUnderRoot(artifact_root, output.parent_path())) {
    return ErrorResponse(400, "artifact_path_not_allowed",
                         "output_path must stay under the artifact root");
  }
  if (body.value("failure_injection", std::string{}) == "enospc") {
    return ErrorResponse(500, "artifact_write_failed", "injected ENOSPC");
  }

  std::error_code error;
  std::filesystem::create_directories(output.parent_path(), error);
  if (error) {
    return ErrorResponse(500, "artifact_write_failed", error.message());
  }
  const bool include_sigmf = body.value("include_sigmf_capture", false);
  const nlohmann::json bundle{
      {"schema", "graphx.dashboard.fhss_artifact_bundle.v1"},
      {"truth_in_labeling", "Deterministic GraphX CPU FHSS fixture."},
      {"sigmf_capture", {{"enabled", include_sigmf}, {"contains_raw_iq", false}}},
      {"scenario_summary", {{"message_count", scenario.value("messages", nlohmann::json::array()).size()}}}};
  std::ofstream bundle_file(output, std::ios::binary | std::ios::trunc);
  bundle_file << std::setw(2) << bundle << '\n';
  if (!bundle_file.good()) {
    return ErrorResponse(500, "artifact_write_failed", "failed to write bundle");
  }

  nlohmann::json files = nlohmann::json::array({output.string()});
  if (include_sigmf) {
    const auto sigmf = output.parent_path() / (output.stem().string() + ".sigmf-meta");
    std::ofstream sigmf_file(sigmf, std::ios::binary | std::ios::trunc);
    sigmf_file << nlohmann::json{{"global", {{"core:datatype", "cf32_le"}}},
                                 {"captures", nlohmann::json::array()},
                                 {"annotations", nlohmann::json::array()}}
               << '\n';
    if (!sigmf_file.good()) {
      return ErrorResponse(500, "artifact_write_failed", "failed to write SigMF metadata");
    }
    files.push_back(sigmf.string());
  }
  return JsonResponse(202,
                      {{"schema", "graphx.dashboard.fhss_artifact_bundle_result.v1"},
                       {"status", "succeeded"},
                       {"files", std::move(files)}});
}

} // namespace

graph::dashboard::EmbeddedDashboardServer::ApiHandler MakeApiHandler(
    std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service,
    std::filesystem::path artifact_root) {
  return [service = std::move(configuration_service),
          root = std::move(artifact_root)](const ApiRequest &request)
             -> std::optional<ApiResponse> {
    if (!service) {
      return std::nullopt;
    }
    const auto scenario = service->GetScenarioResponse().value(
        "scenario", nlohmann::json::object());
    if (request.method == "GET" && request.path == "/api/v1/fhss/visualization") {
      auto visualization = BuildVisualization(scenario, request.query);
      visualization["config_revision"] = service->ConfigRevision();
      return JsonResponse(200, visualization);
    }
    if (request.method == "POST" &&
        request.path == "/api/v1/fhss/artifacts/bundle") {
      return ExportBundle(request, root, scenario);
    }
    return std::nullopt;
  };
}

} // namespace dsp::fhss::dashboard
