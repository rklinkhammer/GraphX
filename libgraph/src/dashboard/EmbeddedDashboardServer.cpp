// SPDX-License-Identifier: MIT

#include "graph/dashboard/EmbeddedDashboardServer.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace graph::dashboard {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
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

struct AssetReadResult {
  std::optional<std::string> body;
  bool too_large = false;
};

AssetReadResult ReadAssetWithoutFollowingLinks(const std::filesystem::path &root,
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
  } directory{::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
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
  struct stat metadata {};
  if (::fstat(file.value, &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
    return {};
  }
  if (static_cast<std::uintmax_t>(metadata.st_size) > maximum_bytes) {
    return {.too_large = true};
  }
  std::string body(static_cast<std::size_t>(metadata.st_size), '\0');
  std::size_t offset = 0;
  while (offset < body.size()) {
    const auto count = ::read(file.value, body.data() + offset, body.size() - offset);
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
  const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
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
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%') { decoded.push_back(value[i]); continue; }
    if (i + 2 >= value.size()) return std::nullopt;
    const int high = hex(value[i + 1]);
    const int low = hex(value[i + 2]);
    if (high < 0 || low < 0) return std::nullopt;
    const char byte = static_cast<char>((high << 4) | low);
    if (byte == '\0' || byte == '\\') return std::nullopt;
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
    if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
  }
  return true;
}

std::optional<std::string> AllowedMethodsFor(const std::string &path) {
  if (path == "/healthz" || path == "/readyz" || path == "/api/v1/version" ||
      path == "/api/v1/fhss/events" || path == "/api/v1/fhss/graph" ||
      path == "/api/v1/fhss/config/authoritative" ||
      path == "/api/v1/fhss/config/effective" ||
      path == "/api/v1/fhss/config/derived-paths" ||
      path == "/api/v1/fhss/config/value" || path == "/api/v1/fhss/metrics" ||
      path == "/api/v1/fhss/metrics/edges" ||
      path == "/api/v1/fhss/diagnostics" || path == "/api/v1/fhss/status" ||
      path == "/api/v1/fhss/visualization" ||
      StartsWith(path, "/api/v1/fhss/nodes/")) return "GET";
  if (path == "/api/v1/fhss/config") return "GET, PATCH";
  if (path == "/api/v1/fhss/config/validate" ||
      path == "/api/v1/fhss/config/rebuild" ||
      path == "/api/v1/fhss/config/discard" ||
      path == "/api/v1/fhss/config/undo" ||
      path == "/api/v1/fhss/config/export" ||
      path == "/api/v1/fhss/commands/start" ||
      path == "/api/v1/fhss/commands/stop") return "POST";
  if (StartsWith(path, "/api/v1/fhss/operations/")) {
    return path.ends_with("/cancel") ? "POST" : "GET, DELETE";
  }
  return std::nullopt;
}

bool MethodAllowed(const std::string &method, const std::string &allow) {
  std::size_t begin = 0;
  while (begin < allow.size()) {
    auto end = allow.find(',', begin);
    auto item = allow.substr(begin, end == std::string::npos ? end : end - begin);
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    if (item == method) return true;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return false;
}

std::optional<std::string> ValidateJsonStructure(
    const std::string &body, const EmbeddedDashboardServer::Options &options) {
  bool limit_exceeded = false;
  std::size_t members = 0;
  std::vector<std::unordered_set<std::string>> object_keys(options.max_json_depth + 1);
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
      if (parsed.is_string() &&
          parsed.get_ref<const std::string &>().size() > options.max_json_string_bytes) {
        limit_exceeded = true;
        return false;
      }
      if (event == nlohmann::json::parse_event_t::key && parsed.is_string() &&
          !object_keys[static_cast<std::size_t>(depth)]
               .insert(parsed.get_ref<const std::string &>()).second) {
        limit_exceeded = true;
        return false;
      }
      if (parsed.is_number()) {
        const double number = parsed.get<double>();
        if (!std::isfinite(number) || std::abs(number) > options.max_json_number_magnitude) {
          limit_exceeded = true;
          return false;
        }
      }
    }
    return true;
  };
  const auto parsed = nlohmann::json::parse(body, callback, false);
  if (limit_exceeded) return "JSON structural limit exceeded";
  if (parsed.is_discarded()) return "request body must be valid JSON";
  return std::nullopt;
}

} // namespace

EmbeddedDashboardServer::EmbeddedDashboardServer(
    Options options, std::shared_ptr<GraphConfigurationService> configuration_service,
    std::shared_ptr<GraphRuntimeSession> runtime_session,
  std::shared_ptr<GraphSnapshotCollector> snapshot_collector)
    : options_(std::move(options)),
      configuration_service_(std::move(configuration_service)),
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
    server_state_->acceptor->listen(
        static_cast<int>(std::min<std::size_t>(options_.max_concurrent_connections,
                                               128)));
    const auto local = server_state_->acceptor->local_endpoint();
    bound_port_ = local.port();
    bound_host_ = local.address().to_string();
    if (options_.application_api_handler) {
      const auto worker_count = std::min<std::size_t>(options_.max_concurrent_connections, 4);
      for (std::size_t index = 0; index < worker_count; ++index) {
        server_state_->handler_workers.emplace_back([state = server_state_.get()](std::stop_token pool_stop) {
          for (;;) {
            std::shared_ptr<ServerState::HandlerJob> job;
            {
              std::unique_lock lock(state->handler_mutex);
              state->handler_cv.wait(lock, pool_stop, [&] {
                return state->handler_stopping || !state->handler_queue.empty();
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
                  job->request, ApiContext{.deadline = job->deadline,
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
    last_error_ = std::string{"failed to bind dashboard socket: "} + error.code().message();
    server_state_.reset();
    return false;
  } catch (const std::exception &) {
    last_error_ = "failed to initialize dashboard socket";
    server_state_.reset();
    return false;
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

  if (server_state_ && server_state_->acceptor) {
    boost::system::error_code ignored;
    server_state_->acceptor->cancel(ignored);
    server_state_->acceptor->close(ignored);
  }

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  if (server_state_) {
    {
      std::scoped_lock lock(server_state_->handler_mutex);
      server_state_->handler_stopping = true;
      for (const auto &job : server_state_->handler_queue) job->stop_source.request_stop();
      for (const auto &job : server_state_->active_handler_jobs) job->stop_source.request_stop();
    }
    for (auto &worker : server_state_->handler_workers) worker.request_stop();
    server_state_->handler_cv.notify_all();
    server_state_->handler_workers.clear();
    std::scoped_lock lock(server_state_->workers_mutex);
    for (auto &worker : server_state_->workers) {
      if (worker.thread.joinable()) {
        worker.thread.join();
      }
    }
    server_state_->workers.clear();
    server_state_.reset();
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

const std::string &EmbeddedDashboardServer::BoundHost() const { return bound_host_; }

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
  boost::system::error_code address_error;
  const auto address = asio::ip::make_address(options_.host, address_error);
  if (address_error || !address.is_loopback()) {
    last_error_ = "dashboard host must be an explicit IPv4 or IPv6 loopback address";
    return false;
  }
  if (options_.max_header_bytes < 1024 || options_.max_body_bytes == 0 ||
      options_.max_response_bytes == 0 || options_.max_concurrent_connections == 0 ||
      options_.max_json_depth == 0 || options_.max_json_members == 0 ||
      options_.max_json_string_bytes == 0 ||
      !std::isfinite(options_.max_json_number_magnitude) ||
      options_.max_json_number_magnitude <= 0.0 ||
      options_.max_concurrent_connections > 128 || options_.idle_timeout.count() <= 0 ||
      options_.read_timeout.count() <= 0 || options_.write_timeout.count() <= 0 ||
      options_.total_request_timeout.count() <= 0) {
    last_error_ = "invalid dashboard resource limits";
    return false;
  }
  if (options_.application_api_handler &&
      (!options_.application_api_handler->handler ||
       !options_.application_api_handler->cooperative_cancellation ||
       options_.application_api_handler->maximum_checkpoint_latency.count() <= 0 ||
       options_.application_api_handler->maximum_checkpoint_latency >
           options_.total_request_timeout)) {
    last_error_ = "application handler must declare cooperative cancellation and a bounded checkpoint latency";
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
    boost::system::error_code accept_error;
    tcp::socket socket(server_state_->context);
    server_state_->acceptor->accept(socket, accept_error);
    if (accept_error) {
      if (!running_.load()) return;
      continue;
    }
    if (server_state_->active_connections.load() >= options_.max_concurrent_connections) {
      boost::system::error_code ignored;
      socket.close(ignored);
      continue;
    }
    server_state_->active_connections.fetch_add(1);
    std::scoped_lock lock(server_state_->workers_mutex);
    for (auto worker = server_state_->workers.begin(); worker != server_state_->workers.end();) {
      if (worker->done->load()) {
        if (worker->thread.joinable()) worker->thread.join();
        worker = server_state_->workers.erase(worker);
      } else {
        ++worker;
      }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    server_state_->workers.push_back(ServerState::Worker{
      .thread = std::thread([this, socket = std::move(socket), done]() mutable {
      const auto request_started = std::chrono::steady_clock::now();
      const auto request_deadline = request_started + options_.total_request_timeout;
      asio::io_context request_context;
      const auto protocol = socket.local_endpoint().protocol();
      tcp::socket request_socket(request_context, protocol, socket.release());
      beast::tcp_stream stream(std::move(request_socket));
      beast::flat_buffer buffer(options_.max_header_bytes + options_.max_body_bytes);
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
        if (read_finished || timeout_kind != ReadTimeoutKind::None) return;
        timeout_kind = kind;
        boost::system::error_code ignored;
        stream.socket().cancel(ignored);
      };
      std::function<void()> arm_idle_timer;
      arm_idle_timer = [&] {
        const auto generation = ++idle_generation;
        idle_timer.expires_after(options_.idle_timeout);
        idle_timer.async_wait([&, generation](boost::system::error_code error) {
          if (!error && generation == idle_generation) expire_read(ReadTimeoutKind::Idle);
        });
      };
      absolute_read_timer.expires_after(options_.read_timeout);
      absolute_read_timer.async_wait([&](boost::system::error_code error) {
        if (!error) expire_read(ReadTimeoutKind::Read);
      });
      total_timer.expires_at(request_deadline);
      total_timer.async_wait([&](boost::system::error_code error) {
        if (!error) expire_read(ReadTimeoutKind::Total);
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
        stream.async_read_some(buffer.prepare(std::min<std::size_t>(4096, available)),
                               [&](boost::system::error_code error, std::size_t bytes) {
          if (error) {
            read_error = error;
            read_finished = true;
          } else {
            buffer.commit(bytes);
            boost::system::error_code parse_error;
            while (!parser.is_done() && buffer.size() != 0) {
              const auto consumed = parser.put(buffer.data(), parse_error);
              buffer.consume(consumed);
              if (parse_error == http::error::need_more) {
                parse_error.clear();
                break;
              }
              if (parse_error || consumed == 0) break;
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
      if (timeout_kind != ReadTimeoutKind::None) read_error = beast::error::timeout;

      Response response;
      if (read_error) {
        int status = 400;
        std::string code = "bad_request";
        if (read_error == http::error::header_limit) { status = 431; code = "headers_too_large"; }
        if (read_error == http::error::body_limit) { status = 413; code = "body_too_large"; }
        if (read_error == beast::error::timeout) {
          status = 408;
          if (timeout_kind == ReadTimeoutKind::Idle) code = "idle_timeout";
          if (timeout_kind == ReadTimeoutKind::Read) code = "read_timeout";
          if (timeout_kind == ReadTimeoutKind::Total) code = "total_request_timeout";
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
        if (query != std::string::npos) request.query = request.target.substr(query + 1);
        request.body = wire_request.body();
        request.deadline = request_deadline;
        try {
          if (!request.body.empty() && request.method != "GET") {
            if (const auto json_error = ValidateJsonStructure(request.body, options_)) {
              response = {.status_code = 400,
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
                      .body = ErrorBody(400, "invalid_request", "request JSON has an invalid type or value")};
        } catch (const std::invalid_argument &) {
          response = {.status_code = 400,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(400, "invalid_request", "request value is invalid")};
        } catch (const std::exception &) {
          response = {.status_code = 500,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(500, "internal_error", "request processing failed")};
        }
      }
      if (response.status_code >= 400) {
        const auto existing = nlohmann::json::parse(response.body, nullptr, false);
        if (existing.is_discarded() || !existing.contains("type") ||
            !existing.contains("title") || !existing.contains("status") ||
            !existing.contains("detail")) {
          if (existing.is_object()) {
            auto normalized = existing;
            const auto code = normalized.value("code", std::string{"request_failed"});
            const auto detail = normalized.value("message", std::string{"request failed"});
            normalized["type"] = "urn:graphx:dashboard:problem:" + code;
            normalized["title"] = code;
            normalized["status"] = response.status_code;
            normalized["detail"] = detail;
            response.body = normalized.dump();
          } else {
            response.body = ErrorBody(response.status_code, "request_failed", "request failed");
          }
        }
        response.content_type = "application/problem+json";
      }
      if (response.body.size() > options_.max_response_bytes) {
        response = {.status_code = 500,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(500, "response_too_large", "response exceeded configured limit")};
      }
      http::response<http::string_body> wire_response{
          static_cast<http::status>(response.status_code), 11};
      wire_response.set(http::field::server, "GraphX-FHSS-Dashboard");
      wire_response.set(http::field::content_type, response.content_type);
      wire_response.set(http::field::cache_control, "no-store");
      wire_response.set("Content-Security-Policy",
                        "default-src 'self'; "
                        "script-src 'self' 'sha256-luWG0AGUtqv/EWxe142RXtxEPXV4rlPnayE78rvdmoA='; "
                        "style-src 'self' 'sha256-jQHa+rRNVQ/8YXMcwyAcWI7ncLkCpER9euD0l5x6S3Q='; "
                        "img-src 'self' data:; connect-src 'self'; object-src 'none'; "
                        "base-uri 'none'; frame-ancestors 'none'");
      wire_response.set("X-Content-Type-Options", "nosniff");
      wire_response.set("Referrer-Policy", "no-referrer");
      wire_response.set("X-Frame-Options", "DENY");
      if (response.status_code == 405) wire_response.set(http::field::allow, response.allow);
      wire_response.keep_alive(false);
      wire_response.body() = std::move(response.body);
      wire_response.prepare_payload();
      stream.expires_at(std::min(request_deadline,
                                 std::chrono::steady_clock::now() + options_.write_timeout));
      boost::system::error_code write_error;
      http::write(stream, wire_response, write_error);
      boost::system::error_code ignored;
      stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
      server_state_->active_connections.fetch_sub(1);
      done->store(true);
    }), .done = std::move(done)});
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

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleRequest(const Request &request) const {
  if (!options_.enable_mutating_routes && request.method != "GET" &&
      StartsWith(request.path, "/api/v1/fhss/")) {
    return Response{.status_code = 404,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }
  if (const auto allow = AllowedMethodsFor(request.path);
      allow && !MethodAllowed(request.method, *allow)) {
    return Response{.status_code = 405,
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

  if (request.method == "PATCH" || request.method == "POST" || request.method == "DELETE") {
    return HandleApiRequest(request);
  }

  return Response{.status_code = 405,
                  .content_type = "application/problem+json",
                  .body = ErrorBody(405, "method_not_allowed", "method not allowed"),
                  .allow = request.path.rfind("/api/", 0) == 0 ? "GET, POST, PATCH, DELETE" : "GET"};
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
    if (!ready) {
      return Response{.status_code = 503,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(503, "not_ready", "dashboard runtime is not ready")};
    }
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

  if (request.path == "/api/v1/fhss/events" && request.method == "GET") {
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

  if (request.path == "/api/v1/fhss/graph") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetGraphResponse())};
  }

  if (request.path == "/api/v1/fhss/config") {
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

  if (request.path == "/api/v1/fhss/config/authoritative") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetScenarioResponse())};
  }

  if (request.path == "/api/v1/fhss/config/effective") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetConfigResponse())};
  }

  if (request.path == "/api/v1/fhss/config/derived-paths") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->GetDerivedPathsResponse())};
  }

  if (request.path == "/api/v1/fhss/config/value") {
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

  if (StartsWith(request.path, "/api/v1/fhss/nodes/")) {
    const auto node_suffix = request.path.substr(std::string{"/api/v1/fhss/nodes/"}.size());
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

  if (request.path == "/api/v1/fhss/metrics") {
    const auto metrics_snapshot = snapshot_collector_->GetMetricsSnapshot();
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(metrics_snapshot)};
  }

  if (request.path == "/api/v1/fhss/metrics/edges") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(snapshot_collector_->GetEdgeMetricsSnapshot())};
  }

  if (request.path == "/api/v1/fhss/diagnostics") {
    const auto diagnostics_snapshot = snapshot_collector_->GetDiagnosticsSnapshot();
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(diagnostics_snapshot)};
  }

  if (request.path == "/api/v1/fhss/config/validate" && request.method == "POST") {
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

  if (request.path == "/api/v1/fhss/config/rebuild" && request.method == "POST") {
    const auto body = request.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded()) {
      return Response{.status_code = 400,
                      .content_type = "application/json",
                      .body = ErrorBody(400, "invalid_json", "request body must be JSON")};
    }

    const auto expected_revision = body.value("expected_revision", configuration_service_->ConfigRevision());
    if (expected_revision != configuration_service_->ConfigRevision()) {
      return Response{.status_code = 409,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(409, "stale_revision_conflict",
                                        "expected revision does not match current revision")};
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

  if (request.path == "/api/v1/fhss/status" && request.method == "GET") {
    const auto status = RuntimeStatusJson(runtime_session_->SnapshotStatus());
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(status)};
  }

  if (request.path == "/api/v1/fhss/commands/start" && request.method == "POST") {
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

  if (request.path == "/api/v1/fhss/commands/stop" && request.method == "POST") {
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

  if (request.path == "/api/v1/fhss/config/discard" && request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->DiscardEdits())};
  }

  if (request.path == "/api/v1/fhss/config/undo" && request.method == "POST") {
    return Response{.status_code = 200,
                    .content_type = "application/json",
                    .body = JsonResponse(configuration_service_->UndoLastEdit())};
  }

  if (request.path == "/api/v1/fhss/config/export" && request.method == "POST") {
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

  if (StartsWith(request.path, "/api/v1/fhss/operations/")) {
    const auto operation_suffix = request.path.substr(std::string{"/api/v1/fhss/operations/"}.size());
    const std::string cancel_suffix = "/cancel";
    if (request.method == "POST" &&
        operation_suffix.size() > cancel_suffix.size() &&
        operation_suffix.rfind(cancel_suffix) == operation_suffix.size() - cancel_suffix.size()) {
      const auto operation_id = operation_suffix.substr(0, operation_suffix.size() - cancel_suffix.size());
      const auto result = configuration_service_->CancelOperation(operation_id);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 409)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "GET") {
      const auto result = configuration_service_->GetOperationResponse(operation_suffix);
      const auto status = result.value("schema", std::string{}) == "graphx.dashboard.error.v1"
                              ? result.value("status", 404)
                              : 200;
      return Response{.status_code = status,
                      .content_type = "application/json",
                      .body = JsonResponse(result)};
    }
    if (request.method == "DELETE") {
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

  if (options_.application_api_handler) {
    if (std::chrono::steady_clock::now() >= request.deadline) {
      return Response{.status_code = 408,
                      .content_type = "application/problem+json",
                      .body = ErrorBody(408, "request_timeout", "request deadline exceeded")};
    }
    auto job = std::make_shared<ServerState::HandlerJob>();
    job->handler = options_.application_api_handler->handler;
    job->request = ApiRequest{.method = request.method,
                              .path = request.path,
                              .query = request.query,
                              .body = request.body};
    job->deadline = request.deadline;
    auto future = job->completion.get_future();
    {
      std::scoped_lock lock(server_state_->handler_mutex);
      const auto outstanding = server_state_->handler_queue.size() +
                               server_state_->active_handler_jobs.size();
      if (server_state_->handler_stopping ||
          outstanding >= options_.max_concurrent_connections) {
        return Response{.status_code = 503,
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
                      .body = ErrorBody(408, "request_timeout", "application handler deadline exceeded")};
    }
    const auto application_response = future.get();
    if (application_response) {
      return Response{.status_code = application_response->status_code,
                      .content_type = application_response->content_type,
                      .body = application_response->body};
    }
  }

  return Response{.status_code = 404,
                  .content_type = "application/json",
                  .body = ErrorBody(404, "not_found", "resource not found")};
}

EmbeddedDashboardServer::Response
EmbeddedDashboardServer::HandleStaticAsset(const Request &request) const {
  const std::string encoded_target = request.path == "/" ? "/index.html" : request.path;
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
  const auto canonical_root = std::filesystem::weakly_canonical(options_.asset_directory, error);
  const auto canonical_file = std::filesystem::weakly_canonical(resolved, error);
  if (error || !IsContainedPath(canonical_root, canonical_file) ||
      !std::filesystem::is_regular_file(canonical_file, error)) {
    return Response{.status_code = 404,
                    .content_type = "application/json",
                    .body = ErrorBody(404, "not_found", "resource not found")};
  }
  const auto asset = ReadAssetWithoutFollowingLinks(options_.asset_directory, relative,
                                                    options_.max_response_bytes);
  if (asset.too_large) {
    return Response{.status_code = 413,
                    .content_type = "application/problem+json",
                    .body = ErrorBody(413, "asset_too_large", "static asset exceeds response limit")};
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
                        {"stream", "/api/v1/fhss/events"},
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
