// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
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
    std::unordered_map<std::string, std::string> headers;
  };

  struct ApiResponse {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
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
    /// Optional operational/test gate. Returning false rejects new WebSocket
    /// upgrades and closes maintained streams while leaving HTTP available.
    std::function<bool()> websocket_availability_probe;
    bool enable_mutating_routes = false;
    bool enable_configuration_mutation_routes = false;
    bool enable_runtime_control_routes = true;
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
    std::size_t max_websocket_clients = 8;
    std::size_t max_websocket_frame_bytes = 64 * 1024;
    std::size_t max_websocket_message_bytes = 256 * 1024;
    std::size_t max_websocket_fragments_per_message = 32;
    std::size_t max_websocket_commands_per_second = 16;
    std::size_t max_websocket_events_per_second = 256;
    std::size_t max_websocket_replay_events = 256;
    std::size_t max_websocket_replay_bytes = 2 * 1024 * 1024;
    std::size_t max_websocket_queue_events = 128;
    std::size_t max_websocket_queue_bytes = 2 * 1024 * 1024;
    std::size_t max_publisher_ingress_events = 1024;
    std::size_t max_publisher_ingress_bytes = 8 * 1024 * 1024;
    std::size_t max_retained_event_bytes = 8 * 1024 * 1024;
    std::size_t max_retained_events = 4096;
    std::chrono::milliseconds websocket_idle_timeout{30000};
    std::chrono::milliseconds websocket_heartbeat_interval{10000};
    std::chrono::milliseconds websocket_max_lifetime{3600000};
    std::chrono::milliseconds websocket_close_timeout{1000};
    std::chrono::milliseconds websocket_client_state_ttl{300000};
    std::chrono::milliseconds event_retention_window{120000};
  };

  EmbeddedDashboardServer(
      Options options,
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

  /// Production event ingress. FHSS application services provide correlation
  /// and semantic metadata in `context`; the transport supplies ordering,
  /// publisher epoch, time, configuration, generation, and run provenance.
  void PublishEvent(std::string event_type, nlohmann::json payload,
                    nlohmann::json context = nlohmann::json::object()) const;
  void ExpireRetainedEventsForTesting();
  void SetEventQueueDepthForTesting(std::size_t depth);
  void SetEventRetentionForTesting(std::chrono::milliseconds retention);
  void SetPublisherPausedForTesting(bool paused);

private:
  struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::chrono::steady_clock::time_point deadline;
    std::unordered_map<std::string, std::string> headers;
  };

  struct Response {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
    std::string allow;
    std::unordered_map<std::string, std::string> headers;
  };

  bool ValidateStartup();
  void RunLoop();

  static std::string GuessContentType(const std::filesystem::path &path);

  static std::string GetPathWithoutQuery(const std::string &target);
  static std::string GetQueryValue(const std::string &query,
                                   const std::string &key);
  Response HandleRequest(const Request &request) const;
  Response HandleApiRequest(const Request &request) const;
  Response HandleStaticAsset(const Request &request) const;

  struct EventEnvelope {
    std::uint64_t sequence = 0;
    std::string event_type;
    std::string timestamp;
    std::string publisher_epoch;
    std::uint64_t generation = 0;
    std::uint64_t run_epoch = 0;
    std::uint64_t config_revision = 0;
    std::string config_etag;
    nlohmann::json context = nlohmann::json::object();
    nlohmann::json payload = nlohmann::json::object();
    std::size_t encoded_bytes = 0;
  };

  struct ClientState {
    std::deque<EventEnvelope> queue;
    std::size_t queued_bytes = 0;
    bool resync_required = false;
    std::uint64_t dropped_events = 0;
    std::chrono::steady_clock::time_point last_activity =
        std::chrono::steady_clock::now();
  };

  [[nodiscard]] nlohmann::json
  PollEvents(const std::string &client_id,
             std::optional<std::uint64_t> last_sequence,
             bool clear_client) const;
  void PublishEventImpl(std::string event_type, nlohmann::json payload,
                        nlohmann::json context, std::string timestamp,
                        std::uint64_t generation, std::uint64_t run_epoch,
                        std::uint64_t config_revision,
                        std::string config_etag) const;
  [[nodiscard]] bool ReserveEventClient(const std::string &client_id) const;
  void
  TrimRetainedEventsLocked(std::chrono::system_clock::time_point now) const;
  [[nodiscard]] std::string NowIso8601() const;
  [[nodiscard]] nlohmann::json
  EventEnvelopeJson(const EventEnvelope &event) const;

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
  mutable std::deque<
      std::pair<EventEnvelope, std::chrono::system_clock::time_point>>
      retained_events_;
  mutable std::unordered_map<std::string, ClientState> clients_;
  mutable std::uint64_t next_event_sequence_ = 1;
  std::string publisher_epoch_;
  mutable std::size_t retained_event_bytes_ = 0;
  mutable std::uint64_t dropped_events_total_ = 0;
  mutable std::uint64_t coalesced_events_total_ = 0;
  mutable std::uint64_t reconnects_total_ = 0;
  mutable std::size_t per_client_queue_depth_ = 128;
  mutable std::size_t per_client_queue_bytes_ = 2 * 1024 * 1024;
  mutable std::size_t max_retained_event_bytes_ = 8 * 1024 * 1024;
  mutable std::size_t max_retained_events_ = 4096;
  mutable std::chrono::milliseconds event_retention_window_{
      std::chrono::seconds(120)};

  struct PendingEvent {
    std::string event_type;
    nlohmann::json payload;
    nlohmann::json context;
    std::string timestamp;
    std::uint64_t generation = 0;
    std::uint64_t run_epoch = 0;
    std::uint64_t config_revision = 0;
    std::string config_etag;
    std::size_t encoded_bytes = 0;
    bool coalescible = false;
  };
  mutable std::mutex publisher_mutex_;
  mutable std::condition_variable publisher_cv_;
  mutable std::deque<PendingEvent> pending_events_;
  mutable std::size_t pending_event_bytes_ = 0;
  mutable std::jthread publisher_worker_;
  mutable std::jthread snapshot_worker_;
  mutable bool publisher_stopping_ = false;
  mutable bool publisher_paused_for_testing_ = false;
};

} // namespace graph::dashboard
