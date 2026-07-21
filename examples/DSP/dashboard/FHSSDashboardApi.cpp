// SPDX-License-Identifier: MIT

#include "FHSSDashboardApi.hpp"
#include "FHSSJobController.hpp"
#include "FHSSInvestigationBundleService.hpp"
#include "FHSSObservationService.hpp"

#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace dsp::fhss::dashboard {
namespace {

constexpr std::uint64_t kPulsePeriodSamples = 6'500;

using ApiRequest = graph::dashboard::EmbeddedDashboardServer::ApiRequest;
using ApiResponse = graph::dashboard::EmbeddedDashboardServer::ApiResponse;

ApiResponse JsonResponse(int status, const nlohmann::json &body) {
  return {.status_code = status,
          .content_type = "application/json",
          .body = body.dump()};
}

ApiResponse ErrorResponse(int status, std::string code, std::string message) {
  const auto body =
      nlohmann::json{{"type", "urn:graphx:dashboard:problem:" + code},
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
    const auto item = query.substr(
        offset, end == std::string::npos ? std::string::npos : end - offset);
    const auto equals = item.find('=');
    if (item.substr(0, equals) == name) {
      return equals == std::string::npos ? std::string{}
                                         : item.substr(equals + 1);
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

std::uint64_t BoundedQuery(const std::string &query, std::string_view name,
                           std::uint64_t fallback, std::uint64_t minimum,
                           std::uint64_t maximum) {
  const auto value = ParseUnsigned(QueryValue(query, name)).value_or(fallback);
  return std::clamp(value, minimum, maximum);
}

bool Cancelled(
    const graph::dashboard::EmbeddedDashboardServer::ApiContext &context) {
  return context.stop_token.stop_requested() ||
         std::chrono::steady_clock::now() >= context.deadline;
}

std::optional<nlohmann::json> BuildVisualization(
    const nlohmann::json &scenario, const std::string &query,
    const graph::dashboard::EmbeddedDashboardServer::ApiContext &context) {
  const auto messages =
      scenario.contains("messages") && scenario.at("messages").is_array()
          ? scenario.at("messages")
          : nlohmann::json::array();
  const auto message_offset = static_cast<std::size_t>(
      BoundedQuery(query, "message_offset", 0, 0, messages.size()));
  const auto message_limit =
      static_cast<std::size_t>(BoundedQuery(query, "message_limit", 16, 1, 64));
  const auto pulse_offset = static_cast<std::size_t>(BoundedQuery(
      query, "pulse_offset", 0, 0, std::numeric_limits<std::uint64_t>::max()));
  const auto pulse_limit =
      static_cast<std::size_t>(BoundedQuery(query, "pulse_limit", 128, 1, 512));
  const auto refresh_ms = BoundedQuery(query, "refresh_ms", 250, 100, 2000);

  nlohmann::json schedule_messages = nlohmann::json::array();
  nlohmann::json timeline = nlohmann::json::array();
  std::array<std::uint64_t, 64> channel_counts{};
  std::size_t absolute_pulse = 0;

  for (std::size_t message_index = 0; message_index < messages.size();
       ++message_index) {
    if (Cancelled(context))
      return std::nullopt;
    const auto &message = messages.at(message_index);
    const auto pulses =
        message.contains("pulses") && message.at("pulses").is_array()
            ? message.at("pulses")
            : nlohmann::json::array();
    std::uint64_t preamble_count = 0;

    for (std::size_t pulse_index = 0; pulse_index < pulses.size();
         ++pulse_index, ++absolute_pulse) {
      if (Cancelled(context))
        return std::nullopt;
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
        timeline.push_back(
            {{"absolute_pulse_index", absolute_pulse},
             {"message_index", message_index},
             {"message_id", message.value("message_id", message_index + 1u)},
             {"pulse_index", pulse_index},
             {"frequency_index", channel},
             {"expected_sample_start",
              message.value("transmit_start_sample", std::uint64_t{0}) +
                  pulse_index * kPulsePeriodSamples},
             {"source", "configured_schedule"}});
      }
    }

    if (message_index >= message_offset &&
        message_index < message_offset + message_limit) {
      schedule_messages.push_back(
          {{"message_index", message_index},
           {"message_id", message.value("message_id", message_index + 1u)},
           {"transmit_start_sample",
            message.value("transmit_start_sample", std::uint64_t{0})},
           {"pulse_count", pulses.size()},
           {"preamble_pulse_count", preamble_count},
           {"body_pulse_count", pulses.size() - preamble_count}});
    }
  }

  nlohmann::json channels = nlohmann::json::array();
  for (std::size_t channel = 0; channel < channel_counts.size(); ++channel) {
    if (Cancelled(context))
      return std::nullopt;
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
        {"total_pulse_count", absolute_pulse},
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
    std::shared_ptr<graph::dashboard::GraphConfigurationService>
        configuration_service,
    std::shared_ptr<graph::dashboard::GraphRuntimeSession> runtime_session,
    std::shared_ptr<FHSSJobController> job_controller,
    std::shared_ptr<FHSSInvestigationBundleService> investigation_service) {
  auto observation_service = std::make_shared<FHSSObservationService>(
      configuration_service, std::move(runtime_session));
  return [service = std::move(configuration_service),
          observation_service = std::move(observation_service),
          job_controller = std::move(job_controller),
          investigation_service = std::move(investigation_service)](
             const ApiRequest &request,
             const graph::dashboard::EmbeddedDashboardServer::ApiContext
                 &context) -> std::optional<ApiResponse> {
    if (context.stop_token.stop_requested() ||
        std::chrono::steady_clock::now() >= context.deadline) {
      return ErrorResponse(408, "request_timeout",
                           "application handler deadline exceeded");
    }
    if (!service) {
      return std::nullopt;
    }
    const auto job_response = [](FHSSJobController::Result result) {
      return ApiResponse{.status_code = result.status_code,
                         .content_type = result.status_code >= 400
                                             ? "application/problem+json"
                                             : "application/json",
                         .body = result.document.dump()};
    };
    const auto investigation_response = [](FHSSInvestigationBundleService::Result result) {
      return ApiResponse{.status_code = result.status_code,
                         .content_type = result.status_code >= 400
                                             ? "application/problem+json"
                                             : "application/json",
                         .body = result.document.dump()};
    };
    const auto parse_body = [&]() -> std::optional<nlohmann::json> {
      try {
        return nlohmann::json::parse(request.body);
      } catch (...) {
        return std::nullopt;
      }
    };
    if (investigation_service && request.method == "GET" &&
        request.path == "/api/v1/fhss/investigations/operations")
      return investigation_response(investigation_service->List());
    if (investigation_service && request.method == "GET" &&
        request.path == "/api/v1/fhss/investigations/quota")
      return investigation_response(investigation_service->Quota());
    if (investigation_service && request.method == "POST" &&
        (request.path == "/api/v1/fhss/investigations/exports" ||
         request.path == "/api/v1/fhss/investigations/import-validations" ||
         request.path == "/api/v1/fhss/investigations/replays")) {
      const auto body = parse_body();
      if (!body)
        return ErrorResponse(400, "invalid_json",
                             "investigation request is not valid JSON");
      const auto key = request.headers.contains("idempotency-key")
                           ? request.headers.at("idempotency-key")
                           : std::string{};
      if (request.path.ends_with("/exports"))
        return investigation_response(investigation_service->SubmitExport(*body, key));
      if (request.path.ends_with("/import-validations"))
        return investigation_response(investigation_service->SubmitValidation(*body, key));
      return investigation_response(investigation_service->SubmitReplay(*body, key));
    }
    constexpr std::string_view investigation_prefix =
        "/api/v1/fhss/investigations/operations/";
    if (investigation_service && request.path.starts_with(investigation_prefix)) {
      const auto suffix = request.path.substr(investigation_prefix.size());
      constexpr std::string_view cancel_suffix = "/cancel";
      if (request.method == "POST" && suffix.ends_with(cancel_suffix)) {
        const auto body = parse_body();
        if (!body || !body->is_object() || !body->empty())
          return ErrorResponse(400, "invalid_cancel_request",
                               "cancel body must be an empty object");
        return investigation_response(investigation_service->Cancel(
            suffix.substr(0, suffix.size() - cancel_suffix.size())));
      }
      if (request.method == "GET" && suffix.find('/') == std::string::npos)
        return investigation_response(investigation_service->Get(suffix));
    }
    if (job_controller && request.method == "GET" &&
        request.path == "/api/v1/fhss/jobs")
      return job_response(job_controller->List());
    if (job_controller && request.method == "POST" &&
        request.path == "/api/v1/fhss/jobs") {
      const auto body = parse_body();
      if (!body)
        return ErrorResponse(400, "invalid_json",
                             "job request is not valid JSON");
      const auto key = request.headers.contains("idempotency-key")
                           ? request.headers.at("idempotency-key")
                           : std::string{};
      return job_response(job_controller->Submit(*body, key));
    }
    constexpr std::string_view jobs_prefix = "/api/v1/fhss/jobs/";
    if (job_controller && request.path.starts_with(jobs_prefix)) {
      const auto suffix = request.path.substr(jobs_prefix.size());
      constexpr std::string_view cancel_suffix = "/cancel";
      if (request.method == "POST" && suffix.ends_with(cancel_suffix)) {
        const auto body = parse_body();
        if (!body || !body->is_object() || !body->empty())
          return ErrorResponse(400, "invalid_cancel_request",
                               "cancel body must be an empty object");
        const auto job_id =
            suffix.substr(0, suffix.size() - cancel_suffix.size());
        return job_response(job_controller->Cancel(job_id));
      }
      if (request.method == "GET" && suffix.find('/') == std::string::npos)
        return job_response(job_controller->Get(suffix));
    }
    if (job_controller && request.method == "POST" &&
        (request.path == "/api/v1/fhss/commands/step" ||
         request.path == "/api/v1/fhss/commands/continue")) {
      auto body = parse_body();
      if (!body || !body->is_object())
        return ErrorResponse(400, "invalid_json",
                             "command request is not valid JSON");
      (*body)["operation"] =
          request.path.ends_with("/step") ? "step" : "continue";
      const auto key = request.headers.contains("idempotency-key")
                           ? request.headers.at("idempotency-key")
                           : std::string{};
      return job_response(job_controller->Submit(*body, key));
    }
    if (job_controller && request.method == "POST" &&
        request.path == "/api/v1/fhss/commands/reset") {
      const auto body = parse_body();
      if (!body || !body->is_object() || !body->empty())
        return ErrorResponse(400, "invalid_reset_request",
                             "reset body must be an empty object");
      return job_response(job_controller->Reset());
    }
    const auto scenario = service->GetScenarioResponse().value(
        "scenario", nlohmann::json::object());
    if (request.method == "GET" &&
        request.path == "/api/v1/fhss/visualization") {
      auto visualization = BuildVisualization(scenario, request.query, context);
      if (!visualization) {
        return ErrorResponse(408, "request_timeout",
                             "application handler deadline exceeded");
      }
      (*visualization)["config_revision"] = service->ConfigRevision();
      return JsonResponse(200, *visualization);
    }
    if (request.method == "GET" &&
        request.path == "/api/v1/fhss/expected-truth") {
      return JsonResponse(200, observation_service->ExpectedTruth().document);
    }
    if (request.method == "GET" &&
        request.path == "/api/v1/fhss/observations") {
      return JsonResponse(200,
                          observation_service->ReceiverObservation().document);
    }
    if (request.method == "GET" && request.path == "/api/v1/fhss/comparison") {
      return JsonResponse(200, observation_service->Comparison().document);
    }
    if (request.method == "GET" &&
        request.path == "/api/v1/fhss/observation-provenance") {
      return JsonResponse(200, observation_service->Provenance());
    }
    if (request.method == "GET" &&
        request.path == "/api/v1/fhss/observation-history") {
      return JsonResponse(200, observation_service->History());
    }
    if (request.method == "GET" && request.path == "/api/v1/fhss/spectrum") {
      const auto channel_text = QueryValue(request.query, "channel");
      const auto fft_text = QueryValue(request.query, "fft_size");
      const auto channel = channel_text.empty() ? std::optional<std::uint64_t>{}
                                                : ParseUnsigned(channel_text);
      const auto fft_size = fft_text.empty() ? std::optional<std::uint64_t>(128)
                                             : ParseUnsigned(fft_text);
      if ((!channel_text.empty() && !channel) || !fft_size ||
          (channel && *channel > 63) || *fft_size > 256 || *fft_size < 16 ||
          !std::has_single_bit(*fft_size)) {
        return ErrorResponse(400, "invalid_spectrum_request",
                             "channel or fft_size is invalid");
      }
      return JsonResponse(
          200, observation_service->Spectrum(
                   channel ? std::optional<std::uint32_t>(
                                 static_cast<std::uint32_t>(*channel))
                           : std::nullopt,
                   static_cast<std::size_t>(*fft_size)));
    }
    return std::nullopt;
  };
}

} // namespace dsp::fhss::dashboard
