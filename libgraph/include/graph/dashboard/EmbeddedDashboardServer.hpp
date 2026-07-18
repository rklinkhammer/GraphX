// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace graph::dashboard {

class EmbeddedDashboardServer {
public:
  struct ApiRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
  };

  struct ApiResponse {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
  };

  struct ApiContext {
    std::chrono::steady_clock::time_point deadline;
    std::stop_token stop_token;
  };

  using ApiHandler = std::function<std::optional<ApiResponse>(
      const ApiRequest &request, const ApiContext &context)>;

  struct ApiHandlerRegistration {
    ApiHandler handler;
    bool cooperative_cancellation = false;
    std::chrono::milliseconds maximum_checkpoint_latency{0};
  };

  struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::filesystem::path asset_directory;
    std::filesystem::path artifact_root;
    std::optional<ApiHandlerRegistration> application_api_handler;
    bool enable_mutating_routes = false;
    std::size_t max_header_bytes = 16 * 1024;
    std::size_t max_body_bytes = 1024 * 1024;
    std::size_t max_response_bytes = 4 * 1024 * 1024;
    std::size_t max_concurrent_connections = 16;
    std::size_t max_json_depth = 64;
    std::size_t max_json_members = 16384;
    std::size_t max_json_string_bytes = 256 * 1024;
    double max_json_number_magnitude = 1.0e15;
    std::chrono::milliseconds idle_timeout{5000};
    std::chrono::milliseconds read_timeout{10000};
    std::chrono::milliseconds write_timeout{5000};
    std::chrono::milliseconds total_request_timeout{15000};
  };

  EmbeddedDashboardServer(Options options,
                          std::shared_ptr<GraphConfigurationService> configuration_service,
                          std::shared_ptr<GraphRuntimeSession> runtime_session,
                          std::shared_ptr<GraphSnapshotCollector> snapshot_collector);
  ~EmbeddedDashboardServer();

  EmbeddedDashboardServer(const EmbeddedDashboardServer &) = delete;
  EmbeddedDashboardServer &operator=(const EmbeddedDashboardServer &) = delete;

  [[nodiscard]] bool Start();
  void Stop();

  [[nodiscard]] bool IsRunning() const;
  [[nodiscard]] std::uint16_t BoundPort() const;
  [[nodiscard]] const std::string &BoundHost() const;
  [[nodiscard]] const std::string &LastError() const;

  void PublishEventForTesting(std::string event_type,
                              nlohmann::json payload,
                              std::optional<std::uint64_t> revision = std::nullopt);
  void ExpireRetainedEventsForTesting();
  void SetEventQueueDepthForTesting(std::size_t depth);
  void SetEventRetentionForTesting(std::chrono::milliseconds retention);

private:
  struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::chrono::steady_clock::time_point deadline;
  };

  struct Response {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::string allow;
  };

  bool ValidateStartup();
  void RunLoop();

  static std::string GuessContentType(const std::filesystem::path &path);

  static std::string GetPathWithoutQuery(const std::string &target);
  static std::string GetQueryValue(const std::string &query, const std::string &key);
  Response HandleRequest(const Request &request) const;
  Response HandleApiRequest(const Request &request) const;
  Response HandleStaticAsset(const Request &request) const;

  struct EventEnvelope {
    std::uint64_t sequence = 0;
    std::string event_type;
    std::string timestamp;
    std::optional<std::uint64_t> revision;
    nlohmann::json payload = nlohmann::json::object();
  };

  struct ClientState {
    std::deque<EventEnvelope> queue;
    bool resync_required = false;
    std::uint64_t dropped_events = 0;
  };

  [[nodiscard]] nlohmann::json PollEvents(const std::string &client_id,
                                          std::optional<std::uint64_t> last_sequence,
                                          bool clear_client) const;
  void PublishEvent(std::string event_type,
                    nlohmann::json payload,
                    std::optional<std::uint64_t> revision = std::nullopt) const;
  void TrimRetainedEventsLocked(std::chrono::system_clock::time_point now) const;
  [[nodiscard]] std::string NowIso8601() const;
  [[nodiscard]] nlohmann::json EventEnvelopeJson(const EventEnvelope &event) const;

  Options options_;
  std::shared_ptr<GraphConfigurationService> configuration_service_;
  std::shared_ptr<GraphRuntimeSession> runtime_session_;
  std::shared_ptr<GraphSnapshotCollector> snapshot_collector_;

  struct ServerState;
  std::unique_ptr<ServerState> server_state_;
  std::atomic<bool> running_{false};
  std::thread server_thread_;
  std::uint16_t bound_port_ = 0;
  std::string bound_host_;
  std::string last_error_;

  mutable std::mutex event_mutex_;
  mutable std::deque<std::pair<EventEnvelope, std::chrono::system_clock::time_point>> retained_events_;
  mutable std::unordered_map<std::string, ClientState> clients_;
  mutable std::uint64_t next_event_sequence_ = 1;
  mutable std::uint64_t dropped_events_total_ = 0;
  mutable std::uint64_t coalesced_events_total_ = 0;
  mutable std::uint64_t reconnects_total_ = 0;
  mutable std::size_t per_client_queue_depth_ = 128;
  mutable std::chrono::milliseconds event_retention_window_{std::chrono::seconds(120)};
};

} // namespace graph::dashboard
