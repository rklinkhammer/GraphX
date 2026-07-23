// SPDX-License-Identifier: MIT

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace graph::dashboard {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

struct EmbeddedDashboardServer::ServerState {
  struct Worker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };
  asio::io_context context;
  std::unique_ptr<tcp::acceptor> acceptor;
  std::mutex workers_mutex;
  std::vector<Worker> workers;
  std::atomic<std::size_t> active_connections{0};
  std::atomic<std::size_t> active_websocket_clients{0};
  std::atomic<std::uint64_t> websocket_pongs_received{0};
  std::atomic<std::uint64_t> websocket_idle_closes{0};
  std::atomic<std::uint64_t> websocket_protocol_failures{0};
  std::atomic<std::uint64_t> websocket_rejected_upgrades{0};
  std::atomic<std::uint64_t> websocket_replayed_events{0};
  std::atomic<std::uint64_t> websocket_resync_requests{0};
  std::atomic<std::uint64_t> websocket_queue_overflows{0};
  std::atomic<std::uint64_t> websocket_close_normal{0};
  std::atomic<std::uint64_t> websocket_close_protocol{0};
  std::atomic<std::uint64_t> websocket_close_unsupported{0};
  std::atomic<std::uint64_t> websocket_close_invalid_utf8{0};
  std::atomic<std::uint64_t> websocket_close_too_big{0};
  std::atomic<std::uint64_t> websocket_close_policy{0};
  std::atomic<std::uint64_t> websocket_close_going_away{0};
  std::atomic<std::uint64_t> websocket_close_internal{0};
  std::mutex streams_mutex;
  std::unordered_set<tcp::socket::native_handle_type> active_socket_handles;
  struct HandlerJob {
    ApiHandler handler;
    ApiRequest request;
    std::chrono::steady_clock::time_point deadline;
    std::promise<std::optional<ApiResponse>> completion;
    std::stop_source stop_source;
  };
  std::mutex handler_mutex;
  std::condition_variable_any handler_cv;
  std::deque<std::shared_ptr<HandlerJob>> handler_queue;
  std::vector<std::shared_ptr<HandlerJob>> active_handler_jobs;
  std::vector<std::jthread> handler_workers;
  bool handler_stopping = false;
};

namespace {

bool IsValidWebSocketKey(std::string_view value) {
  // RFC 6455 requires a base64-encoded 16-byte nonce. A canonical encoding is
  // therefore 24 characters, has two padding characters, and has only two
  // significant bits in its final base64 digit.
  if (value.size() != 24 || !value.ends_with("=="))
    return false;
  for (const auto character : value.substr(0, 21)) {
    const auto byte = static_cast<unsigned char>(character);
    if (!std::isalnum(byte) && character != '+' && character != '/')
      return false;
  }
  return value[21] == 'A' || value[21] == 'Q' || value[21] == 'g' ||
         value[21] == 'w';
}

std::optional<std::string> ParseMediaType(std::string_view value) {
  const auto trim = [](std::string_view part) {
    while (!part.empty() &&
           std::isspace(static_cast<unsigned char>(part.front())))
      part.remove_prefix(1);
    while (!part.empty() &&
           std::isspace(static_cast<unsigned char>(part.back())))
      part.remove_suffix(1);
    return part;
  };
  const auto semicolon = value.find(';');
  auto base = trim(value.substr(0, semicolon));
  if (base.empty())
    return std::nullopt;
  std::size_t cursor = semicolon;
  while (cursor != std::string_view::npos) {
    const auto next = value.find(';', cursor + 1);
    const auto parameter = trim(value.substr(
        cursor + 1, next == std::string_view::npos ? next : next - cursor - 1));
    const auto equals = parameter.find('=');
    if (parameter.empty() || equals == std::string_view::npos || equals == 0 ||
        equals + 1 == parameter.size())
      return std::nullopt;
    cursor = next;
  }
  return std::string(base);
}

struct AssetReadResult {
  std::optional<std::string> body;
  bool too_large = false;
};

AssetReadResult
ReadAssetWithoutFollowingLinks(const std::filesystem::path &root,
                               const std::filesystem::path &relative,
                               std::size_t maximum_bytes) {
#if defined(_WIN32)
  const auto path = root / relative;
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size > maximum_bytes) {
    return {.too_large = !error};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    return {};
  }
  std::ostringstream stream;
  stream << input.rdbuf();
  return {.body = stream.str()};
#else
  struct ScopedFd {
    int value = -1;
    explicit ScopedFd(int descriptor) : value(descriptor) {}
    ~ScopedFd() {
      if (value >= 0) {
        ::close(value);
      }
    }
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
  } directory{
      ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  if (directory.value < 0) {
    return {};
  }

  std::vector<std::filesystem::path> components;
  for (const auto &component : relative) {
    if (component.empty() || component == "." || component == "..") {
      return {};
    }
    components.push_back(component);
  }
  if (components.empty()) {
    return {};
  }
  for (std::size_t index = 0; index + 1 < components.size(); ++index) {
    const int next = ::openat(directory.value, components[index].c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (next < 0) {
      return {};
    }
    ::close(directory.value);
    directory.value = next;
  }
  ScopedFd file{::openat(directory.value, components.back().c_str(),
                         O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
  if (file.value < 0) {
    return {};
  }
  struct stat metadata{};
  if (::fstat(file.value, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_size < 0) {
    return {};
  }
  if (static_cast<std::uintmax_t>(metadata.st_size) > maximum_bytes) {
    return {.too_large = true};
  }
  std::string body(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t offset = 0;
  while (offset < body.size()) {
    const auto count =
        ::read(file.value, body.data() + offset, body.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      return {};
    }
    offset += static_cast<std::size_t>(count);
  }
  return {.body = std::move(body)};
#endif
}

std::string JsonResponse(const nlohmann::json &json) { return json.dump(); }

nlohmann::json
RuntimeStatusJson(const GraphRuntimeSession::StatusSnapshot &snapshot) {
  return nlohmann::json{
      {"schema", "graphx.dashboard.runtime_status.v1"},
      {"lifecycle_state", GraphRuntimeSession::StateToString(snapshot.state)},
      {"ready", snapshot.ready},
      {"rebuild_allowed", snapshot.rebuild_allowed},
      {"rebuild_blocked", snapshot.rebuild_blocked},
      {"active_generation", snapshot.active_generation},
      {"active_run_epoch", snapshot.active_run_epoch},
      {"rebuild_attempts", snapshot.rebuild_attempts},
      {"successful_rebuilds", snapshot.successful_rebuilds},
      {"active_config_revision", snapshot.active_config_revision},
      {"active_config_etag", snapshot.active_config_etag},
      {"stop_requested", snapshot.stop_requested},
      {"started_at", snapshot.started_at.empty()
                         ? nlohmann::json(nullptr)
                         : nlohmann::json(snapshot.started_at)},
      {"terminal_at", snapshot.terminal_at.empty()
                          ? nlohmann::json(nullptr)
                          : nlohmann::json(snapshot.terminal_at)},
      {"terminal_result",
       snapshot.terminal_generation == 0
           ? nlohmann::json(nullptr)
           : nlohmann::json{{"generation", snapshot.terminal_generation},
                            {"code", snapshot.terminal_result_code},
                            {"message", snapshot.terminal_result_message}}},
      {"last_error",
       snapshot.last_error_code.empty()
           ? nlohmann::json(nullptr)
           : nlohmann::json{{"code", snapshot.last_error_code},
                            {"message", snapshot.last_error_message}}}};
}

std::string ErrorBody(int status_code, std::string code, std::string message,
                      std::string pointer = {}) {
  nlohmann::json error{{"type", "urn:graphx:dashboard:problem:" + code},
                       {"title", code},
                       {"detail", message},
                       {"status", status_code},
                       {"schema", "graphx.dashboard.error.v1"},
                       {"code", std::move(code)},
                       {"message", message},
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

std::optional<std::uint64_t> ParseUint64(const std::string &value) {
  if (value.empty()) {
    return std::nullopt;
  }
  std::uint64_t parsed = 0;
  const auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc{} || ptr != value.data() + value.size()) {
    return std::nullopt;
  }
  return parsed;
}

bool ParseBool(const std::string &value) {
  return value == "1" || value == "true" || value == "yes";
}

std::optional<std::string> PercentDecodePath(const std::string &value) {
  std::string decoded;
  decoded.reserve(value.size());
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%') {
      decoded.push_back(value[i]);
      continue;
    }
    if (i + 2 >= value.size())
      return std::nullopt;
    const int high = hex(value[i + 1]);
    const int low = hex(value[i + 2]);
    if (high < 0 || low < 0)
      return std::nullopt;
    const char byte = static_cast<char>((high << 4) | low);
    if (byte == '\0' || byte == '\\')
      return std::nullopt;
    decoded.push_back(byte);
    i += 2;
  }
  return decoded;
}

bool IsContainedPath(const std::filesystem::path &root,
                     const std::filesystem::path &candidate) {
  auto root_it = root.begin();
  auto candidate_it = candidate.begin();
  for (; root_it != root.end(); ++root_it, ++candidate_it) {
    if (candidate_it == candidate.end() || *root_it != *candidate_it)
      return false;
  }
  return true;
}

std::optional<std::string> AllowedMethodsFor(const std::string &path) {
  if (path == "/healthz" || path == "/readyz" || path == "/api/v1/version" ||
      path == "/api/v1/fhss/events" || path == "/api/v1/fhss/events/stream" ||
      path == "/api/v1/fhss/snapshot" || path == "/api/v1/fhss/graph" ||
      path == "/api/v1/fhss/config/authoritative" ||
      path == "/api/v1/fhss/config/effective" ||
      path == "/api/v1/fhss/config/provenance" ||
      path == "/api/v1/fhss/graph/receiver-minimal" ||
      path == "/api/v1/fhss/config/derived-paths" ||
      path == "/api/v1/fhss/config/value" || path == "/api/v1/fhss/metrics" ||
      path == "/api/v1/fhss/metrics/edges" ||
      path == "/api/v1/fhss/diagnostics" || path == "/api/v1/fhss/status" ||
      path == "/api/v1/fhss/visualization" ||
      path == "/api/v1/fhss/expected-truth" ||
      path == "/api/v1/fhss/observations" ||
      path == "/api/v1/fhss/comparison" || path == "/api/v1/fhss/spectrum" ||
      path == "/api/v1/fhss/observation-provenance" ||
      path == "/api/v1/fhss/observation-history" ||
      StartsWith(path, "/api/v1/fhss/nodes/"))
    return "GET";
  if (path == "/api/v1/fhss/config")
    return "GET, PATCH";
  if (path == "/api/v1/fhss/config/validate" ||
      path == "/api/v1/fhss/config/rebuild" ||
      path == "/api/v1/fhss/config/discard" ||
      path == "/api/v1/fhss/config/undo" ||
      path == "/api/v1/fhss/config/export" ||
      path == "/api/v1/fhss/commands/start" ||
      path == "/api/v1/fhss/commands/stop")
    return "POST";
  if (StartsWith(path, "/api/v1/fhss/operations/")) {
    return path.ends_with("/cancel") ? "POST" : "GET, DELETE";
  }
  return std::nullopt;
}

bool MethodAllowed(const std::string &method, const std::string &allow) {
  std::size_t begin = 0;
  while (begin < allow.size()) {
    auto end = allow.find(',', begin);
    auto item =
        allow.substr(begin, end == std::string::npos ? end : end - begin);
    while (!item.empty() && item.front() == ' ')
      item.erase(item.begin());
    if (item == method)
      return true;
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return false;
}

std::optional<std::string>
ValidateJsonStructure(const std::string &body,
                      const EmbeddedDashboardServer::Options &options) {
  bool limit_exceeded = false;
  std::size_t members = 0;
  std::vector<std::unordered_set<std::string>> object_keys(
      options.max_json_depth + 1);
  const auto callback = [&](int depth, nlohmann::json::parse_event_t event,
                            nlohmann::json &parsed) {
    if (depth < 0 || static_cast<std::size_t>(depth) > options.max_json_depth) {
      limit_exceeded = true;
      return false;
    }
    if (event == nlohmann::json::parse_event_t::object_start) {
      object_keys[static_cast<std::size_t>(depth)].clear();
    }
    if (event == nlohmann::json::parse_event_t::key ||
        event == nlohmann::json::parse_event_t::value) {
      if (++members > options.max_json_members) {
        limit_exceeded = true;
        return false;
      }
      if (parsed.is_string() && parsed.get_ref<const std::string &>().size() >
                                    options.max_json_string_bytes) {
        limit_exceeded = true;
        return false;
      }
      if (event == nlohmann::json::parse_event_t::key && parsed.is_string() &&
          !object_keys[static_cast<std::size_t>(depth)]
               .insert(parsed.get_ref<const std::string &>())
               .second) {
        limit_exceeded = true;
        return false;
      }
      if (parsed.is_number()) {
        const double number = parsed.get<double>();
        if (!std::isfinite(number) ||
            std::abs(number) > options.max_json_number_magnitude) {
          limit_exceeded = true;
          return false;
        }
      }
    }
    return true;
  };
  const auto parsed = nlohmann::json::parse(body, callback, false);
  if (limit_exceeded)
    return "JSON structural limit exceeded";
  if (parsed.is_discarded())
    return "request body must be valid JSON";
  return std::nullopt;
}

} // namespace

EmbeddedDashboardServer::EmbeddedDashboardServer(
    Options options,
    std::shared_ptr<GraphConfigurationService> configuration_service,
    std::shared_ptr<GraphRuntimeSession> runtime_session,
    std::shared_ptr<GraphSnapshotCollector> snapshot_collector)
    : options_(std::move(options)),
      configuration_service_(std::move(configuration_service)),
      runtime_session_(std::move(runtime_session)),
      snapshot_collector_(std::move(snapshot_collector)) {
  std::array<std::uint64_t, 2> epoch{};
  std::random_device random;
  for (auto &part : epoch)
    part = (static_cast<std::uint64_t>(random()) << 32U) ^ random();
  std::ostringstream encoded_epoch;
  encoded_epoch << std::hex << std::setfill('0') << std::setw(16) << epoch[0]
                << std::setw(16) << epoch[1];
  publisher_epoch_ = encoded_epoch.str();
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

  {
    std::scoped_lock lock(event_mutex_);
    per_client_queue_depth_ = options_.max_websocket_queue_events;
    per_client_queue_bytes_ = options_.max_websocket_queue_bytes;
    max_retained_event_bytes_ = options_.max_retained_event_bytes;
    max_retained_events_ = options_.max_retained_events;
    event_retention_window_ = options_.event_retention_window;
  }

  try {
    server_state_ = std::make_unique<ServerState>();
    const auto address = asio::ip::make_address(options_.host);
    tcp::endpoint endpoint(address, options_.port);
    server_state_->acceptor =
        std::make_unique<tcp::acceptor>(server_state_->context);
    server_state_->acceptor->open(endpoint.protocol());
    server_state_->acceptor->set_option(asio::socket_base::reuse_address(true));
    if (address.is_v6()) {
      server_state_->acceptor->set_option(asio::ip::v6_only(true));
    }
    server_state_->acceptor->bind(endpoint);
    server_state_->acceptor->listen(static_cast<int>(
        std::min<std::size_t>(options_.max_concurrent_connections, 128)));
    server_state_->acceptor->non_blocking(true);
    const auto local = server_state_->acceptor->local_endpoint();
    bound_port_ = local.port();
    bound_host_ = local.address().to_string();
    if (options_.application_api_handler) {
      const auto worker_count =
          std::min<std::size_t>(options_.max_concurrent_connections, 4);
      for (std::size_t index = 0; index < worker_count; ++index) {
        server_state_->handler_workers.emplace_back(
            [state = server_state_.get()](std::stop_token pool_stop) {
              for (;;) {
                std::shared_ptr<ServerState::HandlerJob> job;
                {
                  std::unique_lock lock(state->handler_mutex);
                  state->handler_cv.wait(lock, pool_stop, [&] {
                    return state->handler_stopping ||
                           !state->handler_queue.empty();
                  });
                  if ((pool_stop.stop_requested() || state->handler_stopping) &&
                      state->handler_queue.empty()) {
                    return;
                  }
                  job = state->handler_queue.front();
                  state->handler_queue.pop_front();
                  state->active_handler_jobs.push_back(job);
                }
                try {
                  job->completion.set_value(job->handler(
                      job->request,
                      ApiContext{.deadline = job->deadline,
                                 .stop_token = job->stop_source.get_token()}));
                } catch (...) {
                  job->completion.set_exception(std::current_exception());
                }
                {
                  std::scoped_lock lock(state->handler_mutex);
                  std::erase(state->active_handler_jobs, job);
                }
              }
            });
      }
    }
  } catch (const boost::system::system_error &error) {
    last_error_ = std::string{"failed to bind dashboard socket: "} +
                  error.code().message();
    server_state_.reset();
    return false;
  } catch (const std::exception &) {
    last_error_ = "failed to initialize dashboard socket";
    server_state_.reset();
    return false;
  }

  runtime_session_->MarkReady();
  {
    std::scoped_lock lock(publisher_mutex_);
    publisher_stopping_ = false;
    publisher_paused_for_testing_ = false;
    pending_events_.clear();
    pending_event_bytes_ = 0;
  }
  publisher_worker_ = std::jthread([this](std::stop_token stop) {
    std::deque<std::chrono::steady_clock::time_point> publication_times;
    for (;;) {
      PendingEvent event;
      {
        std::unique_lock lock(publisher_mutex_);
        publisher_cv_.wait(lock, [&] {
          return stop.stop_requested() || publisher_stopping_ ||
                 (!publisher_paused_for_testing_ && !pending_events_.empty());
        });
        if ((stop.stop_requested() || publisher_stopping_) &&
            pending_events_.empty())
          return;
        event = std::move(pending_events_.front());
        pending_event_bytes_ -= event.encoded_bytes;
        pending_events_.pop_front();
        publisher_cv_.notify_all();
      }
      const auto now = std::chrono::steady_clock::now();
      while (!publication_times.empty() &&
             now - publication_times.front() >= std::chrono::seconds(1))
        publication_times.pop_front();
      if (publication_times.size() >=
          options_.max_websocket_events_per_second) {
        if (event.coalescible) {
          std::scoped_lock lock(event_mutex_);
          ++coalesced_events_total_;
          server_state_->websocket_queue_overflows.fetch_add(1);
          for (auto &[id, client] : clients_) {
            (void)id;
            client.resync_required = true;
            ++client.dropped_events;
            ++dropped_events_total_;
          }
          continue;
        }
        const auto delay =
            std::chrono::seconds(1) - (now - publication_times.front());
        if (delay > std::chrono::steady_clock::duration::zero())
          std::this_thread::sleep_for(delay);
        publication_times.clear();
      }
      publication_times.push_back(std::chrono::steady_clock::now());
      PublishEventImpl(std::move(event.event_type), std::move(event.payload),
                       std::move(event.context), std::move(event.timestamp),
                       event.generation, event.run_epoch, event.config_revision,
                       std::move(event.config_etag));
    }
  });
  running_.store(true);
  if (options_.application_api_handler)
    snapshot_worker_ = std::jthread([this](std::stop_token stop) {
      std::string previous_runtime;
      std::string previous_metrics;
      std::string previous_diagnostics;
      while (!stop.stop_requested() && running_.load()) {
        try {
          auto runtime = RuntimeStatusJson(runtime_session_->SnapshotStatus());
          auto metrics = snapshot_collector_->GetMetricsSnapshot();
          auto diagnostics = snapshot_collector_->GetDiagnosticsSnapshot();
          const auto runtime_key = runtime.dump();
          const auto metrics_key = metrics.dump();
          const auto diagnostics_key = diagnostics.dump();
          if (runtime_key != previous_runtime) {
            PublishEvent("runtime_status", std::move(runtime),
                         {{"semantic_class", "runtime"}});
            previous_runtime = runtime_key;
          }
          if (metrics_key != previous_metrics) {
            PublishEvent("metrics", std::move(metrics),
                         {{"semantic_class", "metrics"}});
            previous_metrics = metrics_key;
          }
          if (diagnostics_key != previous_diagnostics) {
            PublishEvent("diagnostics", std::move(diagnostics),
                         {{"semantic_class", "diagnostics"}});
            previous_diagnostics = diagnostics_key;
          }
        } catch (const std::exception &) {
          // Snapshot endpoints retain their own truthful unavailable state. A
          // transient collection failure must not terminate transport service.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
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

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  if (server_state_ && server_state_->acceptor) {
    boost::system::error_code ignored;
    server_state_->acceptor->close(ignored);
  }

  if (server_state_) {
    {
      std::scoped_lock lock(server_state_->streams_mutex);
      for (const auto handle : server_state_->active_socket_handles) {
#if defined(_WIN32)
        ::shutdown(handle, SD_BOTH);
#else
        ::shutdown(handle, SHUT_RDWR);
#endif
      }
      server_state_->active_socket_handles.clear();
    }
    {
      std::scoped_lock lock(server_state_->handler_mutex);
      server_state_->handler_stopping = true;
      for (const auto &job : server_state_->handler_queue)
        job->stop_source.request_stop();
      for (const auto &job : server_state_->active_handler_jobs)
        job->stop_source.request_stop();
    }
    for (auto &worker : server_state_->handler_workers)
      worker.request_stop();
    server_state_->handler_cv.notify_all();
    server_state_->handler_workers.clear();
    std::scoped_lock lock(server_state_->workers_mutex);
    for (auto &worker : server_state_->workers) {
      if (worker.thread.joinable()) {
        worker.thread.join();
      }
    }
    server_state_->workers.clear();
    snapshot_worker_.request_stop();
    if (snapshot_worker_.joinable())
      snapshot_worker_.join();
    {
      std::scoped_lock publisher_lock(publisher_mutex_);
      publisher_stopping_ = true;
    }
    publisher_worker_.request_stop();
    publisher_cv_.notify_all();
    if (publisher_worker_.joinable())
      publisher_worker_.join();
    server_state_.reset();
  }

  if (runtime_session_) {
    runtime_session_->MarkDead();
  }

  {
    std::scoped_lock lock(event_mutex_);
    clients_.clear();
    retained_events_.clear();
    retained_event_bytes_ = 0;
  }
}

bool EmbeddedDashboardServer::IsRunning() const { return running_.load(); }

std::uint16_t EmbeddedDashboardServer::BoundPort() const { return bound_port_; }

const std::string &EmbeddedDashboardServer::BoundHost() const {
  return bound_host_;
}

const std::string &EmbeddedDashboardServer::LastError() const {
  return last_error_;
}

void EmbeddedDashboardServer::PublishEvent(std::string event_type,
                                           nlohmann::json payload,
                                           nlohmann::json context) const {
  PendingEvent pending{.event_type = std::move(event_type),
                       .payload = std::move(payload),
                       .context = context.is_object()
                                      ? std::move(context)
                                      : nlohmann::json::object(),
                       .timestamp = NowIso8601()};
  if (runtime_session_) {
    const auto status = runtime_session_->SnapshotStatus();
    pending.generation = status.active_generation;
    pending.run_epoch = status.active_run_epoch;
  }
  if (configuration_service_) {
    pending.config_revision = configuration_service_->ConfigRevision();
    pending.config_etag = configuration_service_->ETag();
  }
  pending.coalescible =
      pending.event_type == "metrics" || pending.event_type == "diagnostics";
  const auto payload_bytes = pending.payload.dump().size();
  const auto context_bytes = pending.context.dump().size();
  const auto checked_add = [&](std::size_t value) {
    if (value > std::numeric_limits<std::size_t>::max() - pending.encoded_bytes)
      return false;
    pending.encoded_bytes += value;
    return true;
  };
  if (!checked_add(payload_bytes) || !checked_add(context_bytes) ||
      !checked_add(pending.event_type.size()) ||
      !checked_add(pending.timestamp.size()) ||
      !checked_add(pending.config_etag.size())) {
    if (server_state_)
      server_state_->websocket_queue_overflows.fetch_add(1);
    return;
  }

  {
    std::scoped_lock lock(publisher_mutex_);
    if (publisher_stopping_ || !server_state_)
      return;
    const auto record_rejection = [&](bool critical) {
      server_state_->websocket_queue_overflows.fetch_add(1);
      std::scoped_lock event_lock(event_mutex_);
      if (!critical) {
        ++coalesced_events_total_;
        return;
      }
      for (auto &[id, client] : clients_) {
        (void)id;
        client.resync_required = true;
        ++client.dropped_events;
        ++dropped_events_total_;
      }
    };
    const auto fits = [&] {
      return pending_events_.size() < options_.max_publisher_ingress_events &&
             pending.encoded_bytes <=
                 options_.max_publisher_ingress_bytes -
                     std::min(pending_event_bytes_,
                              options_.max_publisher_ingress_bytes);
    };
    if (pending.encoded_bytes > options_.max_publisher_ingress_bytes) {
      record_rejection(!pending.coalescible);
      return;
    }
    if (!fits()) {
      const auto replace = std::ranges::find_if(
          pending_events_, [&](const PendingEvent &queued) {
            return pending.coalescible &&
                   queued.event_type == pending.event_type;
          });
      if (replace != pending_events_.end()) {
        const auto bytes_without_replaced =
            pending_event_bytes_ - replace->encoded_bytes;
        if (pending.encoded_bytes <=
            options_.max_publisher_ingress_bytes - bytes_without_replaced) {
          pending_event_bytes_ = bytes_without_replaced;
          *replace = std::move(pending);
          pending_event_bytes_ += replace->encoded_bytes;
          std::scoped_lock event_lock(event_mutex_);
          ++coalesced_events_total_;
          server_state_->websocket_queue_overflows.fetch_add(1);
        } else {
          record_rejection(false);
        }
        return;
      }
      if (!pending.coalescible) {
        while (!fits()) {
          const auto disposable = std::ranges::find_if(
              pending_events_,
              [](const PendingEvent &queued) { return queued.coalescible; });
          if (disposable == pending_events_.end())
            break;
          pending_event_bytes_ -= disposable->encoded_bytes;
          pending_events_.erase(disposable);
          server_state_->websocket_queue_overflows.fetch_add(1);
          std::scoped_lock event_lock(event_mutex_);
          ++coalesced_events_total_;
        }
      }
      if (!fits()) {
        // Admission is deliberately non-blocking. Already accepted critical
        // transitions retain FIFO order; when they alone consume the bounded
        // queue, reject the newest transition and force clients to obtain a
        // coherent snapshot rather than publishing out of order.
        record_rejection(!pending.coalescible);
        return;
      }
    }
    pending_event_bytes_ += pending.encoded_bytes;
    pending_events_.push_back(std::move(pending));
  }
  publisher_cv_.notify_one();
}

void EmbeddedDashboardServer::ExpireRetainedEventsForTesting() {
  std::scoped_lock lock(event_mutex_);
  retained_events_.clear();
  retained_event_bytes_ = 0;
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

void EmbeddedDashboardServer::SetPublisherPausedForTesting(bool paused) {
  {
    std::scoped_lock lock(publisher_mutex_);
    publisher_paused_for_testing_ = paused;
  }
  publisher_cv_.notify_all();
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
  boost::system::error_code address_error;
  const auto address = asio::ip::make_address(options_.host, address_error);
  if (address_error || !address.is_loopback()) {
    last_error_ =
        "dashboard host must be an explicit IPv4 or IPv6 loopback address";
    return false;
  }
  if (options_.max_header_bytes < 1024 || options_.max_body_bytes == 0 ||
      options_.max_response_bytes == 0 ||
      options_.max_concurrent_connections == 0 ||
      options_.max_json_depth == 0 || options_.max_json_members == 0 ||
      options_.max_json_string_bytes == 0 ||
      !std::isfinite(options_.max_json_number_magnitude) ||
      options_.max_json_number_magnitude <= 0.0 ||
      options_.max_concurrent_connections > 128 ||
      options_.max_websocket_clients == 0 ||
      options_.max_websocket_frame_bytes < 125 ||
      options_.max_websocket_message_bytes <
          options_.max_websocket_frame_bytes ||
      options_.max_websocket_fragments_per_message == 0 ||
      options_.max_websocket_commands_per_second == 0 ||
      options_.max_websocket_events_per_second == 0 ||
      options_.max_websocket_replay_events == 0 ||
      options_.max_websocket_replay_bytes <
          options_.max_websocket_frame_bytes ||
      options_.max_websocket_queue_events == 0 ||
      options_.max_websocket_queue_bytes <
          options_.max_websocket_message_bytes ||
      options_.max_publisher_ingress_events == 0 ||
      options_.max_publisher_ingress_bytes <
          options_.max_websocket_message_bytes ||
      options_.max_retained_events == 0 ||
      options_.max_retained_event_bytes <
          options_.max_websocket_message_bytes ||
      options_.websocket_idle_timeout.count() <= 0 ||
      options_.websocket_heartbeat_interval.count() <= 0 ||
      options_.websocket_max_lifetime.count() <= 0 ||
      options_.websocket_close_timeout.count() <= 0 ||
      options_.websocket_client_state_ttl.count() <= 0 ||
      options_.event_retention_window.count() <= 0 ||
      options_.idle_timeout.count() <= 0 ||
      options_.read_timeout.count() <= 0 ||
      options_.write_timeout.count() <= 0 ||
      options_.total_request_timeout.count() <= 0) {
    last_error_ = "invalid dashboard resource limits";
    return false;
  }
  if (options_.application_api_handler &&
      (!options_.application_api_handler->handler ||
       !options_.application_api_handler->cooperative_cancellation ||
       options_.application_api_handler->maximum_checkpoint_latency.count() <=
           0 ||
       options_.application_api_handler->maximum_checkpoint_latency >
           options_.total_request_timeout)) {
    last_error_ = "application handler must declare cooperative cancellation "
                  "and a bounded checkpoint latency";
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

  if (!std::filesystem::exists(options_.asset_directory / "index.html",
                               error)) {
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
    boost::system::error_code accept_error;
    tcp::socket socket(server_state_->context);
    server_state_->acceptor->accept(socket, accept_error);
    if (accept_error) {
      if (!running_.load())
        return;
      if (accept_error == asio::error::would_block ||
          accept_error == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      continue;
    }
    if (server_state_->active_connections.load() >=
        options_.max_concurrent_connections) {
      boost::system::error_code ignored;
      socket.close(ignored);
      continue;
    }
    server_state_->active_connections.fetch_add(1);
    std::scoped_lock lock(server_state_->workers_mutex);
    for (auto worker = server_state_->workers.begin();
         worker != server_state_->workers.end();) {
      if (worker->done->load()) {
        if (worker->thread.joinable())
          worker->thread.join();
        worker = server_state_->workers.erase(worker);
      } else {
        ++worker;
      }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    server_state_->workers.push_back(ServerState::Worker{
        .thread = std::thread([this, socket = std::move(socket),
                               done]() mutable {
          const auto request_started = std::chrono::steady_clock::now();
          const auto request_deadline =
              request_started + options_.total_request_timeout;
          asio::io_context request_context;
          const auto protocol = socket.local_endpoint().protocol();
          tcp::socket request_socket(request_context, protocol,
                                     socket.release());
          auto stream =
              std::make_shared<beast::tcp_stream>(std::move(request_socket));
          const auto native_socket = stream->socket().native_handle();
          {
            std::scoped_lock lock(server_state_->streams_mutex);
            server_state_->active_socket_handles.insert(native_socket);
          }
          const auto socket_registration =
              std::unique_ptr<void, std::function<void(void *)>>(
                  reinterpret_cast<void *>(1), [this, native_socket](void *) {
                    std::scoped_lock lock(server_state_->streams_mutex);
                    server_state_->active_socket_handles.erase(native_socket);
                  });
          beast::flat_buffer buffer(options_.max_header_bytes +
                                    options_.max_body_bytes);
          http::request_parser<http::string_body> parser;
          parser.header_limit(options_.max_header_bytes);
          parser.body_limit(options_.max_body_bytes);
          boost::system::error_code read_error;
          enum class ReadTimeoutKind { None, Idle, Read, Total };
          ReadTimeoutKind timeout_kind = ReadTimeoutKind::None;
          bool read_finished = false;
          std::size_t idle_generation = 0;
          asio::steady_timer idle_timer(request_context);
          asio::steady_timer absolute_read_timer(request_context);
          asio::steady_timer total_timer(request_context);

          const auto expire_read = [&](ReadTimeoutKind kind) {
            if (read_finished || timeout_kind != ReadTimeoutKind::None)
              return;
            timeout_kind = kind;
            boost::system::error_code ignored;
            stream->socket().cancel(ignored);
          };
          std::function<void()> arm_idle_timer;
          arm_idle_timer = [&] {
            const auto generation = ++idle_generation;
            idle_timer.expires_after(options_.idle_timeout);
            idle_timer.async_wait(
                [&, generation](boost::system::error_code error) {
                  if (!error && generation == idle_generation)
                    expire_read(ReadTimeoutKind::Idle);
                });
          };
          absolute_read_timer.expires_after(options_.read_timeout);
          absolute_read_timer.async_wait([&](boost::system::error_code error) {
            if (!error)
              expire_read(ReadTimeoutKind::Read);
          });
          total_timer.expires_at(request_deadline);
          total_timer.async_wait([&](boost::system::error_code error) {
            if (!error)
              expire_read(ReadTimeoutKind::Total);
          });

          std::function<void()> read_some;
          read_some = [&] {
            const auto available = buffer.max_size() - buffer.size();
            if (available == 0) {
              read_error = parser.is_header_done() ? http::error::body_limit
                                                   : http::error::header_limit;
              read_finished = true;
              ++idle_generation;
              idle_timer.cancel();
              absolute_read_timer.cancel();
              total_timer.cancel();
              return;
            }
            arm_idle_timer();
            stream->async_read_some(
                buffer.prepare(std::min<std::size_t>(4096, available)),
                [&](boost::system::error_code error, std::size_t bytes) {
                  if (error) {
                    read_error = error;
                    read_finished = true;
                  } else {
                    buffer.commit(bytes);
                    boost::system::error_code parse_error;
                    while (!parser.is_done() && buffer.size() != 0) {
                      const auto consumed =
                          parser.put(buffer.data(), parse_error);
                      buffer.consume(consumed);
                      if (parse_error == http::error::need_more) {
                        parse_error.clear();
                        break;
                      }
                      if (parse_error || consumed == 0)
                        break;
                    }
                    if (parse_error) {
                      read_error = parse_error;
                      read_finished = true;
                    } else if (parser.is_done()) {
                      read_finished = true;
                    } else {
                      idle_timer.cancel();
                      read_some();
                      return;
                    }
                  }
                  ++idle_generation;
                  idle_timer.cancel();
                  absolute_read_timer.cancel();
                  total_timer.cancel();
                });
          };
          read_some();
          request_context.run();
          if (timeout_kind != ReadTimeoutKind::None)
            read_error = beast::error::timeout;

          Response response;
          if (read_error) {
            int status = 400;
            std::string code = "bad_request";
            if (read_error == http::error::header_limit) {
              status = 431;
              code = "headers_too_large";
            }
            if (read_error == http::error::body_limit) {
              status = 413;
              code = "body_too_large";
            }
            if (read_error == beast::error::timeout) {
              status = 408;
              if (timeout_kind == ReadTimeoutKind::Idle)
                code = "idle_timeout";
              if (timeout_kind == ReadTimeoutKind::Read)
                code = "read_timeout";
              if (timeout_kind == ReadTimeoutKind::Total)
                code = "total_request_timeout";
            }
            response = {.status_code = status,
                        .content_type = "application/problem+json",
                        .body = ErrorBody(status, code, "request rejected")};
          } else {
            const auto &wire_request = parser.get();
            Request request;
            request.method = std::string(wire_request.method_string());
            request.target = std::string(wire_request.target());
            request.path = GetPathWithoutQuery(request.target);
            const auto query = request.target.find('?');
            if (query != std::string::npos)
              request.query = request.target.substr(query + 1);
            request.body = wire_request.body();
            std::size_t host_count = 0;
            std::size_t origin_count = 0;
            std::size_t subprotocol_count = 0;
            std::size_t websocket_key_count = 0;
            for (const auto &field : wire_request) {
              if (field.name() == http::field::host)
                ++host_count;
              auto name = std::string(field.name_string());
              std::transform(name.begin(), name.end(), name.begin(),
                             [](unsigned char value) {
                               return static_cast<char>(std::tolower(value));
                             });
              if (name == "origin")
                ++origin_count;
              else if (name == "sec-websocket-protocol")
                ++subprotocol_count;
              else if (name == "sec-websocket-key")
                ++websocket_key_count;
              // Preserve the first value. Duplicate security-sensitive fields
              // are rejected below and must never be merged or overwritten.
              request.headers.try_emplace(std::move(name),
                                          std::string(field.value()));
            }
            request.deadline = request_deadline;
            if (wire_request.version() == 11 && host_count != 1) {
              response = {.status_code = 400,
                          .content_type = "application/problem+json",
                          .body = ErrorBody(
                              400, "invalid_host_header",
                              "HTTP/1.1 requires exactly one Host header")};
            } else if (websocket::is_upgrade(wire_request) &&
                       request.path == "/api/v1/fhss/events/stream") {
              const auto origin = request.headers.contains("origin")
                                      ? request.headers.at("origin")
                                      : std::string{};
              const auto host = request.headers.contains("host")
                                    ? request.headers.at("host")
                                    : std::string{};
              const auto expected_authority =
                  (bound_host_.find(':') == std::string::npos
                       ? bound_host_
                       : "[" + bound_host_ + "]") +
                  ":" + std::to_string(bound_port_);
              const auto expected_origin = "http://" + expected_authority;
              bool websocket_available = true;
              try {
                websocket_available = !options_.websocket_availability_probe ||
                                      options_.websocket_availability_probe();
              } catch (...) {
                websocket_available = false;
              }
              const auto websocket_key =
                  request.headers.contains("sec-websocket-key")
                      ? request.headers.at("sec-websocket-key")
                      : std::string{};
              if (websocket_key_count != 1 ||
                  !IsValidWebSocketKey(websocket_key)) {
                server_state_->websocket_rejected_upgrades.fetch_add(1);
                response = {.status_code = 400,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(400, "invalid_websocket_key",
                                              "Sec-WebSocket-Key must encode "
                                              "exactly 16 bytes")};
              } else if (origin_count != 1 || host != expected_authority ||
                         origin != expected_origin) {
                server_state_->websocket_rejected_upgrades.fetch_add(1);
                response = {.status_code = 403,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(403, "websocket_origin_rejected",
                                              "WebSocket Origin must match the "
                                              "local dashboard origin")};
              } else if (subprotocol_count != 0) {
                server_state_->websocket_rejected_upgrades.fetch_add(1);
                response = {.status_code = 400,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(
                                400, "websocket_negotiation_unsupported",
                                "WebSocket subprotocols are not supported")};
              } else if (!websocket_available) {
                server_state_->websocket_rejected_upgrades.fetch_add(1);
                response = {.status_code = 503,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(
                                503, "websocket_temporarily_unavailable",
                                "WebSocket event transport is temporarily "
                                "unavailable")};
              } else if (server_state_->active_websocket_clients.fetch_add(1) >=
                         options_.max_websocket_clients) {
                server_state_->active_websocket_clients.fetch_sub(1);
                server_state_->websocket_rejected_upgrades.fetch_add(1);
                response = {.status_code = 503,
                            .content_type = "application/problem+json",
                            .body =
                                ErrorBody(503, "websocket_client_limit",
                                          "WebSocket client limit reached")};
              } else {
                auto websocket_stream =
                    std::make_shared<websocket::stream<beast::tcp_stream>>(
                        std::move(*stream));
                websocket_stream->read_message_max(
                    options_.max_websocket_message_bytes);
                websocket_stream->set_option(
                    websocket::stream_base::timeout::suggested(
                        beast::role_type::server));
                websocket_stream->set_option(websocket::stream_base::decorator(
                    [](websocket::response_type &upgrade_response) {
                      upgrade_response.set(http::field::server,
                                           "GraphX-FHSS-Dashboard");
                    }));
                boost::system::error_code websocket_error;
                websocket::close_reason requested_close;
                requested_close.code = websocket::close_code::normal;
                requested_close.reason = "normal closure";
                bool event_client_reserved = false;
                const auto reject_websocket = [&](websocket::close_code code,
                                                  std::string_view reason) {
                  requested_close.code = code;
                  requested_close.reason = reason;
                  websocket_error = websocket::error::bad_data_frame;
                  server_state_->websocket_protocol_failures.fetch_add(1);
                };
                websocket_stream->accept(wire_request, websocket_error);
                if (websocket_error)
                  server_state_->websocket_rejected_upgrades.fetch_add(1);
                const auto send_json = [&](const nlohmann::json &document) {
                  const auto encoded = document.dump();
                  if (encoded.size() > options_.max_websocket_message_bytes)
                    return false;
                  websocket_stream->text(true);
                  websocket_stream->write(asio::buffer(encoded),
                                          websocket_error);
                  return !websocket_error;
                };

                std::string websocket_client_id;
                std::optional<std::uint64_t> websocket_last_sequence;
                bool subscribed = false;
                if (!websocket_error) {
                  std::uint64_t hello_latest_sequence = 0;
                  std::uint64_t hello_oldest_sequence = 0;
                  {
                    std::scoped_lock lock(event_mutex_);
                    hello_latest_sequence = next_event_sequence_ - 1;
                    hello_oldest_sequence =
                        retained_events_.empty()
                            ? next_event_sequence_
                            : retained_events_.front().first.sequence;
                  }
                  subscribed = send_json(
                      {{"schema", "graphx.dashboard.websocket_hello.v1"},
                       {"api_version", "v1"},
                       {"publisher_epoch", publisher_epoch_},
                       {"latest_sequence", hello_latest_sequence},
                       {"oldest_available_sequence", hello_oldest_sequence},
                       {"heartbeat_interval_ms",
                        options_.websocket_heartbeat_interval.count()},
                       {"limits",
                        {{"frame_bytes", options_.max_websocket_frame_bytes},
                         {"message_bytes",
                          options_.max_websocket_message_bytes},
                         {"fragments_per_message",
                          options_.max_websocket_fragments_per_message},
                         {"commands_per_second",
                          options_.max_websocket_commands_per_second},
                         {"events_per_second",
                          options_.max_websocket_events_per_second},
                         {"replay_events",
                          options_.max_websocket_replay_events},
                         {"replay_bytes", options_.max_websocket_replay_bytes},
                         {"queue_events", options_.max_websocket_queue_events},
                         {"queue_bytes", options_.max_websocket_queue_bytes},
                         {"idle_timeout_ms",
                          options_.websocket_idle_timeout.count()},
                         {"max_lifetime_ms",
                          options_.websocket_max_lifetime.count()}}}});
                }
                beast::flat_buffer websocket_buffer(
                    options_.max_websocket_message_bytes);
                std::size_t websocket_read_operations = 0;
                bool websocket_read_in_progress = false;
                const auto read_message_bounded = [&](bool nonblocking_probe) {
                  if (!websocket_read_in_progress) {
                    websocket_buffer.clear();
                    websocket_read_operations = 0;
                  }
                  websocket_stream->next_layer().expires_after(std::min(
                      options_.read_timeout, options_.websocket_idle_timeout));
                  do {
                    const auto bytes = websocket_stream->read_some(
                        websocket_buffer,
                        options_.max_websocket_frame_bytes + 1,
                        websocket_error);
                    if (nonblocking_probe &&
                        (websocket_error == asio::error::would_block ||
                         websocket_error == asio::error::try_again)) {
                      websocket_error.clear();
                      websocket_read_in_progress = websocket_buffer.size() != 0;
                      websocket_stream->next_layer().expires_never();
                      return false;
                    }
                    if (websocket_error)
                      return false;
                    if (bytes > options_.max_websocket_frame_bytes ||
                        ++websocket_read_operations >
                            options_.max_websocket_fragments_per_message ||
                        websocket_buffer.size() >
                            options_.max_websocket_message_bytes) {
                      reject_websocket(websocket::close_code::too_big,
                                       "frame or message limit exceeded");
                      return false;
                    }
                  } while (!websocket_stream->is_message_done());
                  websocket_read_in_progress = false;
                  websocket_stream->next_layer().expires_never();
                  if (!websocket_stream->got_text()) {
                    reject_websocket(websocket::close_code::unknown_data,
                                     "text commands required");
                    return false;
                  }
                  return true;
                };
                if (subscribed) {
                  if (read_message_bounded(false)) {
                    try {
                      const auto subscription = nlohmann::json::parse(
                          beast::buffers_to_string(websocket_buffer.data()),
                          nullptr, false);
                      const auto subscription_fields_valid =
                          subscription.is_object() &&
                          std::ranges::all_of(
                              subscription.items(), [](const auto &item) {
                                return item.key() == "action" ||
                                       item.key() == "client_id" ||
                                       item.key() == "publisher_epoch" ||
                                       item.key() == "last_sequence";
                              });
                      if (subscription_fields_valid &&
                          subscription.size() >= 2 &&
                          subscription.size() <= 4 &&
                          subscription.contains("action") &&
                          subscription.at("action").is_string() &&
                          subscription.at("action").get<std::string>() ==
                              "subscribe" &&
                          subscription.contains("client_id") &&
                          subscription.at("client_id").is_string() &&
                          (!subscription.contains("last_sequence") ||
                           subscription.at("last_sequence")
                               .is_number_unsigned()) &&
                          (!subscription.contains("publisher_epoch") ||
                           subscription.at("publisher_epoch").is_string())) {
                        websocket_client_id =
                            subscription.at("client_id").get<std::string>();
                        subscribed =
                            !websocket_client_id.empty() &&
                            websocket_client_id.size() <= 64 &&
                            std::ranges::all_of(
                                websocket_client_id, [](unsigned char value) {
                                  return std::isalnum(value) || value == '.' ||
                                         value == '_' || value == '-';
                                });
                        if (subscribed) {
                          subscribed = ReserveEventClient(websocket_client_id);
                          event_client_reserved = subscribed;
                        }
                        if (subscription.contains("last_sequence"))
                          websocket_last_sequence =
                              subscription.at("last_sequence")
                                  .get<std::uint64_t>();
                        if (subscription.contains("publisher_epoch") &&
                            subscription.at("publisher_epoch").is_string() &&
                            !subscription.at("publisher_epoch")
                                 .get<std::string>()
                                 .empty() &&
                            subscription.at("publisher_epoch")
                                    .get<std::string>() != publisher_epoch_) {
                          websocket_last_sequence =
                              std::numeric_limits<std::uint64_t>::max();
                        }
                      } else {
                        subscribed = false;
                      }
                    } catch (const nlohmann::json::exception &) {
                      subscribed = false;
                    } catch (const std::exception &) {
                      subscribed = false;
                    }
                  } else {
                    subscribed = false;
                  }
                }

                if (!subscribed && !websocket_error) {
                  reject_websocket(websocket::close_code::policy_error,
                                   "invalid subscribe command");
                }
                if (subscribed) {
                  websocket_stream->next_layer().expires_never();
                  const auto connected_at = std::chrono::steady_clock::now();
                  auto heartbeat_at = std::chrono::steady_clock::now() +
                                      options_.websocket_heartbeat_interval;
                  auto last_client_activity = std::chrono::steady_clock::now();
                  std::deque<std::chrono::steady_clock::time_point>
                      client_command_times;
                  std::deque<std::chrono::steady_clock::time_point>
                      delivered_event_times;
                  websocket_stream->control_callback(
                      [&](websocket::frame_type kind, beast::string_view) {
                        if (kind == websocket::frame_type::pong) {
                          last_client_activity =
                              std::chrono::steady_clock::now();
                          server_state_->websocket_pongs_received.fetch_add(1);
                        } else if (kind == websocket::frame_type::close) {
                          last_client_activity =
                              std::chrono::steady_clock::now();
                        }
                      });
                  while (running_.load() && !websocket_error) {
                    bool websocket_still_available = true;
                    try {
                      websocket_still_available =
                          !options_.websocket_availability_probe ||
                          options_.websocket_availability_probe();
                    } catch (...) {
                      websocket_still_available = false;
                    }
                    if (!websocket_still_available) {
                      websocket_error = beast::error::timeout;
                      requested_close.code = websocket::close_code::going_away;
                      requested_close.reason =
                          "WebSocket event transport temporarily unavailable";
                      break;
                    }
                    auto batch = PollEvents(websocket_client_id,
                                            websocket_last_sequence, false);
                    if (batch.value("resync_required", false)) {
                      send_json(
                          {{"schema",
                            "graphx.dashboard.websocket_resync_required.v1"},
                           {"publisher_epoch", publisher_epoch_},
                           {"latest_sequence",
                            batch.value("latest_sequence", 0u)},
                           {"snapshot_url", "/api/v1/fhss/snapshot"},
                           {"reason",
                            batch.value("reason", "resync_required")}});
                      break;
                    }
                    for (const auto &event : batch.at("events")) {
                      const auto delivery_now =
                          std::chrono::steady_clock::now();
                      while (!delivered_event_times.empty() &&
                             delivery_now - delivered_event_times.front() >=
                                 std::chrono::seconds(1))
                        delivered_event_times.pop_front();
                      if (delivered_event_times.size() >=
                          options_.max_websocket_events_per_second)
                        break;
                      if (!send_json(event))
                        break;
                      delivered_event_times.push_back(delivery_now);
                      websocket_last_sequence =
                          event.at("sequence").get<std::uint64_t>();
                    }
                    if (websocket_error)
                      break;
                    if (std::chrono::steady_clock::now() >= heartbeat_at) {
                      send_json({{"schema",
                                  "graphx.dashboard.websocket_heartbeat.v1"},
                                 {"publisher_epoch", publisher_epoch_},
                                 {"timestamp", NowIso8601()}});
                      websocket_stream->ping({}, websocket_error);
                      heartbeat_at = std::chrono::steady_clock::now() +
                                     options_.websocket_heartbeat_interval;
                    }
                    if (websocket_error)
                      break;
                    boost::system::error_code read_nonblocking_error;
                    const auto readable_bytes =
                        websocket_stream->next_layer().socket().available(
                            read_nonblocking_error);
                    const auto client_message_ready =
                        !read_nonblocking_error && readable_bytes != 0 &&
                        read_message_bounded(false);
                    if (!client_message_ready && websocket_error)
                      read_nonblocking_error = websocket_error;
                    if (!read_nonblocking_error && client_message_ready) {
                      const auto command_now = std::chrono::steady_clock::now();
                      while (!client_command_times.empty() &&
                             command_now - client_command_times.front() >=
                                 std::chrono::seconds(1))
                        client_command_times.pop_front();
                      if (client_command_times.size() >=
                          options_.max_websocket_commands_per_second) {
                        reject_websocket(websocket::close_code::policy_error,
                                         "command rate limit exceeded");
                        break;
                      }
                      client_command_times.push_back(command_now);
                      bool heartbeat_valid = false;
                      try {
                        const auto client_message = nlohmann::json::parse(
                            beast::buffers_to_string(websocket_buffer.data()),
                            nullptr, false);
                        heartbeat_valid =
                            client_message.is_object() &&
                            client_message.size() == 2 &&
                            client_message.contains("action") &&
                            client_message.at("action").is_string() &&
                            client_message.at("action").get<std::string>() ==
                                "heartbeat_ack" &&
                            client_message.contains("publisher_epoch") &&
                            client_message.at("publisher_epoch").is_string() &&
                            client_message.at("publisher_epoch")
                                    .get<std::string>() == publisher_epoch_;
                      } catch (const nlohmann::json::exception &) {
                        heartbeat_valid = false;
                      } catch (const std::exception &) {
                        heartbeat_valid = false;
                      }
                      if (heartbeat_valid) {
                        last_client_activity = std::chrono::steady_clock::now();
                        server_state_->websocket_pongs_received.fetch_add(1);
                      } else {
                        reject_websocket(websocket::close_code::policy_error,
                                         "invalid heartbeat acknowledgement");
                        break;
                      }
                    }
                    if (read_nonblocking_error) {
                      websocket_error = read_nonblocking_error;
                      if (websocket_error != websocket::error::closed) {
                        const auto peer_reason = websocket_stream->reason();
                        if (peer_reason.code != websocket::close_code::none) {
                          requested_close = peer_reason;
                        } else if (requested_close.code ==
                                   websocket::close_code::normal) {
                          requested_close.code =
                              websocket::close_code::protocol_error;
                          requested_close.reason = "invalid WebSocket frame";
                        }
                        server_state_->websocket_protocol_failures.fetch_add(1);
                      }
                      break;
                    }
                    if (std::chrono::steady_clock::now() -
                            last_client_activity >
                        options_.websocket_idle_timeout) {
                      websocket_error = beast::error::timeout;
                      requested_close.code =
                          websocket::close_code::policy_error;
                      requested_close.reason = "client idle timeout";
                      server_state_->websocket_idle_closes.fetch_add(1);
                      break;
                    }
                    if (std::chrono::steady_clock::now() - connected_at >
                        options_.websocket_max_lifetime) {
                      websocket_error = beast::error::timeout;
                      requested_close.code = websocket::close_code::going_away;
                      requested_close.reason = "connection lifetime reached";
                      break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                  }
                }
                boost::system::error_code close_error;
                websocket_stream->next_layer().socket().non_blocking(
                    false, close_error);
                websocket_stream->next_layer().expires_after(std::min(
                    options_.write_timeout, options_.websocket_close_timeout));
                if (!running_.load() &&
                    requested_close.code == websocket::close_code::normal) {
                  requested_close.code = websocket::close_code::going_away;
                  requested_close.reason = "server shutting down";
                }
                if (websocket_stream->is_open() &&
                    websocket_error != websocket::error::closed)
                  websocket_stream->close(requested_close, close_error);
                switch (requested_close.code) {
                case websocket::close_code::normal:
                  server_state_->websocket_close_normal.fetch_add(1);
                  break;
                case websocket::close_code::protocol_error:
                  server_state_->websocket_close_protocol.fetch_add(1);
                  break;
                case websocket::close_code::unknown_data:
                  server_state_->websocket_close_unsupported.fetch_add(1);
                  break;
                case websocket::close_code::bad_payload:
                  server_state_->websocket_close_invalid_utf8.fetch_add(1);
                  break;
                case websocket::close_code::too_big:
                  server_state_->websocket_close_too_big.fetch_add(1);
                  break;
                case websocket::close_code::policy_error:
                  server_state_->websocket_close_policy.fetch_add(1);
                  break;
                case websocket::close_code::going_away:
                  server_state_->websocket_close_going_away.fetch_add(1);
                  break;
                default:
                  server_state_->websocket_close_internal.fetch_add(1);
                  break;
                }
                if (event_client_reserved)
                  (void)PollEvents(websocket_client_id, std::nullopt, true);
                server_state_->active_websocket_clients.fetch_sub(1);
                server_state_->active_connections.fetch_sub(1);
                done->store(true);
                return;
              }
            } else
              try {
                const bool globally_unsupported =
                    request.method != "GET" && request.method != "POST" &&
                    request.method != "PATCH" && request.method != "DELETE";
                if (globally_unsupported) {
                  response = HandleRequest(request);
                } else if (!request.body.empty() && request.method != "GET") {
                  if (const auto json_error =
                          ValidateJsonStructure(request.body, options_)) {
                    response = {
                        .status_code = 400,
                        .content_type = "application/problem+json",
                        .body = ErrorBody(400, "invalid_json", *json_error)};
                  } else {
                    response = HandleRequest(request);
                  }
                } else {
                  response = HandleRequest(request);
                }
              } catch (const nlohmann::json::exception &) {
                response = {.status_code = 400,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(
                                400, "invalid_request",
                                "request JSON has an invalid type or value")};
              } catch (const std::invalid_argument &) {
                response = {.status_code = 400,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(400, "invalid_request",
                                              "request value is invalid")};
              } catch (const std::exception &) {
                response = {.status_code = 500,
                            .content_type = "application/problem+json",
                            .body = ErrorBody(500, "internal_error",
                                              "request processing failed")};
              }
          }
          if (response.status_code >= 400) {
            const auto existing =
                nlohmann::json::parse(response.body, nullptr, false);
            if (existing.is_object()) {
              auto normalized = existing;
              const auto code =
                  normalized.value("code", std::string{"request_failed"});
              const auto detail =
                  normalized.value("message", std::string{"request failed"});
              if (!normalized.contains("type"))
                normalized["type"] = "urn:graphx:dashboard:problem:" + code;
              if (!normalized.contains("title"))
                normalized["title"] = code;
              if (!normalized.contains("detail"))
                normalized["detail"] = detail;
              // The wire status is authoritative. RFC 9457 requires this
              // advisory member to agree with the actual HTTP status code.
              normalized["status"] = response.status_code;
              response.body = normalized.dump();
            } else {
              response.body = ErrorBody(response.status_code, "request_failed",
                                        "request failed");
            }
            response.content_type = "application/problem+json";
          }
          if (response.body.size() > options_.max_response_bytes) {
            response = {.status_code = 500,
                        .content_type = "application/problem+json",
                        .body =
                            ErrorBody(500, "response_too_large",
                                      "response exceeded configured limit")};
          }
          http::response<http::string_body> wire_response{
              static_cast<http::status>(response.status_code), 11};
          wire_response.set(http::field::server, "GraphX-FHSS-Dashboard");
          wire_response.set(http::field::content_type, response.content_type);
          wire_response.set(http::field::cache_control, "no-store");
          wire_response.set(
              "Content-Security-Policy",
              "default-src 'self'; "
              "script-src 'self' "
              "'sha256-eXgEIvGKZeEewtdoMN9Rs1lNgWWR9OGjnBxgFa8e6wY='; "
              "style-src 'self' "
              "'sha256-QN5HWA7ALQoMjkETFy4axOyi4ziXbq5mXpeEyeZtDFw='; "
              "img-src 'self' data:; connect-src 'self'; object-src 'none'; "
              "base-uri 'none'; frame-ancestors 'none'");
          wire_response.set("X-Content-Type-Options", "nosniff");
          wire_response.set("Referrer-Policy", "no-referrer");
          wire_response.set("X-Frame-Options", "DENY");
          if (response.status_code == 405)
            wire_response.set(http::field::allow, response.allow);
          for (const auto &[name, value] : response.headers) {
            wire_response.set(name, value);
          }
          wire_response.keep_alive(false);
          wire_response.body() = std::move(response.body);
          wire_response.prepare_payload();
          const auto write_started = std::chrono::steady_clock::now();
          // Once a read or handler deadline has expired, retain only the
          // bounded write budget needed to return its terminal 408 problem.
          // Otherwise the total-request deadline remains authoritative.
          stream->expires_at(
              request_deadline <= write_started
                  ? write_started + options_.write_timeout
                  : std::min(request_deadline,
                             write_started + options_.write_timeout));
          boost::system::error_code write_error;
          request_context.restart();
          http::async_write(*stream, wire_response,
                            [&](boost::system::error_code error, std::size_t) {
                              write_error = error;
                            });
          request_context.run();
          boost::system::error_code ignored;
          stream->socket().shutdown(tcp::socket::shutdown_both, ignored);
          stream->socket().close(ignored);
          server_state_->active_connections.fetch_sub(1);
          done->store(true);
        }),
        .done = std::move(done)});
  }
}

std::string
EmbeddedDashboardServer::GuessContentType(const std::filesystem::path &path) {
  const auto ext = path.extension().string();
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".js" || ext == ".mjs") {
    return "text/javascript; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".json") {
    return "application/json";
  }
  if (ext == ".map") {
    return "application/json";
  }
  if (ext == ".woff") {
    return "font/woff";
  }
  if (ext == ".woff2") {
    return "font/woff2";
  }
  if (ext == ".ttf") {
    return "font/ttf";
  }
  if (ext == ".otf") {
    return "font/otf";
  }
  return "application/octet-stream";
}

std::string
EmbeddedDashboardServer::GetPathWithoutQuery(const std::string &target) {
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
    const auto token = query.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin);
    const auto equal = token.find('=');
    const auto token_key = token.substr(0, equal);
    if (token_key == key) {
      return equal == std::string::npos ? std::string{}
                                        : token.substr(equal + 1);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return {};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleRequest(const Request &request) const {
  const bool configuration_mutation =
      request.path == "/api/v1/fhss/config" ||
      request.path == "/api/v1/fhss/config/validate" ||
      request.path == "/api/v1/fhss/config/export";
  const bool mutation_enabled =
      options_.enable_mutating_routes ||
      (configuration_mutation && options_.enable_configuration_mutation_routes);
  if (!mutation_enabled && request.method != "GET" &&
      StartsWith(request.path, "/api/v1/fhss/")) {
    return Response{.status_code = 404,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }
  if (const auto allow = AllowedMethodsFor(request.path);
      allow && !MethodAllowed(request.method, *allow)) {
    return Response{
        .status_code = 405,
        .content_type = "application/problem+json",
        .body = ErrorBody(405, "method_not_allowed", "method not allowed"),
        .allow = *allow};
  }
  if (request.method == "GET") {
    if (request.path.rfind("/api/", 0) == 0 || request.path == "/healthz" ||
        request.path == "/readyz") {
      return HandleApiRequest(request);
    }
    return HandleStaticAsset(request);
  }

  if (request.method == "PATCH" || request.method == "POST" ||
      request.method == "DELETE") {
    return HandleApiRequest(request);
  }

  return Response{
      .status_code = 405,
      .content_type = "application/problem+json",
      .body = ErrorBody(405, "method_not_allowed", "method not allowed"),
      .allow = request.path.rfind("/api/", 0) == 0 ? "GET, POST, PATCH, DELETE"
                                                   : "GET"};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleApiRequest(const Request &request) const {
  const bool runtime_control_path =
      request.path == "/api/v1/fhss/config/rebuild" ||
      request.path == "/api/v1/fhss/config/discard" ||
      request.path == "/api/v1/fhss/config/undo" ||
      request.path == "/api/v1/fhss/status" ||
      request.path == "/api/v1/fhss/commands/start" ||
      request.path == "/api/v1/fhss/commands/stop" ||
      request.path == "/api/v1/fhss/events" ||
      request.path == "/api/v1/fhss/events/stream" ||
      request.path == "/api/v1/fhss/snapshot" ||
      StartsWith(request.path, "/api/v1/fhss/operations/");
  if (runtime_control_path && !options_.enable_runtime_control_routes) {
    return Response{.status_code = 404,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }
  if (request.path == "/healthz") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{{"status", "ok"}})};
  }

  if (request.path == "/readyz") {
    const bool ready = runtime_session_ && runtime_session_->IsReady();
    if (!ready) {
      return Response{.status_code = 503,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(503, "not_ready",
                                        "dashboard runtime is not ready")};
    }
    return Response{
        .status_code = ready ? 200 : 503,
        .content_type = "application/json",
        .body = JsonResponse(nlohmann::json{
            {"ready", ready}, {"state", runtime_session_->StateString()}})};
  }

  if (request.path == "/api/v1/version") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(nlohmann::json{
            {"schema", "graphx.dashboard.version.v1"}, {"api_version", "v1"}})};
  }

  if (request.path == "/api/v1/fhss/events" && request.method == "GET") {
    const auto client_id = GetQueryValue(request.query, "client_id");
    if (client_id.size() > 64 ||
        !std::ranges::all_of(client_id, [](unsigned char value) {
          return std::isalnum(value) || value == '.' || value == '_' ||
                 value == '-';
        })) {
      return Response{
          .status_code = 400,
          .content_type = "application/problem+json",
          .body = ErrorBody(400, "invalid_client_id",
                            "client_id must be 1-64 safe identifier bytes")};
    }
    const auto last_sequence_raw =
        GetQueryValue(request.query, "last_sequence");
    const auto disconnect =
        ParseBool(GetQueryValue(request.query, "disconnect"));
    const auto effective_id = client_id.empty() ? "default" : client_id;
    if (!disconnect && !ReserveEventClient(effective_id)) {
      return Response{.status_code = 429,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(429, "event_client_limit",
                                        "event client limit reached")};
    }

    std::optional<std::uint64_t> last_sequence;
    if (!last_sequence_raw.empty()) {
      last_sequence = ParseUint64(last_sequence_raw);
      if (!last_sequence) {
        return Response{
            .status_code = 400,
            .content_type = "application/json",
            .body = ErrorBody(400, "invalid_last_sequence",
                              "last_sequence must be an unsigned integer")};
      }
    }

    const auto events = PollEvents(effective_id, last_sequence, disconnect);
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(events)};
  }

  if (request.path == "/api/v1/fhss/events/stream") {
    return Response{.status_code = 426,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(426, "websocket_upgrade_required",
                                      "RFC 6455 WebSocket upgrade required"),
                    .headers = {{"Upgrade", "websocket"}}};
  }

  if (request.path == "/api/v1/fhss/snapshot") {
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto config_before = configuration_service_->GetConfigResponse();
      const auto graph_snapshot = configuration_service_->GetGraphResponse();
      const auto runtime_before = runtime_session_->SnapshotStatus();
      const auto metrics = snapshot_collector_->GetMetricsSnapshot();
      const auto diagnostics = snapshot_collector_->GetDiagnosticsSnapshot();
      const auto runtime_after = runtime_session_->SnapshotStatus();
      const auto config_after = configuration_service_->GetConfigResponse();
      if (config_before.value("config_revision", 0u) !=
              config_after.value("config_revision", 0u) ||
          config_before.value("etag", std::string{}) !=
              config_after.value("etag", std::string{}) ||
          runtime_before.active_generation != runtime_after.active_generation ||
          runtime_before.active_run_epoch != runtime_after.active_run_epoch ||
          runtime_before.state != runtime_after.state) {
        continue;
      }
      std::uint64_t latest_sequence = 0;
      std::uint64_t dropped_events = 0;
      std::uint64_t coalesced_events = 0;
      {
        std::scoped_lock lock(event_mutex_);
        latest_sequence = next_event_sequence_ - 1;
        dropped_events = dropped_events_total_;
        coalesced_events = coalesced_events_total_;
      }
      return Response{
          .status_code = 200,
          .content_type = "application/json",
          .body = JsonResponse(
              {{"schema", "graphx.dashboard.fhss_snapshot.v1"},
               {"publisher_epoch", publisher_epoch_},
               {"latest_sequence", latest_sequence},
               {"captured_at", NowIso8601()},
               {"config_revision", config_after.value("config_revision", 0u)},
               {"config_etag", config_after.value("etag", std::string{})},
               {"generation", runtime_after.active_generation},
               {"run_epoch", runtime_after.active_run_epoch},
               {"configuration", config_after},
               {"graph", graph_snapshot},
               {"runtime", RuntimeStatusJson(runtime_after)},
               {"metrics", metrics},
               {"transport",
                {{"active_websocket_clients",
                  server_state_->active_websocket_clients.load()},
                 {"pongs_received",
                  server_state_->websocket_pongs_received.load()},
                 {"idle_closes", server_state_->websocket_idle_closes.load()},
                 {"protocol_failures",
                  server_state_->websocket_protocol_failures.load()},
                 {"rejected_upgrades",
                  server_state_->websocket_rejected_upgrades.load()},
                 {"replayed_events",
                  server_state_->websocket_replayed_events.load()},
                 {"resync_requests",
                  server_state_->websocket_resync_requests.load()},
                 {"queue_overflows",
                  server_state_->websocket_queue_overflows.load()},
                 {"close_reasons",
                  {{"normal", server_state_->websocket_close_normal.load()},
                   {"protocol", server_state_->websocket_close_protocol.load()},
                   {"unsupported_data",
                    server_state_->websocket_close_unsupported.load()},
                   {"invalid_utf8",
                    server_state_->websocket_close_invalid_utf8.load()},
                   {"too_big", server_state_->websocket_close_too_big.load()},
                   {"policy", server_state_->websocket_close_policy.load()},
                   {"going_away",
                    server_state_->websocket_close_going_away.load()},
                   {"internal",
                    server_state_->websocket_close_internal.load()}}},
                 {"dropped_events_total", dropped_events},
                 {"coalesced_events_total", coalesced_events}}},
               {"diagnostics", diagnostics}})};
    }
    return Response{
        .status_code = 503,
        .content_type = "application/problem+json",
        .body = ErrorBody(503, "snapshot_changed_during_capture",
                          "dashboard state changed during snapshot capture")};
  }

  if (request.path == "/api/v1/fhss/graph") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(configuration_service_->GetGraphResponse())};
  }

  if (request.path == "/api/v1/fhss/config") {
    if (request.method == "GET") {
      return Response{
          .status_code = 200,
          .content_type = "application/json",
          .body = JsonResponse(configuration_service_->GetConfigResponse()),
          .headers = {{"ETag", configuration_service_->ETag()}}};
    }
    if (request.method == "PATCH") {
      const auto body =
          request.body.empty()
              ? nlohmann::json::object()
              : nlohmann::json::parse(request.body, nullptr, false);
      if (body.is_discarded()) {
        return Response{.status_code = 400,
                        .content_type = "application/json",
                        .body = ErrorBody(400, "invalid_json",
                                          "request body must be JSON")};
      }
      const auto content_type = request.headers.contains("content-type")
                                    ? request.headers.at("content-type")
                                    : std::string{};
      nlohmann::json result;
      const auto media_type = ParseMediaType(content_type);
      if (media_type == "application/json-patch+json") {
        const auto if_match = request.headers.contains("if-match")
                                  ? request.headers.at("if-match")
                                  : std::string{};
        result = configuration_service_->ApplyJsonPatch(body, if_match, false);
      } else if (media_type == "application/json") {
        result = configuration_service_->PatchConfig(body);
        result["compatibility"] = "deprecated expected_revision wrapper";
      } else {
        result = nlohmann::json{
            {"schema", "graphx.dashboard.error.v1"},
            {"status", 415},
            {"code", "unsupported_media_type"},
            {"message", "PATCH requires application/json-patch+json"}};
      }
      const auto status =
          result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
              ? result.value("status", 500)
              : 200;
      if (status == 200)
        PublishEvent("configuration_changed", result,
                     {{"semantic_class", "configuration"}});
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result),
                      .headers = {{"ETag", configuration_service_->ETag()}}};
    }
  }

  if (request.path == "/api/v1/fhss/config/authoritative") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(configuration_service_->GetScenarioResponse()),
        .headers = {{"ETag", configuration_service_->ETag()}}};
  }

  if (request.path == "/api/v1/fhss/config/effective") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(configuration_service_->GetConfigResponse()),
        .headers = {{"ETag", configuration_service_->ETag()}}};
  }

  if (request.path == "/api/v1/fhss/config/provenance") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(configuration_service_->GetProvenanceResponse()),
        .headers = {{"ETag", configuration_service_->ETag()}}};
  }

  if (request.path == "/api/v1/fhss/graph/receiver-minimal") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(
                        configuration_service_->GetReceiverGraphResponse()),
                    .headers = {{"ETag", configuration_service_->ETag()}}};
  }

  if (request.path == "/api/v1/fhss/config/derived-paths") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(
                        configuration_service_->GetDerivedPathsResponse())};
  }

  if (request.path == "/api/v1/fhss/config/value") {
    const auto pointer = GetQueryValue(request.query, "pointer");
    if (pointer.empty()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "missing_pointer",
                                        "pointer query parameter is required")};
    }
    const auto result = configuration_service_->GetValueResponse(pointer);
    const auto status =
        result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
            ? result.value("status", 404)
            : 200;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (StartsWith(request.path, "/api/v1/fhss/nodes/")) {
    const auto node_suffix =
        request.path.substr(std::string{"/api/v1/fhss/nodes/"}.size());
    const std::string parameters_suffix = "/parameters";
    const auto parameters_pos = node_suffix.rfind(parameters_suffix);
    if (parameters_pos != std::string::npos &&
        parameters_pos == node_suffix.size() - parameters_suffix.size()) {
      const auto node_id = node_suffix.substr(0, parameters_pos);
      const auto result =
          configuration_service_->GetNodeParametersResponse(node_id);
      const auto status =
          result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
              ? result.value("status", 404)
              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    const auto result = configuration_service_->GetNodeResponse(node_suffix);
    const auto status =
        result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
            ? result.value("status", 404)
            : 200;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (request.path == "/api/v1/fhss/metrics") {
    const auto metrics_snapshot = snapshot_collector_->GetMetricsSnapshot();
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(metrics_snapshot)};
  }

  if (request.path == "/api/v1/fhss/metrics/edges") {
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(snapshot_collector_->GetEdgeMetricsSnapshot())};
  }

  if (request.path == "/api/v1/fhss/diagnostics") {
    const auto diagnostics_snapshot =
        snapshot_collector_->GetDiagnosticsSnapshot();
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(diagnostics_snapshot)};
  }

  if (request.path == "/api/v1/fhss/config/validate" &&
      request.method == "POST") {
    const auto body = request.body.empty()
                          ? nlohmann::json::object()
                          : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{
          .status_code = 400,
          .content_type = "application/problem+json",
          .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }
    nlohmann::json result;
    const auto content_type = request.headers.contains("content-type")
                                  ? request.headers.at("content-type")
                                  : std::string{};
    const auto media_type = ParseMediaType(content_type);
    if (media_type == "application/json-patch+json") {
      const auto if_match = request.headers.contains("if-match")
                                ? request.headers.at("if-match")
                                : configuration_service_->ETag();
      result = configuration_service_->ApplyJsonPatch(body, if_match, true);
    } else if (media_type == "application/json") {
      result = configuration_service_->ValidateConfig(body);
    } else {
      return Response{
          .status_code = 415,
          .content_type = "application/problem+json",
          .body = ErrorBody(415, "unsupported_media_type",
                            "validation requires application/json-patch+json "
                            "or application/json")};
    }
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(result),
                    .headers = {{"ETag", configuration_service_->ETag()}}};
  }

  if (request.path == "/api/v1/fhss/config/rebuild" &&
      request.method == "POST") {
    const auto body = request.body.empty()
                          ? nlohmann::json::object()
                          : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{
          .status_code = 400,
          .content_type = "application/problem+json",
          .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }

    const auto receiver = configuration_service_->GetReceiverGraphResponse();
    const auto revision = receiver.at("config_revision").get<std::uint64_t>();
    const auto etag = receiver.at("etag").get<std::string>();
    const auto expected_revision = body.value("expected_revision", revision);
    if (expected_revision != revision) {
      return Response{.status_code = 409,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(
                          409, "stale_revision_conflict",
                          "expected revision does not match current revision")};
    }

    const auto rebuild =
        runtime_session_->Rebuild({.receiver_graph = receiver.at("graph"),
                                   .config_revision = revision,
                                   .config_etag = etag});
    if (rebuild.status_code >= 400) {
      return Response{.status_code = rebuild.status_code,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(rebuild.status_code, rebuild.code,
                                        rebuild.message)};
    }

    const auto status = runtime_session_->SnapshotStatus();
    PublishEvent("runtime_rebuilt", RuntimeStatusJson(status),
                 {{"semantic_class", "runtime"}});
    return Response{
        .status_code = 200,
        .content_type = "application/json",
        .body = JsonResponse(nlohmann::json{
            {"schema", "graphx.dashboard.rebuild_result.v1"},
            {"command_id", body.value("command_id", std::string{})},
            {"status", rebuild.code == "cleanup_failed"
                           ? "succeeded_with_cleanup_failed"
                           : "succeeded"},
            {"submitted_revision", revision},
            {"etag", etag},
            {"lifecycle_state",
             GraphRuntimeSession::StateToString(status.state)},
            {"active_generation", status.active_generation},
            {"warning", rebuild.code == "cleanup_failed"
                            ? nlohmann::json{{"code", rebuild.code},
                                             {"message", rebuild.message}}
                            : nlohmann::json(nullptr)}})};
  }

  if (request.path == "/api/v1/fhss/status" && request.method == "GET") {
    const auto config = configuration_service_->GetConfigResponse();
    auto status = RuntimeStatusJson(runtime_session_->SnapshotStatus());
    status["config_revision"] = config.at("config_revision");
    status["etag"] = config.at("etag");
    status["rebuild_required"] =
        status.value("active_config_revision", 0u) !=
        config.at("config_revision").get<std::uint64_t>();
    status["configuration_stale"] = status["rebuild_required"];
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(status)};
  }

  if (request.path == "/api/v1/fhss/commands/start" &&
      request.method == "POST") {
    const auto body = request.body.empty()
                          ? nlohmann::json::object()
                          : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded())
      return {.status_code = 400,
              .content_type = "application/problem+json",
              .body =
                  ErrorBody(400, "invalid_json", "request body must be JSON")};
    const auto result = runtime_session_->Start();
    PublishEvent("command", nlohmann::json{{"command", "start"},
                                           {"status", result.status_code == 202
                                                          ? "accepted"
                                                          : "rejected"},
                                           {"code", result.code},
                                           {"message", result.message}});
    if (result.status_code != 202) {
      return Response{
          .status_code = result.status_code,
          .content_type = "application/problem+json",
          .body = ErrorBody(result.status_code, result.code, result.message)};
    }
    return Response{.status_code = 202,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{
                        {"schema", "graphx.dashboard.command_result.v1"},
                        {"command_id", body.value("command_id", std::string{})},
                        {"status", "accepted"},
                        {"active_generation",
                         runtime_session_->SnapshotStatus().active_generation},
                        {"code", result.code},
                        {"message", result.message}})};
  }

  if (request.path == "/api/v1/fhss/commands/stop" &&
      request.method == "POST") {
    const auto body = request.body.empty()
                          ? nlohmann::json::object()
                          : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded())
      return {.status_code = 400,
              .content_type = "application/problem+json",
              .body =
                  ErrorBody(400, "invalid_json", "request body must be JSON")};
    const auto result = runtime_session_->Stop();
    PublishEvent("command", nlohmann::json{{"command", "stop"},
                                           {"status", result.status_code < 400
                                                          ? "completed"
                                                          : "rejected"},
                                           {"code", result.code},
                                           {"message", result.message}});
    if (result.status_code >= 400) {
      return Response{
          .status_code = result.status_code,
          .content_type = "application/problem+json",
          .body = ErrorBody(result.status_code, result.code, result.message)};
    }
    return Response{.status_code = result.status_code,
                    .content_type = "application/json",
                    .body = JsonResponse(nlohmann::json{
                        {"schema", "graphx.dashboard.command_result.v1"},
                        {"command_id", body.value("command_id", std::string{})},
                        {"status", "completed"},
                        {"active_generation",
                         runtime_session_->SnapshotStatus().active_generation},
                        {"code", result.code},
                        {"message", result.message}})};
  }

  if (request.path == "/api/v1/fhss/config/discard" &&
      request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body =
                        JsonResponse(configuration_service_->DiscardEdits())};
  }

  if (request.path == "/api/v1/fhss/config/undo" && request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body =
                        JsonResponse(configuration_service_->UndoLastEdit())};
  }

  if (request.path == "/api/v1/fhss/config/export" &&
      request.method == "POST") {
    const auto body = request.body.empty()
                          ? nlohmann::json::object()
                          : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{
          .status_code = 400,
          .content_type = "application/json",
          .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }
    const auto result = configuration_service_->ExportConfig(body);
    const auto status =
        result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
            ? result.value("status", 500)
            : 202;
    return Response{.status_code = status,
                    .content_type = "application/json",
                    .body = JsonResponse(result)};
  }

  if (StartsWith(request.path, "/api/v1/fhss/operations/")) {
    const auto operation_suffix =
        request.path.substr(std::string{"/api/v1/fhss/operations/"}.size());
    const std::string cancel_suffix = "/cancel";
    if (request.method == "POST" &&
        operation_suffix.size() > cancel_suffix.size() &&
        operation_suffix.rfind(cancel_suffix) ==
            operation_suffix.size() - cancel_suffix.size()) {
      const auto operation_id = operation_suffix.substr(
          0, operation_suffix.size() - cancel_suffix.size());
      const auto result = configuration_service_->CancelOperation(operation_id);
      const auto status =
          result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
              ? result.value("status", 409)
              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "GET") {
      const auto result =
          configuration_service_->GetOperationResponse(operation_suffix);
      const auto status =
          result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
              ? result.value("status", 404)
              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "DELETE") {
      std::string error_code;
      if (!configuration_service_->DeleteOperation(operation_suffix,
                                                   &error_code)) {
        const auto status = error_code == "operation_not_terminal" ? 409 : 404;
        return Response{.status_code = status,
                        .content_type = "application/json",
                        .body =
                            ErrorBody(status,
                                      error_code.empty()
                                          ? "operation_not_found_or_expired"
                                          : error_code,
                                      error_code == "operation_not_terminal"
                                          ? "operation is not terminal"
                                          : "operation not found or expired")};
      }
      return Response{
          .status_code = 204, .content_type = "application/json", .body = {}};
    }
  }

  if (options_.application_api_handler) {
    if (std::chrono::steady_clock::now() >= request.deadline) {
      return Response{.status_code = 408,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(408, "request_timeout",
                                        "request deadline exceeded")};
    }
    auto job = std::make_shared<ServerState::HandlerJob>();
    job->handler = options_.application_api_handler->handler;
    job->request = ApiRequest{.method = request.method,
                              .path = request.path,
                              .query = request.query,
                              .body = request.body,
                              .headers = request.headers};
    job->deadline = request.deadline;
    auto future = job->completion.get_future();
    {
      std::scoped_lock lock(server_state_->handler_mutex);
      const auto outstanding = server_state_->handler_queue.size() +
                               server_state_->active_handler_jobs.size();
      if (server_state_->handler_stopping ||
          outstanding >= options_.max_concurrent_connections) {
        return Response{
            .status_code = 503,
            .content_type = "application/problem+json",
            .body = ErrorBody(503, "handler_capacity_exhausted",
                              "application handler capacity exhausted")};
      }
      server_state_->handler_queue.push_back(job);
    }
    server_state_->handler_cv.notify_one();
    if (future.wait_until(request.deadline) != std::future_status::ready) {
      job->stop_source.request_stop();
      return Response{.status_code = 408,
                      .content_type = "application/problem+json",
                      .body =
                          ErrorBody(408, "request_timeout",
                                    "application handler deadline exceeded")};
    }
    const auto application_response = future.get();
    if (application_response) {
      if (request.method != "GET") {
        const auto payload =
            nlohmann::json::parse(application_response->body, nullptr, false);
        nlohmann::json context = nlohmann::json::object();
        if (payload.is_object()) {
          for (const auto *field :
               {"controller_epoch", "job_id", "scenario_correlation_id",
                "semantic_class"}) {
            if (payload.contains(field))
              context[field == std::string_view{"scenario_correlation_id"}
                          ? "correlation_id"
                          : field] = payload.at(field);
          }
        }
        PublishEvent(request.path.starts_with("/api/v1/fhss/jobs") ||
                             request.path.starts_with("/api/v1/fhss/commands/")
                         ? "job_control"
                         : "application_change",
                     payload.is_object()
                         ? payload
                         : nlohmann::json{{"http_status",
                                           application_response->status_code}},
                     std::move(context));
      }
      return Response{.status_code = application_response->status_code,
                      .content_type = application_response->content_type,
                      .body = application_response->body,
                      .headers = application_response->headers};
    }
  }

  return Response{.status_code = 404,
                  .content_type = "application/json",
                  .body = ErrorBody(404, "not_found", "resource not found")};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleStaticAsset(const Request &request) const {
  const std::string encoded_target =
      request.path == "/" ? "/index.html" : request.path;
  const auto decoded_target = PercentDecodePath(encoded_target);
  if (!decoded_target || decoded_target->find("..") != std::string::npos) {
    return Response{.status_code = 404,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }

  std::filesystem::path relative = *decoded_target;
  if (relative.is_absolute()) {
    relative = relative.relative_path();
  }

  std::filesystem::path resolved = options_.asset_directory / relative;
  std::error_code error;
  const auto canonical_root =
      std::filesystem::weakly_canonical(options_.asset_directory, error);
  const auto canonical_file =
      std::filesystem::weakly_canonical(resolved, error);
  if (error || !IsContainedPath(canonical_root, canonical_file) ||
      !std::filesystem::is_regular_file(canonical_file, error)) {
    return Response{.status_code = 404,
                    .content_type = "application/json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }
  const auto asset = ReadAssetWithoutFollowingLinks(
      options_.asset_directory, relative, options_.max_response_bytes);
  if (asset.too_large) {
    return Response{.status_code = 413,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(413, "asset_too_large",
                                      "static asset exceeds response limit")};
  }
  if (!asset.body || asset.body->empty()) {
    return Response{.status_code = 404,
                    .content_type = "application/json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }

  return Response{.status_code = 200,
                  .content_type = GuessContentType(canonical_file),
                  .body = *asset.body};
}

std::string EmbeddedDashboardServer::NowIso8601() const {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds);
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

nlohmann::json
EmbeddedDashboardServer::EventEnvelopeJson(const EventEnvelope &event) const {
  nlohmann::json json{{"schema", "graphx.dashboard.event.v1"},
                      {"api_version", "v1"},
                      {"publisher_epoch", event.publisher_epoch},
                      {"event_type", event.event_type},
                      {"sequence", event.sequence},
                      {"timestamp", event.timestamp},
                      {"generation", event.generation},
                      {"run_epoch", event.run_epoch},
                      {"config_revision", event.config_revision},
                      {"config_etag", event.config_etag},
                      {"payload", event.payload}};
  for (const auto *field :
       {"controller_epoch", "job_id", "correlation_id", "semantic_class"})
    json[field] = event.context.contains(field) ? event.context.at(field)
                                                : nlohmann::json(nullptr);
  return json;
}

void EmbeddedDashboardServer::TrimRetainedEventsLocked(
    std::chrono::system_clock::time_point now) const {
  while (!retained_events_.empty()) {
    const auto expired =
        retained_events_.front().second + event_retention_window_ < now;
    if (!expired) {
      break;
    }
    retained_event_bytes_ -= retained_events_.front().first.encoded_bytes;
    retained_events_.pop_front();
  }

  while (retained_events_.size() > max_retained_events_ ||
         retained_event_bytes_ > max_retained_event_bytes_) {
    retained_event_bytes_ -= retained_events_.front().first.encoded_bytes;
    retained_events_.pop_front();
  }
}

void EmbeddedDashboardServer::PublishEventImpl(
    std::string event_type, nlohmann::json payload, nlohmann::json context,
    std::string timestamp, std::uint64_t generation, std::uint64_t run_epoch,
    std::uint64_t config_revision, std::string config_etag) const {
  const auto now = std::chrono::system_clock::now();
  EventEnvelope envelope;
  envelope.event_type = std::move(event_type);
  envelope.timestamp = std::move(timestamp);
  envelope.publisher_epoch = publisher_epoch_;
  envelope.generation = generation;
  envelope.run_epoch = run_epoch;
  envelope.config_revision = config_revision;
  envelope.config_etag = std::move(config_etag);
  envelope.context =
      context.is_object() ? std::move(context) : nlohmann::json::object();
  envelope.payload = std::move(payload);
  std::scoped_lock lock(event_mutex_);
  if (next_event_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    for (auto &[id, client] : clients_) {
      (void)id;
      client.resync_required = true;
    }
    return;
  }
  envelope.sequence = next_event_sequence_;
  envelope.encoded_bytes = EventEnvelopeJson(envelope).dump().size();
  ++next_event_sequence_;
  TrimRetainedEventsLocked(now);
  if (envelope.encoded_bytes > max_retained_event_bytes_) {
    ++coalesced_events_total_;
    for (auto &[id, client] : clients_) {
      (void)id;
      client.resync_required = true;
      ++client.dropped_events;
      ++dropped_events_total_;
    }
    return;
  }

  retained_events_.emplace_back(envelope, now);
  if (envelope.encoded_bytes >
      std::numeric_limits<std::size_t>::max() - retained_event_bytes_) {
    retained_events_.pop_back();
    return;
  }
  retained_event_bytes_ += envelope.encoded_bytes;
  TrimRetainedEventsLocked(now);

  for (auto &[client_id, client] : clients_) {
    if (client.resync_required) {
      (void)client_id;
      continue;
    }
    if (client.queue.size() >= per_client_queue_depth_ ||
        envelope.encoded_bytes >
            per_client_queue_bytes_ -
                std::min(per_client_queue_bytes_, client.queued_bytes)) {
      dropped_events_total_ += client.queue.size();
      client.dropped_events += client.queue.size();
      client.queue.clear();
      client.queued_bytes = 0;
      client.resync_required = true;
      ++coalesced_events_total_;
      server_state_->websocket_queue_overflows.fetch_add(1);
      continue;
    }
    client.queue.push_back(envelope);
    client.queued_bytes += envelope.encoded_bytes;
  }
}

bool EmbeddedDashboardServer::ReserveEventClient(
    const std::string &client_id) const {
  std::scoped_lock lock(event_mutex_);
  const auto now = std::chrono::steady_clock::now();
  std::erase_if(clients_, [&](const auto &entry) {
    return entry.second.last_activity + options_.websocket_client_state_ttl <
           now;
  });
  if (clients_.contains(client_id))
    return true;
  if (clients_.size() >= options_.max_websocket_clients)
    return false;
  clients_.try_emplace(client_id);
  return true;
}

nlohmann::json
EmbeddedDashboardServer::PollEvents(const std::string &client_id,
                                    std::optional<std::uint64_t> last_sequence,
                                    bool clear_client) const {
  std::vector<EventEnvelope> outbound;
  bool resync_required = false;
  bool truncated = false;
  std::string resync_reason = "none";
  std::uint64_t latest_sequence = 0;
  std::uint64_t oldest_available_sequence = 0;
  std::uint64_t newest_available_sequence = 0;
  std::uint64_t dropped_events = 0;
  std::uint64_t delivered_through = last_sequence.value_or(0);
  std::size_t outbound_bytes = 0;
  std::uint64_t dropped_total = 0;
  std::uint64_t coalesced_total = 0;
  std::uint64_t reconnects_total = 0;

  {
    std::scoped_lock lock(event_mutex_);
    const auto now = std::chrono::system_clock::now();
    TrimRetainedEventsLocked(now);

    const auto steady_now = std::chrono::steady_clock::now();
    std::erase_if(clients_, [&](const auto &entry) {
      return entry.second.last_activity + options_.websocket_client_state_ttl <
             steady_now;
    });

    if (clear_client) {
      clients_.erase(client_id);
      ++reconnects_total_;
      return nlohmann::json{
          {"schema", "graphx.dashboard.events_batch.v1"},
          {"stream", "/api/v1/fhss/events"},
          {"publisher_epoch", publisher_epoch_},
          {"client_id", client_id},
          {"resync_required", false},
          {"reason", "none"},
          {"latest_sequence", next_event_sequence_ - 1},
          {"oldest_available_sequence",
           retained_events_.empty() ? next_event_sequence_
                                    : retained_events_.front().first.sequence},
          {"newest_available_sequence", next_event_sequence_ - 1},
          {"truncated", false},
          {"events", nlohmann::json::array()},
          {"counters",
           {{"dropped_events", 0},
            {"dropped_events_total", dropped_events_total_},
            {"coalesced_events_total", coalesced_events_total_},
            {"reconnects_total", reconnects_total_}}}};
    }

    auto [client_entry, inserted] = clients_.try_emplace(client_id);
    if (inserted && clients_.size() > options_.max_websocket_clients) {
      clients_.erase(client_entry);
      server_state_->websocket_resync_requests.fetch_add(1);
      return nlohmann::json{
          {"schema", "graphx.dashboard.events_batch.v1"},
          {"stream", "/api/v1/fhss/events"},
          {"publisher_epoch", publisher_epoch_},
          {"client_id", client_id},
          {"resync_required", true},
          {"reason", "client_limit"},
          {"latest_sequence", next_event_sequence_ - 1},
          {"oldest_available_sequence",
           retained_events_.empty() ? next_event_sequence_
                                    : retained_events_.front().first.sequence},
          {"newest_available_sequence", next_event_sequence_ - 1},
          {"truncated", false},
          {"events", nlohmann::json::array()},
          {"counters",
           {{"dropped_events", 0},
            {"dropped_events_total", dropped_events_total_},
            {"coalesced_events_total", coalesced_events_total_},
            {"reconnects_total", reconnects_total_}}}};
    }
    auto &client = client_entry->second;
    client.last_activity = steady_now;
    if (client.queue.empty() && !clear_client) {
      ++reconnects_total_;
    }

    dropped_events = client.dropped_events;
    client.dropped_events = 0;

    latest_sequence = next_event_sequence_ - 1;
    oldest_available_sequence = retained_events_.empty()
                                    ? next_event_sequence_
                                    : retained_events_.front().first.sequence;
    newest_available_sequence = retained_events_.empty()
                                    ? next_event_sequence_ - 1
                                    : retained_events_.back().first.sequence;
    if (retained_events_.empty() && last_sequence.has_value() &&
        *last_sequence != next_event_sequence_ - 1) {
      client.resync_required = true;
      resync_required = true;
      resync_reason = "retention_gap";
    } else if (!retained_events_.empty()) {
      latest_sequence = retained_events_.back().first.sequence;
      if (last_sequence.has_value()) {
        if (*last_sequence == std::numeric_limits<std::uint64_t>::max()) {
          client.resync_required = true;
          resync_required = true;
          resync_reason = "publisher_epoch_changed";
        } else {
          const auto expected_first = *last_sequence + 1;
          const auto first_available = retained_events_.front().first.sequence;
          if (expected_first < first_available ||
              *last_sequence > latest_sequence) {
            client.resync_required = true;
            resync_required = true;
            resync_reason = *last_sequence > latest_sequence ? "sequence_ahead"
                                                             : "retention_gap";
          } else if (expected_first <= latest_sequence) {
            std::uint64_t expected = expected_first;
            for (const auto &[event, _time_point] : retained_events_) {
              if (event.sequence <= *last_sequence) {
                continue;
              }
              if (event.sequence != expected) {
                client.resync_required = true;
                resync_required = true;
                resync_reason = "sequence_gap";
                break;
              }
              if (outbound.size() >= options_.max_websocket_replay_events ||
                  event.encoded_bytes >
                      options_.max_websocket_replay_bytes - outbound_bytes) {
                client.resync_required = true;
                resync_required = true;
                truncated = true;
                resync_reason = "replay_limit";
                break;
              }
              outbound.push_back(event);
              server_state_->websocket_replayed_events.fetch_add(1);
              outbound_bytes += event.encoded_bytes;
              delivered_through = event.sequence;
              if (expected == std::numeric_limits<std::uint64_t>::max()) {
                client.resync_required = true;
                resync_required = true;
                break;
              }
              ++expected;
            }
          }
        }
      }
    }

    if (client.resync_required) {
      resync_required = true;
      if (resync_reason == "none")
        resync_reason = "queue_overflow";
      outbound.clear();
      client.queue.clear();
      client.queued_bytes = 0;
    } else {
      while (!client.queue.empty()) {
        if (client.queue.front().sequence > delivered_through) {
          if (outbound.size() >= options_.max_websocket_replay_events ||
              client.queue.front().encoded_bytes >
                  options_.max_websocket_replay_bytes - outbound_bytes) {
            client.resync_required = true;
            resync_required = true;
            truncated = true;
            resync_reason = "replay_limit";
            outbound.clear();
            break;
          }
          outbound.push_back(client.queue.front());
          outbound_bytes += client.queue.front().encoded_bytes;
        }
        client.queued_bytes -= client.queue.front().encoded_bytes;
        client.queue.pop_front();
      }
      if (!outbound.empty()) {
        latest_sequence = std::max(latest_sequence, outbound.back().sequence);
      }
    }
    dropped_total = dropped_events_total_;
    coalesced_total = coalesced_events_total_;
    reconnects_total = reconnects_total_;
    if (resync_required)
      server_state_->websocket_resync_requests.fetch_add(1);
  }

  nlohmann::json events = nlohmann::json::array();
  for (const auto &event : outbound) {
    events.push_back(EventEnvelopeJson(event));
  }

  return nlohmann::json{
      {"schema", "graphx.dashboard.events_batch.v1"},
      {"stream", "/api/v1/fhss/events"},
      {"publisher_epoch", publisher_epoch_},
      {"client_id", client_id},
      {"resync_required", resync_required},
      {"reason", resync_reason},
      {"latest_sequence", latest_sequence},
      {"oldest_available_sequence", oldest_available_sequence},
      {"newest_available_sequence", newest_available_sequence},
      {"truncated", truncated},
      {"events", std::move(events)},
      {"counters", nlohmann::json{{"dropped_events", dropped_events},
                                  {"dropped_events_total", dropped_total},
                                  {"coalesced_events_total", coalesced_total},
                                  {"reconnects_total", reconnects_total}}}};
}

} // namespace graph::dashboard
