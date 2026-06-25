// SPDX-License-Identifier: MIT

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace graph::dashboard {
namespace {

std::string ReadFileToString(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    return {};
  }
  std::ostringstream stream;
  stream << input.rdbuf();
  return stream.str();
}

std::string JsonResponse(const nlohmann::json &json) { return json.dump(); }

nlohmann::json RuntimeStatusJson(const GraphRuntimeSession::StatusSnapshot &snapshot) {
  return nlohmann::json{{"schema", "graphx.dashboard.runtime_status.v1"},
                        {"lifecycle_state", GraphRuntimeSession::StateToString(snapshot.state)},
                        {"ready", snapshot.ready},
                        {"rebuild_allowed", snapshot.rebuild_allowed},
                        {"rebuild_blocked", snapshot.rebuild_blocked},
                        {"active_generation", snapshot.active_generation},
                        {"rebuild_attempts", snapshot.rebuild_attempts},
                        {"successful_rebuilds", snapshot.successful_rebuilds},
                        {"last_error",
                         snapshot.last_error_code.empty()
                             ? nlohmann::json(nullptr)
                             : nlohmann::json{{"code", snapshot.last_error_code},
                                              {"message", snapshot.last_error_message}}}};
}

std::string ErrorBody(int status_code, std::string code, std::string message,
                      std::string pointer = {}) {
  nlohmann::json error{{"schema", "graphx.dashboard.error.v1"},
                       {"status", status_code},
                       {"code", std::move(code)},
                       {"message", std::move(message)},
                       {"details", nullptr},
                       {"request_id", "dashboard"},
                       {"retriable", false}};
  if (!pointer.empty()) {
    error["pointer"] = std::move(pointer);
  }
  return error.dump();
}

bool StartsWith(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string Trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::optional<std::size_t> ParseContentLength(const std::string &request_text) {
  const auto header_end = request_text.find("\r\n\r\n");
  const auto header_block = request_text.substr(0, header_end);
  std::istringstream stream(header_block);
  std::string line;
  std::getline(stream, line);
  while (std::getline(stream, line)) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    auto name = Trim(line.substr(0, colon));
    auto value = Trim(line.substr(colon + 1));
    if (name == "Content-Length") {
      std::size_t parsed = 0;
      const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (ec == std::errc{}) {
        return parsed;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> ParseUint64(const std::string &value) {
  if (value.empty()) {
    return std::nullopt;
  }
  std::uint64_t parsed = 0;
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc{} || ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed;
}

bool ParseBool(const std::string &value) {
  return value == "1" || value == "true" || value == "yes";
}

} // namespace

EmbeddedDashboardServer::EmbeddedDashboardServer(
    Options options, std::shared_ptr<GraphConfigurationService> configuration_service,
    std::shared_ptr<GraphRuntimeSession> runtime_session,
    std::shared_ptr<GraphSnapshotCollector> snapshot_collector,
    std::shared_ptr<FHSSScenarioController> fhss_controller)
    : options_(std::move(options)),
      configuration_service_(std::move(configuration_service)),
      fhss_controller_(std::move(fhss_controller)),
      runtime_session_(std::move(runtime_session)),
      snapshot_collector_(std::move(snapshot_collector)) {
  if (snapshot_collector_ && runtime_session_) {
    snapshot_collector_->BindRuntimeSession(runtime_session_);
  }
}

EmbeddedDashboardServer::~EmbeddedDashboardServer() { Stop(); }

bool EmbeddedDashboardServer::Start() {
  if (running_.load()) {
    return true;
  }

  if (!ValidateStartup()) {
    return false;
  }

  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    last_error_ = "failed to create socket";
    return false;
  }

  const int reuse = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options_.port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    last_error_ = "failed to bind socket";
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (::listen(listen_fd_, 16) != 0) {
    last_error_ = "failed to listen on socket";
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  sockaddr_in bound_addr{};
  socklen_t bound_len = sizeof(bound_addr);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&bound_addr), &bound_len) == 0) {
    bound_port_ = ntohs(bound_addr.sin_port);
  }

  runtime_session_->MarkReady();
  running_.store(true);
  server_thread_ = std::thread([this] { RunLoop(); });
  return true;
}

void EmbeddedDashboardServer::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (runtime_session_) {
    runtime_session_->MarkShuttingDown();
  }

  if (listen_fd_ >= 0) {
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
  }

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  if (runtime_session_) {
    runtime_session_->MarkDead();
  }

  {
    std::scoped_lock lock(event_mutex_);
    clients_.clear();
    retained_events_.clear();
  }
}

bool EmbeddedDashboardServer::IsRunning() const { return running_.load(); }

std::uint16_t EmbeddedDashboardServer::BoundPort() const { return bound_port_; }

const std::string &EmbeddedDashboardServer::LastError() const { return last_error_; }

void EmbeddedDashboardServer::PublishEventForTesting(
    std::string event_type, nlohmann::json payload,
    std::optional<std::uint64_t> revision) {
  PublishEvent(std::move(event_type), std::move(payload), revision);
}

void EmbeddedDashboardServer::ExpireRetainedEventsForTesting() {
  std::scoped_lock lock(event_mutex_);
  retained_events_.clear();
}

void EmbeddedDashboardServer::SetEventQueueDepthForTesting(std::size_t depth) {
  std::scoped_lock lock(event_mutex_);
  per_client_queue_depth_ = std::max<std::size_t>(1, depth);
}

void EmbeddedDashboardServer::SetEventRetentionForTesting(
    std::chrono::milliseconds retention) {
  std::scoped_lock lock(event_mutex_);
  event_retention_window_ = retention;
}

bool EmbeddedDashboardServer::ValidateStartup() {
  if (!configuration_service_) {
    last_error_ = "missing configuration service";
    return false;
  }
  if (!runtime_session_) {
    last_error_ = "missing runtime session";
    return false;
  }
  if (!snapshot_collector_) {
    last_error_ = "missing snapshot collector";
    return false;
  }
  if (!configuration_service_->IsValid()) {
    last_error_ = "invalid effective graph configuration";
    return false;
  }

  if (options_.asset_directory.empty()) {
    last_error_ = "asset directory not set";
    return false;
  }

  std::error_code error;
  if (!std::filesystem::exists(options_.asset_directory, error) ||
      !std::filesystem::is_directory(options_.asset_directory, error)) {
    last_error_ = "asset directory is missing";
    return false;
  }

  if (!std::filesystem::exists(options_.asset_directory / "index.html", error)) {
    last_error_ = "index.html is missing from asset directory";
    return false;
  }

  if (options_.artifact_root.empty()) {
    options_.artifact_root = options_.asset_directory.parent_path();
  }
  configuration_service_->SetArtifactRoot(options_.artifact_root);
  return true;
}

void EmbeddedDashboardServer::RunLoop() {
  while (running_.load()) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (!running_.load()) {
        return;
      }
      continue;
    }

    std::string request_text;
    std::array<char, 4096> buffer{};
    for (;;) {
      const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
      if (received <= 0) {
        break;
      }
      request_text.append(buffer.data(), static_cast<std::size_t>(received));
      if (request_text.find("\r\n\r\n") != std::string::npos) {
        const auto content_length = ParseContentLength(request_text);
        if (!content_length) {
          break;
        }
        const auto header_end = request_text.find("\r\n\r\n");
        const auto body_start = header_end + 4;
        if (request_text.size() >= body_start + *content_length) {
          break;
        }
      }
    }

    Request request;
    Response response;
    if (ParseRequest(request_text, request)) {
      response = HandleRequest(request);
    } else {
      response.status_code = 400;
      response.content_type = "application/json";
      response.body = ErrorBody(400, "bad_request", "unable to parse request");
    }

    const auto wire = BuildHttpResponse(response);
    ::send(client_fd, wire.c_str(), wire.size(), 0);
    ::shutdown(client_fd, SHUT_RDWR);
    ::close(client_fd);
  }
}

std::string EmbeddedDashboardServer::StatusText(int status_code) {
  switch (status_code) {
  case 200:
    return "OK";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 409:
    return "Conflict";
  case 405:
    return "Method Not Allowed";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 503:
    return "Service Unavailable";
  default:
    return "Unknown";
  }
}

std::string EmbeddedDashboardServer::GuessContentType(const std::filesystem::path &path) {
  const auto ext = path.extension().string();
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".js") {
    return "text/javascript; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".json") {
    return "application/json";
  }
  return "application/octet-stream";
}

std::string EmbeddedDashboardServer::GetPathWithoutQuery(const std::string &target) {
  const auto query = target.find('?');
  if (query == std::string::npos) {
    return target;
  }
  return target.substr(0, query);
}

std::string EmbeddedDashboardServer::GetQueryValue(const std::string &query,
                                                  const std::string &key) {
  std::size_t begin = 0;
  while (begin < query.size()) {
    const auto end = query.find('&', begin);
    const auto token = query.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    const auto equal = token.find('=');
    const auto token_key = token.substr(0, equal);
    if (token_key == key) {
      return equal == std::string::npos ? std::string{} : token.substr(equal + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return {};
}

bool EmbeddedDashboardServer::ParseRequest(const std::string &request_text,
                                           Request &request) {
  const auto header_end = request_text.find("\r\n\r\n");
  const auto header_block = request_text.substr(0, header_end);
  std::istringstream stream(header_block);
  std::string http_version;
  if (!(stream >> request.method >> request.target >> http_version)) {
    return false;
  }
  request.path = GetPathWithoutQuery(request.target);
  const auto query = request.target.find('?');
  if (query != std::string::npos) {
    request.query = request.target.substr(query + 1);
  }

  const auto body_start = header_end == std::string::npos ? std::string::npos : header_end + 4;
  if (body_start != std::string::npos && body_start < request_text.size()) {
    const auto content_length = ParseContentLength(request_text);
    if (content_length && request_text.size() >= body_start + *content_length) {
      request.body = request_text.substr(body_start, *content_length);
    } else if (!content_length) {
      request.body = request_text.substr(body_start);
    }
  }
  return !request.method.empty() && !request.target.empty();
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleRequest(const Request &request) const {
  if (request.method == "GET") {
    if (request.path.rfind("/api/", 0) == 0 || request.path == "/healthz" ||
        request.path == "/readyz") {
      return HandleApiRequest(request);
    }
    return HandleStaticAsset(request);
  }

  if (request.method == "PATCH" || request.method == "POST" || request.method == "DELETE") {
    return HandleApiRequest(request);
  }

  return Response{.status_code = 405,
                  .content_type = "application/json",
                  .body = ErrorBody(405, "method_not_allowed", "method not allowed")};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleApiRequest(const Request &request) const {
  if (request.path == "/healthz") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"status", "ok"}})};
  }

  if (request.path == "/readyz") {
    const bool ready = runtime_session_ && runtime_session_->IsReady();
    return Response{.status_code = ready ? 200 : 503,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"ready", ready},
                                                        {"state", runtime_session_->StateString()}})};
  }

  if (request.path == "/api/v1/version") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"schema", "graphx.dashboard.version.v1"},
                                                        {"api_version", "v1"}})};
  }

  if (request.path == "/api/v1/events" && request.method == "GET") {
    const auto client_id = GetQueryValue(request.query, "client_id");
    const auto last_sequence_raw = GetQueryValue(request.query, "last_sequence");
    const auto disconnect = ParseBool(GetQueryValue(request.query, "disconnect"));

    std::optional<std::uint64_t> last_sequence;
    if (!last_sequence_raw.empty()) {
      last_sequence = ParseUint64(last_sequence_raw);
      if (!last_sequence) {
        return Response{.status_code = 400,
                        .content_type = "application/json",
                        .body = ErrorBody(400, "invalid_last_sequence",
                                          "last_sequence must be an unsigned integer")};
      }
    }

    const auto events = PollEvents(client_id.empty() ? "default" : client_id,
                                   last_sequence, disconnect);
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(events)};
  }

  if (request.path == "/api/v1/graph") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetGraphResponse())};
  }

  if (request.path == "/api/v1/config") {
    if (request.method == "GET") {
      return Response{.status_code = 200,
                      .content_type = "application/json",
                      .body = JsonResponse(configuration_service_->GetConfigResponse())};
    }
    if (request.method == "PATCH") {
      const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
      if (body.is_discarded()) {
        return Response{.status_code = 400,
                        .content_type = "application/json",
                        .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
      }
      const auto result = configuration_service_->PatchConfig(body);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 500)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
  }

  if (request.path == "/api/v1/config/authoritative") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetScenarioResponse())};
  }

  if (request.path == "/api/v1/config/effective") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetConfigResponse())};
  }

  if (request.path == "/api/v1/config/derived-paths") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetDerivedPathsResponse())};
  }

  if (request.path == "/api/v1/config/value") {
    const auto pointer = GetQueryValue(request.query, "pointer");
    if (pointer.empty()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "missing_pointer", "pointer query parameter is required")};
    }
    const auto result = configuration_service_->GetValueResponse(pointer);
    const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                            ? result.value("status", 404)
                            : 200;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (StartsWith(request.path, "/api/v1/nodes/")) {
    const auto node_suffix = request.path.substr(std::string{"/api/v1/nodes/"}.size());
    const std::string parameters_suffix = "/parameters";
    const auto parameters_pos = node_suffix.rfind(parameters_suffix);
    if (parameters_pos != std::string::npos &&
        parameters_pos == node_suffix.size() - parameters_suffix.size()) {
      const auto node_id = node_suffix.substr(0, parameters_pos);
      const auto result = configuration_service_->GetNodeParametersResponse(node_id);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 404)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    const auto result = configuration_service_->GetNodeResponse(node_suffix);
    const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                            ? result.value("status", 404)
                            : 200;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (request.path == "/api/v1/metrics") {
    PublishEvent("metrics", snapshot_collector_->GetMetricsSnapshot());
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(snapshot_collector_->GetMetricsSnapshot())};
  }

  if (request.path == "/api/v1/metrics/edges") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(snapshot_collector_->GetEdgeMetricsSnapshot())};
  }

  if (request.path == "/api/v1/diagnostics") {
    PublishEvent("diagnostics", snapshot_collector_->GetDiagnosticsSnapshot());
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(snapshot_collector_->GetDiagnosticsSnapshot())};
  }

  if (request.path == "/api/v1/config/validate" && request.method == "POST") {
    const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }
    const auto result = configuration_service_->ValidateConfig(body);
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (request.path == "/api/v1/config/rebuild" && request.method == "POST") {
    const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }

    const auto expected_revision = body.value("expected_revision", configuration_service_->ConfigRevision());
    if (expected_revision != configuration_service_->ConfigRevision()) {
      nlohmann::json error{{"schema", "graphx.dashboard.error.v1"},
                           {"status", 409},
                           {"code", "stale_revision_conflict"},
                           {"message", "expected revision does not match current revision"},
                           {"details", nlohmann::json{{"current_revision", configuration_service_->ConfigRevision()}}},
                           {"request_id", "dashboard"},
                           {"retriable", false}};
      return Response{.status_code = 409,
                      .content_type = "application/json",
                      .body = JsonResponse(error)};
    }

    const auto rebuild = runtime_session_->Rebuild();
    if (rebuild.status_code == 409 || rebuild.status_code == 500 || rebuild.status_code == 503) {
      return Response{.status_code = rebuild.status_code,
                      .content_type = "application/json",
                      .body = ErrorBody(rebuild.status_code, rebuild.code, rebuild.message)};
    }

    const auto status = runtime_session_->SnapshotStatus();
    return Response{.status_code = 202,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"schema", "graphx.dashboard.rebuild_result.v1"},
                                                        {"command_id", body.value("command_id", std::string{})},
                                                        {"status", rebuild.code == "cleanup_failed" ? "succeeded_with_cleanup_failed"
                                                                                                        : "succeeded"},
                                                        {"submitted_revision", configuration_service_->ConfigRevision()},
                                                        {"lifecycle_state", GraphRuntimeSession::StateToString(status.state)},
                                                        {"active_generation", status.active_generation},
                                                        {"warning", rebuild.code == "cleanup_failed" ? nlohmann::json{{"code", rebuild.code},
                                                                                                                          {"message", rebuild.message}}
                                                                                                          : nlohmann::json(nullptr)}})};
  }

  if (request.path == "/api/v1/status" && request.method == "GET") {
    const auto status = RuntimeStatusJson(runtime_session_->SnapshotStatus());
    PublishEvent("status", status);
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(status)};
  }

  if (request.path == "/api/v1/commands/start" && request.method == "POST") {
    const auto result = runtime_session_->Start();
    PublishEvent("command",
                 nlohmann::json{{"command", "start"},
                                {"status", result.status_code == 202 ? "accepted" : "rejected"},
                                {"code", result.code},
                                {"message", result.message}});
    if (result.status_code != 202) {
      return Response{.status_code = result.status_code,
                      .content_type = "application/json",
                      .body = ErrorBody(result.status_code, result.code, result.message)};
    }
    return Response{.status_code = 202,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"schema", "graphx.dashboard.command_result.v1"},
                                                        {"status", "accepted"},
                                                        {"code", result.code},
                                                        {"message", result.message}})};
  }

  if (request.path == "/api/v1/commands/stop" && request.method == "POST") {
    const auto result = runtime_session_->Stop();
    PublishEvent("command",
                 nlohmann::json{{"command", "stop"},
                                {"status", result.status_code == 202 ? "accepted" : "rejected"},
                                {"code", result.code},
                                {"message", result.message}});
    if (result.status_code != 202) {
      return Response{.status_code = result.status_code,
                      .content_type = "application/json",
                      .body = ErrorBody(result.status_code, result.code, result.message)};
    }
    return Response{.status_code = 202,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"schema", "graphx.dashboard.command_result.v1"},
                                                        {"status", "accepted"},
                                                        {"code", result.code},
                                                        {"message", result.message}})};
  }

  if ((request.path == "/api/v1/commands/step-message" ||
       request.path == "/api/v1/commands/continue" ||
       request.path == "/api/v1/commands/reset") &&
      request.method == "POST") {
    if (!fhss_controller_) {
      return Response{.status_code = 501,
                      .content_type = "application/json",
                      .body = ErrorBody(501, "not_implemented",
                                        "FHSS Step-5 controller is not configured")};
    }
    const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }
    const auto result = request.path == "/api/v1/commands/step-message"
                            ? fhss_controller_->StepOneMessage(body)
                            : request.path == "/api/v1/commands/continue"
                                  ? fhss_controller_->Continue(body)
                                  : fhss_controller_->Reset(body);
    PublishEvent("fhss_progress",
           nlohmann::json{{"path", request.path},
                  {"status_code", result.status_code},
                  {"response", result.body}});
    return Response{.status_code = result.status_code,
                    .content_type = "application/json",
                    .body = JsonResponse(result.body)};
  }

  if (request.path == "/api/v1/config/discard" && request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->DiscardEdits())};
  }

  if (request.path == "/api/v1/config/undo" && request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->UndoLastEdit())};
  }

  if (request.path == "/api/v1/config/export" && request.method == "POST") {
    const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }
    const auto result = configuration_service_->ExportConfig(body);
    const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                            ? result.value("status", 500)
                            : 202;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (StartsWith(request.path, "/api/v1/operations/")) {
    const auto operation_suffix = request.path.substr(std::string{"/api/v1/operations/"}.size());
    const std::string cancel_suffix = "/cancel";
    if (request.method == "POST" &&
        operation_suffix.size() > cancel_suffix.size() &&
        operation_suffix.rfind(cancel_suffix) == operation_suffix.size() - cancel_suffix.size()) {
      const auto operation_id = operation_suffix.substr(0, operation_suffix.size() - cancel_suffix.size());
      if (fhss_controller_) {
        if (const auto result = fhss_controller_->CancelOperationIfKnown(operation_id); result) {
          const auto status = result->value("schema", std::string{}) == "graphx.dashboard.error.v1"
                                  ? result->value("status", 409)
                                  : 200;
          return Response{.status_code = status,
                          .content_type = "application/json",
                          .body = JsonResponse(*result)};
        }
      }
      const auto result = configuration_service_->CancelOperation(operation_id);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 409)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "GET") {
      if (fhss_controller_) {
        if (const auto result = fhss_controller_->GetOperationResponseIfKnown(operation_suffix); result) {
          return Response{.status_code = 200,
                          .content_type = "application/json",
                          .body = JsonResponse(*result)};
        }
      }
      const auto result = configuration_service_->GetOperationResponse(operation_suffix);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 404)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "DELETE") {
      if (fhss_controller_) {
        std::string error_code;
        if (const auto deleted = fhss_controller_->DeleteOperationIfKnown(operation_suffix, &error_code);
            deleted.has_value()) {
          if (!*deleted) {
            return Response{.status_code = 409,
                            .content_type = "application/json",
                            .body = ErrorBody(409,
                                              error_code.empty() ? "operation_not_terminal" : error_code,
                                              "operation is not terminal")};
          }
          return Response{.status_code = 204, .content_type = "application/json", .body = {}};
        }
      }
      std::string error_code;
      if (!configuration_service_->DeleteOperation(operation_suffix, &error_code)) {
        const auto status = error_code == "operation_not_terminal" ? 409 : 404;
        return Response{.status_code = status,
                        .content_type = "application/json",
                        .body = ErrorBody(status, error_code.empty() ? "operation_not_found_or_expired" : error_code,
                                          error_code == "operation_not_terminal"
                                              ? "operation is not terminal"
                                              : "operation not found or expired")};
      }
      return Response{.status_code = 204, .content_type = "application/json", .body = {}};
    }
  }

  if (request.path == "/api/v1/fhss/scenario" && request.method == "GET") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetScenarioResponse())};
  }

  return Response{.status_code = 404,
                  .content_type = "application/json",
                  .body = ErrorBody(404, "not_found", "resource not found")};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleStaticAsset(const Request &request) const {
  const std::string raw_target = request.path == "/" ? "/index.html" : request.path;

  std::filesystem::path relative = raw_target;
  if (relative.is_absolute()) {
    relative = relative.relative_path();
  }

  std::filesystem::path resolved = options_.asset_directory / relative;
  std::error_code error;
  const auto canonical_root = std::filesystem::weakly_canonical(options_.asset_directory, error);
  const auto canonical_file = std::filesystem::weakly_canonical(resolved, error);
  if (error || canonical_file.native().find(canonical_root.native()) != 0) {
    return Response{.status_code = 404,
                    .content_type = "application/json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }

  const std::string body = ReadFileToString(canonical_file);
  if (body.empty()) {
    return Response{.status_code = 404,
                    .content_type = "application/json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }

  return Response{.status_code = 200,
                  .content_type = GuessContentType(canonical_file),
                  .body = body};
}

std::string EmbeddedDashboardServer::BuildHttpResponse(const Response &response) {
  std::ostringstream wire;
  wire << "HTTP/1.1 " << response.status_code << ' ' << StatusText(response.status_code)
       << "\r\n";
  wire << "Content-Type: " << response.content_type << "\r\n";
  wire << "Content-Length: " << response.body.size() << "\r\n";
  wire << "Connection: close\r\n\r\n";
  wire << response.body;
  return wire.str();
}

std::string EmbeddedDashboardServer::NowIso8601() const {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds);
  std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
         << std::setfill('0') << ms.count() << 'Z';
  return stream.str();
}

nlohmann::json EmbeddedDashboardServer::EventEnvelopeJson(
    const EventEnvelope &event) const {
  nlohmann::json json{{"schema", "graphx.dashboard.event.v1"},
                      {"event_type", event.event_type},
                      {"sequence", event.sequence},
                      {"timestamp", event.timestamp},
                      {"payload", event.payload}};
  if (event.revision.has_value()) {
    json["revision"] = *event.revision;
  }
  return json;
}

void EmbeddedDashboardServer::TrimRetainedEventsLocked(
    std::chrono::system_clock::time_point now) const {
  while (!retained_events_.empty()) {
    const auto expired = retained_events_.front().second + event_retention_window_ < now;
    if (!expired) {
      break;
    }
    retained_events_.pop_front();
  }

  constexpr std::size_t kMaxRetainedEvents = 4096;
  while (retained_events_.size() > kMaxRetainedEvents) {
    retained_events_.pop_front();
  }
}

void EmbeddedDashboardServer::PublishEvent(
    std::string event_type, nlohmann::json payload,
    std::optional<std::uint64_t> revision) const {
  const auto now = std::chrono::system_clock::now();
  std::scoped_lock lock(event_mutex_);
  TrimRetainedEventsLocked(now);

  EventEnvelope envelope;
  envelope.sequence = next_event_sequence_++;
  envelope.event_type = std::move(event_type);
  envelope.timestamp = NowIso8601();
  envelope.revision = revision;
  envelope.payload = std::move(payload);

  retained_events_.emplace_back(envelope, now);

  for (auto &[client_id, client] : clients_) {
    if (client.resync_required) {
      (void)client_id;
      continue;
    }
    if (client.queue.size() >= per_client_queue_depth_) {
      dropped_events_total_ += client.queue.size();
      client.dropped_events += client.queue.size();
      client.queue.clear();
      client.resync_required = true;
      ++coalesced_events_total_;
      continue;
    }
    client.queue.push_back(envelope);
  }
}

nlohmann::json EmbeddedDashboardServer::PollEvents(
    const std::string &client_id, std::optional<std::uint64_t> last_sequence,
    bool clear_client) const {
  std::vector<EventEnvelope> outbound;
  bool resync_required = false;
  std::uint64_t latest_sequence = 0;
  std::uint64_t dropped_events = 0;
  std::uint64_t delivered_through = last_sequence.value_or(0);

  {
    std::scoped_lock lock(event_mutex_);
    const auto now = std::chrono::system_clock::now();
    TrimRetainedEventsLocked(now);

    if (clear_client) {
      clients_.erase(client_id);
      ++reconnects_total_;
    }

    auto &client = clients_[client_id];
    if (client.queue.empty() && !clear_client) {
      ++reconnects_total_;
    }

    dropped_events = client.dropped_events;
    client.dropped_events = 0;

    if (!retained_events_.empty()) {
      latest_sequence = retained_events_.back().first.sequence;
      if (last_sequence.has_value()) {
        const auto expected_first = *last_sequence + 1;
        const auto first_available = retained_events_.front().first.sequence;
        if (expected_first < first_available || *last_sequence > latest_sequence) {
          client.resync_required = true;
          resync_required = true;
        } else if (expected_first <= latest_sequence) {
          std::uint64_t expected = expected_first;
          for (const auto &[event, _time_point] : retained_events_) {
            if (event.sequence <= *last_sequence) {
              continue;
            }
            if (event.sequence != expected) {
              client.resync_required = true;
              resync_required = true;
              break;
            }
            outbound.push_back(event);
            delivered_through = event.sequence;
            ++expected;
          }
        }
      }
    }

    if (client.resync_required) {
      resync_required = true;
      outbound.clear();
      client.queue.clear();
    } else {
      while (!client.queue.empty()) {
        if (client.queue.front().sequence > delivered_through) {
          outbound.push_back(client.queue.front());
        }
        client.queue.pop_front();
      }
      if (!outbound.empty()) {
        latest_sequence = std::max(latest_sequence, outbound.back().sequence);
      }
    }
  }

  nlohmann::json events = nlohmann::json::array();
  for (const auto &event : outbound) {
    events.push_back(EventEnvelopeJson(event));
  }

  return nlohmann::json{{"schema", "graphx.dashboard.events_batch.v1"},
                        {"stream", "/api/v1/events"},
                        {"client_id", client_id},
                        {"resync_required", resync_required},
                        {"latest_sequence", latest_sequence},
                        {"events", std::move(events)},
                        {"counters", nlohmann::json{{"dropped_events", dropped_events},
                                                      {"dropped_events_total", dropped_events_total_},
                                                      {"coalesced_events_total", coalesced_events_total_},
                                                      {"reconnects_total", reconnects_total_}}}};
}

} // namespace graph::dashboard
