// SPDX-License-Identifier: MIT

#include "FHSSDashboardApi.hpp"

#include "graph/dashboard/GraphConfigurationService.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
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
  const auto body = nlohmann::json{{"type", "urn:graphx:dashboard:problem:" + code},
                       {"title", code},
                       {"detail", message},
                       {"schema", "graphx.dashboard.error.v1"},
                       {"status", status},
                       {"code", std::move(code)},
                       {"message", message},
                       {"details", nullptr},
                       {"request_id", "fhss-dashboard"},
                       {"retriable", false}};
  return {.status_code = status,
          .content_type = "application/problem+json",
          .body = body.dump()};
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

bool Cancelled(const graph::dashboard::EmbeddedDashboardServer::ApiContext &context) {
  return context.stop_token.stop_requested() ||
         std::chrono::steady_clock::now() >= context.deadline;
}

std::optional<nlohmann::json> BuildVisualization(
    const nlohmann::json &scenario, const std::string &query,
    const graph::dashboard::EmbeddedDashboardServer::ApiContext &context) {
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

  nlohmann::json schedule_messages = nlohmann::json::array();
  nlohmann::json timeline = nlohmann::json::array();
  std::array<std::uint64_t, 64> channel_counts{};
  std::size_t absolute_pulse = 0;

  for (std::size_t message_index = 0; message_index < messages.size(); ++message_index) {
    if (Cancelled(context)) return std::nullopt;
    const auto &message = messages.at(message_index);
    const auto pulses = message.contains("pulses") && message.at("pulses").is_array()
                            ? message.at("pulses")
                            : nlohmann::json::array();
    std::uint64_t preamble_count = 0;

    for (std::size_t pulse_index = 0; pulse_index < pulses.size();
         ++pulse_index, ++absolute_pulse) {
      if (Cancelled(context)) return std::nullopt;
      const auto &pulse = pulses.at(pulse_index);
      const auto channel = pulse.value("frequency_index", std::uint64_t{0});
      const auto role = pulse.value("role", std::string{"body"});
      if (channel < channel_counts.size()) {
        ++channel_counts[static_cast<std::size_t>(channel)];
      }
      if (role == "preamble") {
        ++preamble_count;
      }
      if (absolute_pulse >= pulse_offset && timeline.size() < pulse_limit) {
        timeline.push_back({{"message_index", message_index},
                            {"pulse_index", pulse_index},
                            {"frequency_index", channel},
                            {"expected_sample_start",
                             message.value("transmit_start_sample", std::uint64_t{0}) +
                                 pulse_index},
                            {"source", "configured_schedule"}});
      }
    }

    if (message_index >= message_offset &&
        message_index < message_offset + message_limit) {
      schedule_messages.push_back(
          {{"message_index", message_index},
           {"message_id", message.value("message_id", message_index + 1u)},
           {"pulse_count", pulses.size()},
           {"preamble_pulse_count", preamble_count}});
    }
  }

  nlohmann::json channels = nlohmann::json::array();
  for (std::size_t channel = 0; channel < channel_counts.size(); ++channel) {
    if (Cancelled(context)) return std::nullopt;
    channels.push_back({{"channel_index", channel},
                        {"expected_pulse_count", channel_counts[channel]}});
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
      {"bounds",
       {{"refresh_interval_ms", refresh_ms},
        {"max_message_limit", 64u},
        {"max_pulse_limit", 512u}}}};
  result["bounds"]["snapshot_bytes_estimate"] = result.dump().size();
  return result;
}

} // namespace

graph::dashboard::EmbeddedDashboardServer::ApiHandler MakeApiHandler(
    std::shared_ptr<graph::dashboard::GraphConfigurationService> configuration_service) {
  return [service = std::move(configuration_service)](const ApiRequest &request,
                                           const graph::dashboard::EmbeddedDashboardServer::ApiContext &context)
             -> std::optional<ApiResponse> {
    if (context.stop_token.stop_requested() ||
        std::chrono::steady_clock::now() >= context.deadline) {
      return ErrorResponse(408, "request_timeout", "application handler deadline exceeded");
    }
    if (!service) {
      return std::nullopt;
    }
    const auto scenario = service->GetScenarioResponse().value(
        "scenario", nlohmann::json::object());
    if (request.method == "GET" && request.path == "/api/v1/fhss/visualization") {
      auto visualization = BuildVisualization(scenario, request.query, context);
      if (!visualization) {
        return ErrorResponse(408, "request_timeout", "application handler deadline exceeded");
      }
      (*visualization)["config_revision"] = service->ConfigRevision();
      return JsonResponse(200, *visualization);
    }
    return std::nullopt;
  };
}

} // namespace dsp::fhss::dashboard
