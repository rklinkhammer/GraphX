/**
 * @file GraphHttpServer.cpp
 * @brief Generic GraphX graph-management HTTP server.
 */

#include "graph/GraphHttpServer.hpp"

#include "capabilities/CommandCapability.hpp"
#include "capabilities/MetricsCapability.hpp"
#include "graph/ExecutionState.hpp"
#include "graph/GraphCoordinator.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <concepts>
#include <condition_variable>
#include <ctime>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

constexpr std::size_t kMaximumHeaderBytes = 64U * 1024U;
constexpr std::size_t kMaximumBodyBytes = 1024U * 1024U;
constexpr std::size_t kMaximumStaticPathBytes = 2048U;
constexpr int kMaximumPendingConnections = 16;
constexpr std::size_t kRequestWorkerCount =
    graph::GraphHttpServer::RequestWorkerLimit();
constexpr std::size_t kPendingRequestCount =
    graph::GraphHttpServer::PendingRequestLimit();
constexpr std::size_t kMaximumMetricValues = 4096U;
constexpr std::size_t kMaximumMetricsBodyBytes = 1024U * 1024U;
constexpr auto kMetricStaleAfter = std::chrono::seconds(3);

using MetricsCallbackObservation =
    graph::GraphHttpServer::MetricsCallbackObservation;
thread_local MetricsCallbackObservation* active_metrics_callback = nullptr;

class MetricsCallbackScope {
public:
  explicit MetricsCallbackScope(MetricsCallbackObservation& observation)
      : prior_(std::exchange(active_metrics_callback, &observation)) {}
  ~MetricsCallbackScope() { active_metrics_callback = prior_; }
private:
  MetricsCallbackObservation* prior_;
};

void ObserveCallbackBoundary(
    std::size_t MetricsCallbackObservation::*counter) noexcept {
  if (active_metrics_callback != nullptr) {
    ++(active_metrics_callback->*counter);
  }
}

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body{};
  std::vector<std::pair<std::string, std::string>> headers{};
};

std::string ReasonPhrase(const int status) {
  switch (status) {
  case 200:
    return "OK";
  case 202:
    return "Accepted";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 409:
    return "Conflict";
  case 413:
    return "Payload Too Large";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 503:
    return "Service Unavailable";
  default:
    return "Error";
  }
}

std::string Lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char byte) {
    return static_cast<char>(std::tolower(byte));
  });
  return value;
}

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

bool IsValidUtf8(const std::string_view text) {
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7FU) {
      ++index;
      continue;
    }
    std::size_t trailing = 0U;
    std::uint32_t code_point = 0U;
    std::uint32_t minimum = 0U;
    if ((first & 0xE0U) == 0xC0U) {
      trailing = 1U; code_point = first & 0x1FU; minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
      trailing = 2U; code_point = first & 0x0FU; minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
      trailing = 3U; code_point = first & 0x07U; minimum = 0x10000U;
    } else {
      return false;
    }
    if (index + trailing >= text.size()) return false;
    for (std::size_t offset = 1U; offset <= trailing; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) return false;
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += trailing + 1U;
  }
  return true;
}

std::optional<std::string> DecodeStaticPath(const std::string_view path) {
  if (path.empty() || path.front() != '/' ||
      path.size() > kMaximumStaticPathBytes) {
    return std::nullopt;
  }
  std::string decoded;
  decoded.reserve(path.size());
  const auto hex_value = [](const char digit) -> std::optional<unsigned int> {
    if (digit >= '0' && digit <= '9') {
      return static_cast<unsigned int>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
      return static_cast<unsigned int>(digit - 'a' + 10);
    }
    if (digit >= 'A' && digit <= 'F') {
      return static_cast<unsigned int>(digit - 'A' + 10);
    }
    return std::nullopt;
  };
  for (std::size_t index = 0; index < path.size(); ++index) {
    if (path[index] != '%') {
      decoded.push_back(path[index]);
      continue;
    }
    if (index + 2U >= path.size()) {
      return std::nullopt;
    }
    const auto high = hex_value(path[index + 1U]);
    const auto low = hex_value(path[index + 2U]);
    if (!high || !low) {
      return std::nullopt;
    }
    const auto byte = static_cast<char>((*high << 4U) | *low);
    if (byte == '\0') {
      return std::nullopt;
    }
    decoded.push_back(byte);
    index += 2U;
  }
  if (decoded.find('\\') != std::string::npos) {
    return std::nullopt;
  }
  return decoded;
}

bool IsContainedPath(const std::filesystem::path &root,
                     const std::filesystem::path &candidate) {
  const auto relative = candidate.lexically_relative(root);
  if (relative.empty() || relative.is_absolute()) {
    return false;
  }
  return std::ranges::none_of(relative, [](const auto &component) {
    return component == "..";
  });
}

std::string StaticContentType(const std::filesystem::path &path) {
  const auto extension = Lowercase(path.extension().string());
  if (extension == ".html") {
    return "text/html; charset=utf-8";
  }
  if (extension == ".js" || extension == ".mjs") {
    return "application/javascript; charset=utf-8";
  }
  if (extension == ".css") {
    return "text/css; charset=utf-8";
  }
  if (extension == ".json" || extension == ".map") {
    return "application/json";
  }
  if (extension == ".svg") {
    return "image/svg+xml";
  }
  if (extension == ".woff2") {
    return "font/woff2";
  }
  return "application/octet-stream";
}

std::filesystem::path
SelectStaticResourceRoot(const std::string &explicit_index_path) {
  std::error_code error;
  if (!explicit_index_path.empty()) {
    const auto parent =
        std::filesystem::path{explicit_index_path}.parent_path();
    const auto canonical = std::filesystem::weakly_canonical(parent, error);
    return error ? parent.lexically_normal() : canonical;
  }
#ifdef GRAPHX_GENERIC_DASHBOARD_SOURCE_ROOT
  for (const auto &candidate :
       {std::filesystem::path{GRAPHX_GENERIC_DASHBOARD_SOURCE_ROOT},
        std::filesystem::path{GRAPHX_GENERIC_DASHBOARD_INSTALL_ROOT}}) {
    if (!std::filesystem::is_regular_file(candidate / "index.html", error) ||
        error) {
      error.clear();
      continue;
    }
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (!error) {
      return canonical;
    }
    error.clear();
  }
#endif
  return {};
}

std::optional<std::size_t> ParseContentLength(const std::string &headers) {
  std::istringstream stream(headers);
  std::string line;
  std::getline(stream, line);
  while (std::getline(stream, line)) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    if (Lowercase(Trim(line.substr(0, separator))) != "content-length") {
      continue;
    }
    const auto value = Trim(line.substr(separator + 1U));
    std::size_t length = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), length);
    if (result.ec != std::errc{} ||
        result.ptr != value.data() + value.size()) {
      return std::nullopt;
    }
    return length;
  }
  return std::size_t{0};
}

bool SendAll(const int socket_fd, const std::string &bytes) {
  ObserveCallbackBoundary(&MetricsCallbackObservation::socket_operations);
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    const auto result = ::send(socket_fd, bytes.data() + sent,
                               bytes.size() - sent, flags);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

HttpResponse JsonResponse(const int status, nlohmann::json document) {
  ObserveCallbackBoundary(&MetricsCallbackObservation::http_responses);
  ObserveCallbackBoundary(&MetricsCallbackObservation::json_serializations);
  return HttpResponse{
      .status = status,
      .content_type = "application/json",
      .body = document.dump(),
  };
}

HttpResponse ErrorResponse(const int status, const std::string &code,
                           const std::string &message) {
  return JsonResponse(status,
                      {{"success", false},
                       {"error", code},
                       {"message", message}});
}

std::string EncodeIdentityPart(const std::string_view text) {
  return std::to_string(text.size()) + ":" + std::string{text};
}

std::optional<std::string> MetricTargetKey(
    const app::metrics::MetricTarget& target) {
  if (target.kind == app::metrics::MetricTarget::Kind::Node) {
    if (target.node_id.empty() || target.node_id.size() > 256U) {
      return std::nullopt;
    }
    return "node|" + EncodeIdentityPart(target.node_id);
  }
  const auto port = [](const std::string& kind,
                       const auto& value) -> std::optional<std::string> {
    if (kind == "index" && std::holds_alternative<std::uint64_t>(value)) {
      return "index:" +
             std::to_string(std::get<std::uint64_t>(value));
    }
    if (kind == "name" && std::holds_alternative<std::string>(value)) {
      const auto& name = std::get<std::string>(value);
      if (!name.empty() && name.size() <= 128U) {
        return "name:" + EncodeIdentityPart(name);
      }
    }
    return std::nullopt;
  };
  const auto source_port = port(target.source_port_kind, target.source_port);
  const auto target_port = port(target.target_port_kind, target.target_port);
  if (target.source_node_id.empty() || target.source_node_id.size() > 256U ||
      target.target_node_id.empty() || target.target_node_id.size() > 256U ||
      !source_port || !target_port) {
    return std::nullopt;
  }
  return "edge|" + EncodeIdentityPart(target.source_node_id) + "|" +
         *source_port + "|" + EncodeIdentityPart(target.target_node_id) +
         "|" + *target_port;
}

nlohmann::json MetricTargetJson(const app::metrics::MetricTarget& target) {
  if (target.kind == app::metrics::MetricTarget::Kind::Node) {
    return {{"kind", "node"}, {"node_id", target.node_id}};
  }
  const auto port_json = [](const std::string& kind, const auto& value) {
    nlohmann::json result{{"kind", kind}};
    std::visit([&result](const auto& typed) { result["value"] = typed; }, value);
    return result;
  };
  return {{"kind", "edge"},
          {"source_node_id", target.source_node_id},
          {"source_port", port_json(target.source_port_kind, target.source_port)},
          {"target_node_id", target.target_node_id},
          {"target_port", port_json(target.target_port_kind, target.target_port)}};
}

std::string Rfc3339(
    const std::chrono::system_clock::time_point timestamp) {
  const auto seconds =
      std::chrono::time_point_cast<std::chrono::seconds>(timestamp);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      timestamp - seconds).count();
  const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
  std::tm utc{};
  gmtime_r(&raw, &utc);
  std::array<char, 32> buffer{};
  std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &utc);
  std::ostringstream result;
  result << buffer.data() << '.' << std::setfill('0') << std::setw(3)
         << milliseconds << 'Z';
  return result.str();
}

struct CounterDelta {
  bool numeric{false};
  bool increasing{false};
  long double value{0.0L};
};

CounterDelta ExactCounterDelta(const app::metrics::MetricScalar& previous,
                               const app::metrics::MetricScalar& current) {
  return std::visit([](const auto& left, const auto& right) -> CounterDelta {
    using Left = std::decay_t<decltype(left)>;
    using Right = std::decay_t<decltype(right)>;
    if constexpr (!std::same_as<Left, Right> ||
                  !(std::same_as<Left, std::int64_t> ||
                    std::same_as<Left, std::uint64_t> ||
                    std::same_as<Left, double>)) {
      return {};
    } else {
      if constexpr (std::same_as<Left, double>) {
        if (!std::isfinite(left) || !std::isfinite(right)) return {};
      }
      if (right <= left) return {.numeric = true};
      if constexpr (std::same_as<Left, std::int64_t>) {
        const auto magnitude = [](const std::int64_t value) {
          return value >= 0
              ? static_cast<std::uint64_t>(value)
              : static_cast<std::uint64_t>(-(value + 1)) + 1U;
        };
        const std::uint64_t delta = left < 0
            ? right < 0
                ? magnitude(left) - magnitude(right)
                : magnitude(left) + static_cast<std::uint64_t>(right)
            : static_cast<std::uint64_t>(right) -
                  static_cast<std::uint64_t>(left);
        return {.numeric = true, .increasing = true,
                .value = static_cast<long double>(delta)};
      } else {
        // Unsigned subtraction occurs before conversion, retaining exact
        // deltas for counters above 2^53.
        return {.numeric = true, .increasing = true,
                .value = static_cast<long double>(right - left)};
      }
    }
  }, previous, current);
}

class SimpleHttpServer {
public:
  using RequestHandler =
      std::function<HttpResponse(const std::string &, const std::string &,
                                 const std::string &)>;

  explicit SimpleHttpServer(const int port) : port_(port) {}
  ~SimpleHttpServer() { Stop(); }

  bool Start(RequestHandler handler) {
    if (port_ < 1 || port_ > 65535) {
      return false;
    }
    if (running_.load(std::memory_order_acquire) ||
        server_thread_.joinable()) {
      return false;
    }
    handler_ = std::move(handler);
    {
      std::scoped_lock lock(worker_mutex_);
      worker_shutdown_ = false;
      pending_clients_.clear();
      rejected_requests_.store(0U, std::memory_order_release);
      worker_threads_.reserve(kRequestWorkerCount);
      for (std::size_t index = 0; index < kRequestWorkerCount; ++index) {
        worker_threads_.emplace_back([this] { RunWorker(); });
      }
    }
    {
      std::scoped_lock lock(startup_mutex_);
      startup_complete_ = false;
      startup_succeeded_ = false;
    }
    server_thread_ = std::thread([this] { RunServer(); });
    std::unique_lock lock(startup_mutex_);
    startup_condition_.wait(lock, [this] { return startup_complete_; });
    const bool succeeded = startup_succeeded_;
    lock.unlock();
    if (!succeeded && server_thread_.joinable()) {
      server_thread_.join();
    }
    if (!succeeded) {
      StopWorkers();
    }
    return succeeded;
  }

  bool Stop() {
    running_.store(false, std::memory_order_release);
    const int listen_fd = listen_socket_.exchange(-1);
    if (listen_fd >= 0) {
      ::shutdown(listen_fd, SHUT_RDWR);
      ::close(listen_fd);
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }

    std::vector<int> clients;
    {
      std::scoped_lock lock(client_mutex_);
      clients.assign(client_sockets_.begin(), client_sockets_.end());
    }
    for (const int client : clients) {
      ::shutdown(client, SHUT_RDWR);
    }

    StopWorkers();
    return true;
  }

  [[nodiscard]] bool IsRunning() const {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t RetainedWorkerCount() const {
    std::scoped_lock lock(worker_mutex_);
    return worker_threads_.size();
  }

  [[nodiscard]] std::size_t ActiveRequestCount() const {
    return active_requests_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t PendingRequestCount() const {
    std::scoped_lock lock(worker_mutex_);
    return pending_clients_.size();
  }

  [[nodiscard]] std::size_t RejectedRequestCount() const {
    return rejected_requests_.load(std::memory_order_acquire);
  }

private:
  void CompleteStartup(const bool succeeded) {
    {
      std::scoped_lock lock(startup_mutex_);
      startup_succeeded_ = succeeded;
      startup_complete_ = true;
    }
    startup_condition_.notify_all();
  }

  void RunServer() {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
      CompleteStartup(false);
      return;
    }
    listen_socket_.store(socket_fd);

    int reuse = 1;
    static_cast<void>(
        ::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse,
                     static_cast<socklen_t>(sizeof(reuse))));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port_));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket_fd, reinterpret_cast<sockaddr *>(&address),
               sizeof(address)) < 0 ||
        ::listen(socket_fd, kMaximumPendingConnections) < 0) {
      const int owned_fd = listen_socket_.exchange(-1);
      if (owned_fd >= 0) {
        ::close(owned_fd);
      }
      CompleteStartup(false);
      return;
    }

    running_.store(true, std::memory_order_release);
    CompleteStartup(true);
    while (running_.load(std::memory_order_acquire)) {
      sockaddr_in client_address{};
      socklen_t length = sizeof(client_address);
      const int client =
          ::accept(socket_fd, reinterpret_cast<sockaddr *>(&client_address),
                   &length);
      if (client < 0) {
        if (!running_.load(std::memory_order_acquire)) {
          break;
        }
        continue;
      }
#ifdef SO_NOSIGPIPE
      int no_sigpipe = 1;
      static_cast<void>(
          ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                       static_cast<socklen_t>(sizeof(no_sigpipe))));
#endif
      {
        std::scoped_lock lock(client_mutex_);
        client_sockets_.insert(client);
      }
      bool admitted = false;
      {
        std::scoped_lock lock(worker_mutex_);
        if (!worker_shutdown_ &&
            pending_clients_.size() < kPendingRequestCount) {
          pending_clients_.push_back(client);
          admitted = true;
        }
      }
      if (!admitted) {
        rejected_requests_.fetch_add(1U, std::memory_order_acq_rel);
        {
          std::scoped_lock lock(client_mutex_);
          client_sockets_.erase(client);
        }
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
        continue;
      }
      worker_condition_.notify_one();
    }

    running_.store(false, std::memory_order_release);
    int expected = socket_fd;
    if (listen_socket_.compare_exchange_strong(expected, -1)) {
      ::close(socket_fd);
    }
  }

  void RunWorker() {
    while (true) {
      int client = -1;
      {
        std::unique_lock lock(worker_mutex_);
        worker_condition_.wait(lock, [this] {
          return worker_shutdown_ || !pending_clients_.empty();
        });
        if (worker_shutdown_ && pending_clients_.empty()) {
          return;
        }
        client = pending_clients_.front();
        pending_clients_.pop_front();
      }

      active_requests_.fetch_add(1U, std::memory_order_acq_rel);
      try {
        if (!HandleClient(client)) {
          ::shutdown(client, SHUT_RDWR);
        }
      } catch (...) {
        if (!WriteResponse(client,
                           ErrorResponse(500, "internal_error",
                                         "Unexpected server error"))) {
          ::shutdown(client, SHUT_RDWR);
        }
      }
      {
        std::scoped_lock client_lock(client_mutex_);
        client_sockets_.erase(client);
      }
      ::shutdown(client, SHUT_RDWR);
      ::close(client);
      active_requests_.fetch_sub(1U, std::memory_order_acq_rel);
    }
  }

  void StopWorkers() {
    std::vector<std::thread> workers;
    {
      std::scoped_lock lock(worker_mutex_);
      worker_shutdown_ = true;
      workers = std::move(worker_threads_);
    }
    worker_condition_.notify_all();
    for (auto &worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    {
      std::scoped_lock lock(worker_mutex_);
      pending_clients_.clear();
    }
  }

  [[nodiscard]] bool HandleClient(const int socket_fd) const {
    timeval timeout{.tv_sec = 5, .tv_usec = 0};
    static_cast<void>(::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                  sizeof(timeout)));
    static_cast<void>(::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                                  sizeof(timeout)));

    std::string request;
    request.reserve(4096);
    std::array<char, 4096> buffer{};
    std::size_t header_end = std::string::npos;
    while ((header_end = request.find("\r\n\r\n")) == std::string::npos) {
      const auto bytes = ::recv(socket_fd, buffer.data(), buffer.size(), 0);
      if (bytes <= 0) {
        return false;
      }
      request.append(buffer.data(), static_cast<std::size_t>(bytes));
      if (request.size() > kMaximumHeaderBytes) {
        return WriteResponse(
            socket_fd,
            ErrorResponse(413, "headers_too_large",
                          "request headers exceed the limit"));
      }
    }

    const auto content_length =
        ParseContentLength(request.substr(0, header_end + 2U));
    if (!content_length) {
      return WriteResponse(
          socket_fd,
          ErrorResponse(400, "invalid_content_length",
                        "Content-Length must be an unsigned integer"));
    }
    if (*content_length > kMaximumBodyBytes) {
      return WriteResponse(
          socket_fd,
          ErrorResponse(413, "body_too_large",
                        "request body exceeds the limit"));
    }

    const auto body_offset = header_end + 4U;
    while (request.size() - body_offset < *content_length) {
      const auto bytes = ::recv(socket_fd, buffer.data(), buffer.size(), 0);
      if (bytes <= 0) {
        return WriteResponse(
            socket_fd,
            ErrorResponse(400, "incomplete_body",
                          "request body ended before Content-Length"));
      }
      request.append(buffer.data(), static_cast<std::size_t>(bytes));
      if (request.size() - body_offset > kMaximumBodyBytes) {
        return WriteResponse(
            socket_fd,
            ErrorResponse(413, "body_too_large",
                          "request body exceeds the limit"));
      }
    }

    std::istringstream request_line_stream(
        request.substr(0, request.find("\r\n")));
    std::string method;
    std::string target;
    std::string version;
    request_line_stream >> method >> target >> version;
    if (method.empty() || target.empty() ||
        !version.starts_with("HTTP/1.")) {
      return WriteResponse(
          socket_fd,
          ErrorResponse(400, "invalid_request",
                        "invalid HTTP request line"));
    }
    const auto query = target.find('?');
    const std::string path = target.substr(0, query);
    const std::string body =
        request.substr(body_offset, *content_length);
    return WriteResponse(socket_fd, handler_(method, path, body));
  }

  [[nodiscard]] static bool WriteResponse(const int socket_fd,
                                          const HttpResponse &response) {
    std::ostringstream encoded;
    encoded << "HTTP/1.1 " << response.status << ' '
            << ReasonPhrase(response.status) << "\r\n"
            << "Content-Type: " << response.content_type << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n";
    for (const auto &[name, value] : response.headers) {
      encoded << name << ": " << value << "\r\n";
    }
    encoded << "\r\n" << response.body;
    return SendAll(socket_fd, encoded.str());
  }

  int port_;
  std::atomic<bool> running_{false};
  std::atomic<int> listen_socket_{-1};
  RequestHandler handler_;
  std::thread server_thread_;
  mutable std::mutex startup_mutex_;
  std::condition_variable startup_condition_;
  bool startup_complete_ = false;
  bool startup_succeeded_ = false;
  mutable std::mutex client_mutex_;
  std::set<int> client_sockets_;
  mutable std::mutex worker_mutex_;
  std::condition_variable worker_condition_;
  std::deque<int> pending_clients_;
  bool worker_shutdown_ = true;
  std::vector<std::thread> worker_threads_;
  std::atomic<std::size_t> active_requests_{0U};
  std::atomic<std::size_t> rejected_requests_{0U};
};

} // namespace

namespace graph {

class GraphHttpServer::Impl {
public:
  Impl(std::shared_ptr<GraphCoordinator> coordinator,
       std::shared_ptr<capabilities::CommandCapability> commands,
       std::shared_ptr<capabilities::MetricsCapability> metrics,
       const int port, std::string index_path)
      : coordinator_(std::move(coordinator)),
        commands_(std::move(commands)), metrics_(std::move(metrics)),
        resource_root_(SelectStaticResourceRoot(index_path)),
        server_(port) {
    if (!coordinator_ || !commands_ || !metrics_) {
      throw std::invalid_argument(
          "GraphHttpServer requires coordinator, command, and metrics capabilities");
    }
    RefreshAuthoritativeState();
  }

  ~Impl() noexcept {
    // The request handler captures this Impl. Join every request while all
    // handler-visible state is still alive, before member destruction begins.
    static_cast<void>(server_.Stop());
  }

  bool Start() {
    SynchronizeGeneration();
    return server_.Start(
        [this](const std::string &method, const std::string &path,
               const std::string &body) {
          return HandleRequest(method, path, body);
        });
  }

  bool Stop() { return server_.Stop(); }
  void Register(app::metrics::IMetricsSubscriber* subscriber) {
    ObserveCallbackBoundary(
        &MetricsCallbackObservation::capability_reentries);
    metrics_->RegisterMetricsCallback(subscriber);
  }
  void Unregister(app::metrics::IMetricsSubscriber* subscriber) {
    ObserveCallbackBoundary(
        &MetricsCallbackObservation::capability_reentries);
    metrics_->UnregisterMetricsCallback(subscriber);
  }
  void OnMetricsEvent(const app::metrics::MetricsEvent& event) noexcept {
    GraphHttpServer::MetricsCallbackObservation observation;
    MetricsCallbackScope callback_scope(observation);
    try {
      AcceptMetricsEvent(event, observation);
    } catch (...) {
      RecordLocalRejection(capabilities::MetricsRejectionCategory::Internal);
    }
    if (metrics_callback_observer_) {
      try {
        metrics_callback_observer_(observation);
      } catch (...) {
        // Test instrumentation must not change the noexcept subscriber path.
      }
    }
  }
  void OnMetricsGenerationReset(const std::uint64_t generation) noexcept {
    std::scoped_lock lock(metrics_mutex_);
    current_metrics_.clear();
    current_metrics_bytes_ = 0U;
    allowed_node_ids_.clear();
    allowed_edge_ids_.clear();
    allowed_metric_descriptors_.clear();
    current_metric_schemas_.clear();
    metrics_generation_ = generation;
    last_sample_time_.reset();
    ++snapshot_sequence_;
  }
  void OnMetricsSchemasChanged(
      const std::uint64_t generation,
      const std::vector<app::metrics::NodeMetricsSchema>& schemas) noexcept {
    try {
      // Schema installation is a control-plane transition serialized by the
      // capability. Prepare all topology/schema authority here so the event
      // callback remains bounded and never re-enters capability APIs.
      ApplyAuthoritativeState(coordinator_->Snapshot(), generation, schemas,
                              std::nullopt);
    } catch (...) {
      RecordLocalRejection(
          capabilities::MetricsRejectionCategory::SchemaContract);
    }
  }
  [[nodiscard]] bool IsRunning() const { return server_.IsRunning(); }
  [[nodiscard]] std::size_t RetainedRequestWorkerCount() const {
    return server_.RetainedWorkerCount();
  }
  void SetMetricsSnapshotEntryHookForTesting(std::function<void()> hook) {
    metrics_snapshot_entry_hook_ = std::move(hook);
  }
  void SetMetricsBodyLimitForTesting(const std::size_t bytes) {
    metrics_body_limit_ = bytes;
  }
  void SetMetricsCallbackObserverForTesting(
      std::function<void(const GraphHttpServer::MetricsCallbackObservation&)>
          observer) {
    metrics_callback_observer_ = std::move(observer);
  }
  GraphHttpServer::MetricsCallbackObservation
  ProbeMetricsCallbackBoundariesForTesting() {
    GraphHttpServer::MetricsCallbackObservation observation;
    MetricsCallbackScope callback_scope(observation);
    static_cast<void>(SendAll(-1, {}));
    static_cast<void>(JsonResponse(200, nlohmann::json::object()));
    Register(nullptr);
    return observation;
  }
  [[nodiscard]] std::size_t ActiveRequestCount() const {
    return server_.ActiveRequestCount();
  }
  [[nodiscard]] std::size_t PendingRequestCount() const {
    return server_.PendingRequestCount();
  }
  [[nodiscard]] std::size_t RejectedRequestCount() const {
    return server_.RejectedRequestCount();
  }

private:
  struct CurrentMetric {
    app::metrics::MetricTarget target;
    app::metrics::MetricSample sample;
    std::chrono::system_clock::time_point sample_time;
    std::optional<double> rate;
    std::string rate_reason{"not_enough_samples"};
    std::size_t encoded_bytes{0U};
  };

  struct MetricDescriptorContract {
    std::string scalar_type;
    std::string unit;
    std::string semantics;
    std::string aggregation;
    std::string availability_rule;
    bool operator==(const MetricDescriptorContract&) const = default;
  };

  static std::optional<app::metrics::MetricTarget> EdgeTargetFromDocument(
      const nlohmann::json& edge) {
    if (!edge.is_object() || !edge.contains("source_node_id") ||
        !edge["source_node_id"].is_string() ||
        !edge.contains("target_node_id") ||
        !edge["target_node_id"].is_string()) {
      return std::nullopt;
    }
    app::metrics::MetricTarget target;
    target.kind = app::metrics::MetricTarget::Kind::Edge;
    target.source_node_id = edge["source_node_id"].get<std::string>();
    target.target_node_id = edge["target_node_id"].get<std::string>();
    const auto parse_port = [&edge](const std::string& prefix,
                                    std::string& kind,
                                    auto& value) -> bool {
      const auto numeric = prefix + "_port";
      const auto named = prefix + "_port_name";
      if (edge.contains(numeric) == edge.contains(named)) {
        return false;
      }
      if (edge.contains(numeric) && edge[numeric].is_number_unsigned()) {
        kind = "index";
        value = edge[numeric].get<std::uint64_t>();
        return true;
      }
      if (edge.contains(named) && edge[named].is_string()) {
        kind = "name";
        value = edge[named].get<std::string>();
        return true;
      }
      return false;
    };
    if (!parse_port("source", target.source_port_kind, target.source_port) ||
        !parse_port("target", target.target_port_kind, target.target_port)) {
      return std::nullopt;
    }
    return target;
  }

  bool TargetIsAuthoritativeLocked(
      const app::metrics::MetricTarget& target,
      const std::string& target_key) const {
    return target.kind == app::metrics::MetricTarget::Kind::Node
               ? allowed_node_ids_.contains(target.node_id)
               : allowed_edge_ids_.contains(target_key);
  }

  void ApplyAuthoritativeState(
      const GraphConfigurationSnapshot& snapshot,
      const std::uint64_t generation,
      const std::vector<app::metrics::NodeMetricsSchema>& schemas,
      const std::optional<std::uint64_t> expected_authority_sequence) {
    std::set<std::string> node_ids;
    std::set<std::string> edge_ids;
    const auto& document = snapshot.Document();
    if (document.contains("nodes") && document["nodes"].is_array()) {
      for (const auto& node : document["nodes"]) {
        if (node.is_object() && node.contains("id") && node["id"].is_string()) {
          node_ids.insert(node["id"].get<std::string>());
        }
      }
    }
    if (document.contains("edges") && document["edges"].is_array()) {
      for (const auto& edge : document["edges"]) {
        if (const auto target = EdgeTargetFromDocument(edge)) {
          if (const auto key = MetricTargetKey(*target)) {
            edge_ids.insert(*key);
          }
        }
      }
    }
    std::map<std::string, MetricDescriptorContract> descriptors;
    for (const auto& schema : schemas) {
      const auto target_key = MetricTargetKey(schema.target);
      if (!target_key || schema.graph_generation != generation) {
        continue;
      }
      const bool authoritative =
          schema.target.kind == app::metrics::MetricTarget::Kind::Node
              ? node_ids.contains(schema.target.node_id)
              : edge_ids.contains(*target_key);
      if (!authoritative) {
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::AuthorityMismatch);
        continue;
      }
      for (const auto& descriptor : schema.descriptors) {
        const auto key = *target_key + "|metric|" +
            EncodeIdentityPart(descriptor.metric_id);
        const auto [unused, inserted] = descriptors.emplace(
            key, MetricDescriptorContract{
                .scalar_type = descriptor.scalar_type,
                .unit = descriptor.unit,
                .semantics = descriptor.semantics,
                .aggregation = descriptor.aggregation,
                .availability_rule = descriptor.availability_rule});
        (void)unused;
        if (!inserted) {
          RecordLocalRejection(
              capabilities::MetricsRejectionCategory::SchemaContract);
        }
      }
    }
    std::unique_lock lock(metrics_mutex_);
    // A reset can complete after the snapshots above but before this lock.
    // Never let that stale snapshot overwrite the reset callback's state.
    if (expected_authority_sequence &&
        *expected_authority_sequence != metrics_->GetGenerationSequence()) {
      return;
    }
    const bool authority_changed = generation != metrics_generation_ ||
        node_ids != allowed_node_ids_ || edge_ids != allowed_edge_ids_;
    const bool descriptors_changed =
        descriptors != allowed_metric_descriptors_;
    if (authority_changed || descriptors_changed) {
      current_metrics_.clear();
      current_metrics_bytes_ = 0U;
      last_sample_time_.reset();
      ++snapshot_sequence_;
    }
    metrics_generation_ = generation;
    allowed_node_ids_ = std::move(node_ids);
    allowed_edge_ids_ = std::move(edge_ids);
    allowed_metric_descriptors_ = std::move(descriptors);
    current_metric_schemas_ = schemas;
  }

  void RefreshAuthoritativeState() {
    const auto authority_sequence = metrics_->GetGenerationSequence();
    const auto snapshot = coordinator_->Snapshot();
    const auto generation = metrics_->GetGraphGeneration();
    const auto schemas = metrics_->GetNodeMetricsSchemas();
    if (authority_sequence != metrics_->GetGenerationSequence()) {
      return;
    }
    ApplyAuthoritativeState(snapshot, generation, schemas,
                            authority_sequence);
  }

  static bool SampleIsValid(const app::metrics::MetricSample& sample) {
    if (sample.metric_id.empty() || sample.metric_id.size() > 128U ||
        sample.unit.size() > 32U ||
        sample.availability_rule.empty() ||
        sample.availability_rule.size() > 256U ||
        sample.unavailable_reason.size() > 256U ||
        !IsValidUtf8(sample.metric_id) || !IsValidUtf8(sample.scalar_type) ||
        !IsValidUtf8(sample.unit) || !IsValidUtf8(sample.semantics) ||
        !IsValidUtf8(sample.aggregation) ||
        !IsValidUtf8(sample.availability_rule) ||
        !IsValidUtf8(sample.unavailable_reason)) {
      return false;
    }
    const bool type_valid = sample.scalar_type == "boolean" ||
        sample.scalar_type == "integer" || sample.scalar_type == "unsigned" ||
        sample.scalar_type == "number" || sample.scalar_type == "string";
    const bool semantics_valid = sample.semantics == "gauge" ||
        sample.semantics == "monotonic_counter" || sample.semantics == "state";
    const bool aggregation_valid = sample.aggregation == "sum" ||
        sample.aggregation == "min" || sample.aggregation == "max" ||
        sample.aggregation == "average" || sample.aggregation == "rate" ||
        sample.aggregation == "none";
    if (!type_valid || !semantics_valid || !aggregation_valid ||
        (!sample.available && sample.unavailable_reason.empty())) {
      return false;
    }
    const bool value_type_valid =
        (sample.scalar_type == "boolean" &&
         std::holds_alternative<bool>(sample.value)) ||
        (sample.scalar_type == "integer" &&
         std::holds_alternative<std::int64_t>(sample.value)) ||
        (sample.scalar_type == "unsigned" &&
         std::holds_alternative<std::uint64_t>(sample.value)) ||
        (sample.scalar_type == "number" &&
         std::holds_alternative<double>(sample.value)) ||
        (sample.scalar_type == "string" &&
         std::holds_alternative<std::string>(sample.value));
    return value_type_valid && std::visit([](const auto& value) {
      using Value = std::decay_t<decltype(value)>;
      if constexpr (std::same_as<Value, double>) {
        return std::isfinite(value);
      } else if constexpr (std::same_as<Value, std::string>) {
        return value.size() <= 1024U && IsValidUtf8(value);
      }
      return true;
    }, sample.value);
  }

  static std::size_t EncodedMetricBytes(
      const app::metrics::MetricTarget& target,
      const app::metrics::MetricSample& sample) {
    const std::size_t string_bytes = target.node_id.size() + target.source_node_id.size() +
        target.source_port_kind.size() + target.target_node_id.size() +
        target.target_port_kind.size() + sample.metric_id.size() +
        sample.scalar_type.size() + sample.unit.size() + sample.semantics.size() +
        sample.aggregation.size() + sample.availability_rule.size() +
        sample.unavailable_reason.size();
    // Any valid UTF-8 input byte can require at most six JSON bytes (`\u00XX`).
    // The fixed allowance covers all field names, punctuation, numeric values,
    // identity length prefixes, timestamps, availability/rate fields and array
    // delimiters emitted for one value item.
    std::size_t bytes = string_bytes * 6U + 1024U;
    if (std::holds_alternative<std::string>(target.source_port)) {
      bytes += std::get<std::string>(target.source_port).size() * 6U;
    }
    if (std::holds_alternative<std::string>(target.target_port)) {
      bytes += std::get<std::string>(target.target_port).size() * 6U;
    }
    if (std::holds_alternative<std::string>(sample.value)) {
      bytes += std::get<std::string>(sample.value).size() * 6U;
    }
    return bytes;
  }

  void RecordLocalRejection(
      const capabilities::MetricsRejectionCategory category) noexcept {
    rejected_metric_categories_[static_cast<std::size_t>(category)]
        .fetch_add(1U, std::memory_order_relaxed);
  }

  capabilities::MetricsRejectionDiagnostics LocalRejectionDiagnostics()
      const noexcept {
    capabilities::MetricsRejectionDiagnostics snapshot;
    for (std::size_t index = 0U; index < snapshot.categories.size(); ++index) {
      snapshot.categories[index] =
          rejected_metric_categories_[index].load(std::memory_order_relaxed);
    }
    return snapshot;
  }

  void AcceptMetricsEvent(
      const app::metrics::MetricsEvent& event,
      GraphHttpServer::MetricsCallbackObservation& observation) {
    ++observation.validations;
    // Reject by constant-time metadata before inspecting any sample. This
    // keeps callback work bounded even if a caller supplies a huge vector.
    if (event.graph_generation == 0U ||
        !capabilities::MetricsCapability::ValidateEventContract(event)) {
      RecordLocalRejection(
          capabilities::MetricsRejectionCategory::SampleContract);
      return;
    }
    ++observation.target_key_constructions;
    const auto target_key = MetricTargetKey(event.target);
    if (!target_key) {
      RecordLocalRejection(
          capabilities::MetricsRejectionCategory::SampleContract);
      return;
    }
    ++observation.mutex_acquisitions;
    std::scoped_lock lock(metrics_mutex_);
    if (event.graph_generation != metrics_generation_ ||
        !TargetIsAuthoritativeLocked(event.target, *target_key)) {
      RecordLocalRejection(
          capabilities::MetricsRejectionCategory::AuthorityMismatch);
      return;
    }
    for (const auto& sample : event.samples) {
      ++observation.samples_examined;
      if (!SampleIsValid(sample)) {
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::SampleContract);
        continue;
      }
      const auto key = *target_key + "|metric|" +
                       EncodeIdentityPart(sample.metric_id);
      const auto contract = allowed_metric_descriptors_.find(key);
      if (contract == allowed_metric_descriptors_.end() ||
          contract->second.scalar_type != sample.scalar_type ||
          contract->second.unit != sample.unit ||
          contract->second.semantics != sample.semantics ||
          contract->second.aggregation != sample.aggregation ||
          contract->second.availability_rule != sample.availability_rule) {
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::AuthorityMismatch);
        continue;
      }
      auto found = current_metrics_.find(key);
      if (found == current_metrics_.end() &&
          current_metrics_.size() >= kMaximumMetricValues) {
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::SampleContract);
        continue;
      }
      if (found != current_metrics_.end() &&
          event.timestamp <= found->second.sample_time) {
        if (found->second.sample.semantics == "monotonic_counter" &&
            found->second.sample.aggregation == "rate") {
          found->second.rate.reset();
          found->second.rate_reason = "non_positive_sample_interval";
          ++snapshot_sequence_;
        }
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::SampleContract);
        continue;
      }
      CurrentMetric next{.target = event.target,
                         .sample = sample,
                         .sample_time = event.timestamp,
                         .rate = std::nullopt,
                         .rate_reason = "not_enough_samples",
                         .encoded_bytes = EncodedMetricBytes(event.target, sample)};
      const auto prior_bytes = found == current_metrics_.end()
          ? 0U : found->second.encoded_bytes;
      if (current_metrics_bytes_ - prior_bytes + next.encoded_bytes > 524288U) {
        RecordLocalRejection(
            capabilities::MetricsRejectionCategory::SampleContract);
        continue;
      }
      if (sample.semantics == "monotonic_counter" &&
          sample.aggregation == "rate") {
        if (!sample.available) {
          next.rate_reason = "sample_unavailable";
        } else if (found == current_metrics_.end()) {
          next.rate_reason = "not_enough_samples";
        } else if (found->second.target != event.target ||
                   found->second.sample.metric_id != sample.metric_id ||
                   found->second.sample.scalar_type != sample.scalar_type ||
                   found->second.sample.unit != sample.unit ||
                   found->second.sample.semantics != sample.semantics ||
                   found->second.sample.aggregation != sample.aggregation ||
                   found->second.sample.availability_rule !=
                       sample.availability_rule ||
                   !found->second.sample.available) {
          next.rate_reason = "incompatible_previous_sample";
        } else {
        const auto delta = ExactCounterDelta(found->second.sample.value,
                                             sample.value);
        const auto elapsed = std::chrono::duration<double>(
            event.timestamp - found->second.sample_time).count();
        if (sample.counter_epoch != found->second.sample.counter_epoch) {
          next.rate_reason = "counter_epoch_changed";
        } else if (!delta.numeric) {
          next.rate_reason = "non_numeric_counter";
        } else if (!delta.increasing) {
          next.rate_reason = "counter_not_increasing";
        } else if (elapsed <= 0.0) {
          next.rate_reason = "non_positive_sample_interval";
        } else {
          const auto rate = delta.value / static_cast<long double>(elapsed);
          if (rate > static_cast<long double>(std::numeric_limits<double>::max())) {
            next.rate_reason = "rate_not_finite";
          } else {
            next.rate = static_cast<double>(rate);
            next.rate_reason.clear();
          }
        }
        }
      }
      current_metrics_bytes_ =
          current_metrics_bytes_ - prior_bytes + next.encoded_bytes;
      current_metrics_.insert_or_assign(key, std::move(next));
      ++observation.samples_retained;
      if (!last_sample_time_ || event.timestamp > *last_sample_time_) {
        last_sample_time_ = event.timestamp;
      }
      ++snapshot_sequence_;
    }
  }

  void SynchronizeGeneration() {
    RefreshAuthoritativeState();
  }

  static nlohmann::json ScalarJson(const app::metrics::MetricScalar& scalar) {
    nlohmann::json result;
    std::visit([&result](const auto& value) {
      using Value = std::decay_t<decltype(value)>;
      if constexpr (std::same_as<Value, std::int64_t> ||
                    std::same_as<Value, std::uint64_t>) {
        result = std::to_string(value);
      } else {
        result = value;
      }
    }, scalar);
    return result;
  }

  static std::string ScalarEncoding(const std::string_view scalar_type) {
    return scalar_type == "integer" || scalar_type == "unsigned"
        ? "decimal_string" : "native";
  }

  HttpResponse HandleMetricsSnapshot() {
    if (metrics_snapshot_entry_hook_) {
      metrics_snapshot_entry_hook_();
    }
    SynchronizeGeneration();
    const auto coordinator_snapshot = coordinator_->Snapshot();
    const auto command_state = commands_->GetState(coordinator_snapshot.Revision());
    std::map<std::string, CurrentMetric> values;
    std::vector<app::metrics::NodeMetricsSchema> schemas;
    std::set<std::string> authoritative_node_ids;
    std::set<std::string> authoritative_edge_ids;
    std::uint64_t generation = 0U;
    std::uint64_t sequence = 0U;
    std::optional<std::chrono::system_clock::time_point> last_sample;
    {
      std::scoped_lock lock(metrics_mutex_);
      values = current_metrics_;
      schemas = current_metric_schemas_;
      authoritative_node_ids = allowed_node_ids_;
      authoritative_edge_ids = allowed_edge_ids_;
      generation = metrics_generation_;
      sequence = snapshot_sequence_;
      last_sample = last_sample_time_;
    }
    auto schema_json = nlohmann::json::array();
    struct DescriptorRef {
      const app::metrics::NodeMetricsSchema* schema;
      const app::metrics::NodeMetricsSchema::MetricDescriptor* descriptor;
      std::string key;
    };
    std::vector<DescriptorRef> descriptors;
    std::set<std::string> seen_descriptor_keys;
    for (const auto& schema : schemas) {
      const auto target_key = MetricTargetKey(schema.target);
      if (!target_key || schema.graph_generation != generation ||
          !(schema.target.kind == app::metrics::MetricTarget::Kind::Node
                ? authoritative_node_ids.contains(schema.target.node_id)
                : authoritative_edge_ids.contains(*target_key))) continue;
      for (const auto& descriptor : schema.descriptors) {
        const auto key = *target_key + "|metric|" +
                         EncodeIdentityPart(descriptor.metric_id);
        if (seen_descriptor_keys.insert(key).second) {
          descriptors.push_back({&schema, &descriptor, key});
        }
      }
    }
    std::ranges::sort(descriptors, {}, &DescriptorRef::key);
    const auto now = std::chrono::system_clock::now();
    std::string availability = "available";
    std::string reason;
    if (generation != command_state.graph_generation) {
      availability = "unavailable";
      reason = "generation_transition";
    } else if (generation == 0U ||
        command_state.executor_state == graph::ExecutionState::CONFIGURED) {
      availability = "unavailable";
      reason = "not_initialized";
    } else if (command_state.executor_state == graph::ExecutionState::STOPPING ||
               command_state.executor_state == graph::ExecutionState::STOPPED) {
      availability = "unavailable";
      reason = "execution_stopped";
    } else if (command_state.executor_state == graph::ExecutionState::ERROR) {
      availability = "unavailable";
      reason = "execution_error";
    } else if (descriptors.empty()) {
      availability = "unavailable";
      reason = "no_metric_schemas";
    } else if (!last_sample) {
      availability = "unavailable";
      reason = "no_samples";
    } else if (now - *last_sample > kMetricStaleAfter) {
      availability = "stale";
      reason = "sample_timeout";
    }
    auto value_json = nlohmann::json::array();
    for (const auto& entry : descriptors) {
      const auto& descriptor = *entry.descriptor;
      schema_json.push_back({
          {"target", MetricTargetJson(entry.schema->target)},
          {"graph_generation", generation},
          {"metric_id", descriptor.metric_id},
          {"scalar_type", descriptor.scalar_type},
          {"scalar_encoding", ScalarEncoding(descriptor.scalar_type)},
          {"unit", descriptor.unit},
          {"semantics", descriptor.semantics},
          {"aggregation", descriptor.aggregation},
          {"availability_rule", descriptor.availability_rule}});
      const auto current = values.find(entry.key);
      nlohmann::json item{
          {"target", MetricTargetJson(entry.schema->target)},
          {"graph_generation", generation},
          {"metric_id", descriptor.metric_id},
          {"scalar_type", descriptor.scalar_type},
          {"scalar_encoding", ScalarEncoding(descriptor.scalar_type)},
          {"unit", descriptor.unit},
          {"semantics", descriptor.semantics},
          {"aggregation", descriptor.aggregation},
          {"availability_rule", descriptor.availability_rule}};
      const bool descriptor_mismatch = current != values.end() &&
          (current->second.sample.metric_id != descriptor.metric_id ||
           current->second.sample.scalar_type != descriptor.scalar_type ||
           current->second.sample.unit != descriptor.unit ||
           current->second.sample.semantics != descriptor.semantics ||
           current->second.sample.aggregation != descriptor.aggregation ||
           current->second.sample.availability_rule !=
               descriptor.availability_rule);
      const bool global_no_samples = availability == "unavailable" &&
                                     reason == "no_samples";
      if ((availability != "available" && !global_no_samples) ||
          current == values.end() ||
          descriptor_mismatch) {
        item["availability"] = "unavailable";
        item["reason"] = (availability != "available" && !global_no_samples)
            ? reason
            : descriptor_mismatch ? "schema_mismatch"
            : entry.schema->target.kind ==
                  app::metrics::MetricTarget::Kind::Edge
                ? "unbound_edge_identity" : "not_yet_sampled";
        item["value"] = nullptr;
        item["sample_time"] = nullptr;
      } else {
        item["availability"] = current->second.sample.available
                                   ? "available" : "unavailable";
        item["reason"] = current->second.sample.available
                             ? "" : current->second.sample.unavailable_reason;
        item["value"] = current->second.sample.available
                            ? ScalarJson(current->second.sample.value)
                            : nlohmann::json(nullptr);
        item["sample_time"] = Rfc3339(current->second.sample_time);
      }
      if (descriptor.semantics == "monotonic_counter") {
        item["counter_epoch_encoding"] = "decimal_string";
        if (current == values.end() || descriptor_mismatch) {
          item["counter_epoch"] = nullptr;
          item["rate"] = nullptr;
          item["rate_reason"] = item["reason"];
        } else {
          item["counter_epoch"] =
              std::to_string(current->second.sample.counter_epoch);
          const bool rate_is_available = availability == "available" &&
              current->second.sample.available && current->second.rate;
          item["rate"] = rate_is_available
                              ? nlohmann::json(*current->second.rate)
                              : nlohmann::json(nullptr);
          item["rate_reason"] = rate_is_available
              ? nlohmann::json("")
              : nlohmann::json(current->second.rate_reason.empty()
                    ? std::string{item["reason"]}
                    : current->second.rate_reason);
        }
      }
      value_json.push_back(std::move(item));
    }
    const auto active_revision = command_state.active_revision
        ? nlohmann::json(*command_state.active_revision) : nlohmann::json(nullptr);
    const auto capability_rejections = metrics_->GetRejectionDiagnostics();
    const auto local_rejections = LocalRejectionDiagnostics();
    capabilities::MetricsRejectionDiagnostics combined_rejections;
    for (std::size_t index = 0U;
         index < combined_rejections.categories.size(); ++index) {
      combined_rejections.categories[index] =
          capability_rejections.categories[index] +
          local_rejections.categories[index];
    }
    const auto rejection_count = [&](const auto category) {
      return combined_rejections.Get(category);
    };
    const nlohmann::json rejection_categories = {
        {"schema_contract", rejection_count(
            capabilities::MetricsRejectionCategory::SchemaContract)},
        {"sample_contract", rejection_count(
            capabilities::MetricsRejectionCategory::SampleContract)},
        {"authority_mismatch", rejection_count(
            capabilities::MetricsRejectionCategory::AuthorityMismatch)},
        {"subscriber_failure", rejection_count(
            capabilities::MetricsRejectionCategory::SubscriberFailure)},
        {"internal", rejection_count(
            capabilities::MetricsRejectionCategory::Internal)}};
    const nlohmann::json diagnostics = {
        {"rejected", combined_rejections.Total()},
        {"dropped_queue_full", metrics_->DroppedQueueFullCount()},
        {"rejection_categories", rejection_categories}};
    auto response = JsonResponse(200, {
        {"success", true},
        {"data", {
            {"schema_version", 1},
            {"graph_generation", generation},
            {"active_revision", active_revision},
            {"snapshot_sequence", sequence},
            {"snapshot_time", Rfc3339(now)},
            {"availability", {{"state", availability}, {"reason", reason}}},
            {"schemas", std::move(schema_json)},
            {"values", std::move(value_json)},
            {"diagnostics", diagnostics}}}});
    if (response.body.size() > metrics_body_limit_) {
      return ErrorResponse(503, "snapshot_unavailable",
                           "metric snapshot exceeds the encoded byte limit");
    }
    return response;
  }

  HttpResponse HandleRequest(const std::string &method,
                             const std::string &path,
                             const std::string &body) {
    if (method == "OPTIONS") {
      return HttpResponse{.status = 204,
                          .headers = {{"Allow", AllowedMethods(path)}}};
    }
    const auto allowed_methods = AllowedMethods(path);
    if (allowed_methods != "OPTIONS" &&
        !IsMethodAllowed(allowed_methods, method)) {
      return HttpResponse{
          .status = 405,
          .content_type = "application/json",
          .body = ErrorResponse(
                      405, "method_not_allowed",
                      "method not allowed for requested resource")
                      .body,
          .headers = {{"Allow", allowed_methods}}};
    }
    if (!path.starts_with("/api/") && method == "GET") {
      return HandleGetStaticResource(path);
    }
    if (path == "/api/v1/graph" && method == "GET") {
      const auto snapshot = coordinator_->Snapshot();
      return JsonResponse(
          200, {{"success", true},
                {"data", snapshot.Document()},
                {"snapshot", {
                    {"coordinator_revision", snapshot.Revision()},
                    {"content_identity", snapshot.ContentIdentity()}}}});
    }
    if (path == "/api/v1/metrics" && method == "GET") {
      return HandleMetricsSnapshot();
    }
    if (path == "/api/v1/nodes" && method == "GET") {
      auto graph = coordinator_->GetGraphJson();
      auto nodes = nlohmann::json::array();
      if (graph.contains("nodes") && graph["nodes"].is_array()) {
        nodes = graph["nodes"];
      }
      return JsonResponse(200, {{"success", true}, {"data", nodes}});
    }

    constexpr std::string_view type_prefix = "/api/v1/nodes/type/";
    if (path.starts_with(type_prefix) && method == "GET") {
      const auto type = path.substr(type_prefix.size());
      if (type.empty()) {
        return ErrorResponse(400, "bad_request", "node type is required");
      }
      return JsonResponse(
          200, {{"success", true},
                {"data", coordinator_->GetNodesByType(type)}});
    }

    constexpr std::string_view node_prefix = "/api/v1/nodes/";
    if (path.starts_with(node_prefix)) {
      const auto id = path.substr(node_prefix.size());
      if (id.empty()) {
        return ErrorResponse(400, "bad_request", "node ID is required");
      }
      if (method == "GET") {
        const auto node = coordinator_->GetNode(id);
        if (node.is_null()) {
          return ErrorResponse(404, "not_found", "Node not found: " + id);
        }
        return JsonResponse(200, {{"success", true}, {"data", node}});
      }
      if (method == "PATCH") {
        return HandlePatchNode(id, body);
      }
      return HttpResponse{
          .status = 405,
          .content_type = "application/json",
          .body = ErrorResponse(405, "method_not_allowed",
                                "method not allowed for node resource")
                      .body,
          .headers = {{"Allow", "GET, PATCH, OPTIONS"}}};
    }

    if (path == "/api/v1/execution/state" && method == "GET") {
      const auto snapshot = coordinator_->Snapshot();
      return CommandOperationResultResponse(
          commands_->GetState(snapshot.Revision()), false);
    }
    if (path == "/api/v1/execution/commands" && method == "GET") {
      auto commands = nlohmann::json::array();
      for (const auto& descriptor : commands_->DiscoverCommands()) {
        commands.push_back(
          {{"name", std::string{capabilities::ToString(descriptor.name)}},
             {"asynchronous", descriptor.asynchronous},
             {"arguments", descriptor.arguments},
             {"description", descriptor.description}});
      }
      return JsonResponse(200, {{"success", true}, {"data", commands}});
    }

    constexpr std::string_view command_prefix =
        "/api/v1/execution/commands/";
    if (path.starts_with(command_prefix) && method == "POST") {
      return HandleExecution(
          path.substr(command_prefix.size()), body);
    }

    constexpr std::string_view operation_prefix =
        "/api/v1/execution/operations/";
    if (path.starts_with(operation_prefix) && method == "GET") {
      const auto operation_id = path.substr(operation_prefix.size());
      if (operation_id.empty()) {
        return ErrorResponse(404, "not_found", "operation ID not found");
      }
      const auto result = commands_->GetOperation(operation_id);
      if (!result) {
        return ErrorResponse(404, "not_found",
                             "operation ID not found: " + operation_id);
      }
      return CommandOperationResultResponse(*result, false);
    }

    constexpr std::string_view execution_prefix = "/api/v1/execution/";
    if (path.starts_with(execution_prefix) && method == "POST") {
      return HandleExecution(path.substr(execution_prefix.size()), body);
    }

    if (path.starts_with("/api/")) {
      return ErrorResponse(404, "not_found", "endpoint not found");
    }
    return ErrorResponse(404, "not_found", "resource not found");
  }

  HttpResponse HandlePatchNode(const std::string &id,
                               const std::string &body) {
    try {
      const auto request = nlohmann::json::parse(body);
      if (!request.is_object() || !request.contains("node_config") ||
          !request["node_config"].is_object()) {
        return ErrorResponse(
            400, "bad_request",
            "request must contain an object-valued 'node_config' field");
      }
      if (!coordinator_->UpdateNodeConfig(id, request["node_config"])) {
        return ErrorResponse(404, "not_found", "Node not found: " + id);
      }
      return JsonResponse(
          200,
          {{"success", true}, {"data", coordinator_->GetNode(id)}});
    } catch (const nlohmann::json::parse_error &error) {
      return ErrorResponse(400, "bad_request",
                           std::string{"invalid JSON: "} + error.what());
    }
  }

  HttpResponse HandleExecution(const std::string &operation,
                               const std::string& body) {
    if (operation == "pause" || operation == "resume" ||
        operation == "step") {
      return ErrorResponse(501, "not_implemented",
                           operation + " is not supported by GraphExecutor");
    }

    const auto command = capabilities::ParseCommandName(operation);
    if (!command) {
      return ErrorResponse(404, "not_found", "execution command not found");
    }
    capabilities::CommandRequest request{.name = *command};
    const auto snapshot = coordinator_->Snapshot();
    request.coordinator_revision = snapshot.Revision();
    if (*command == capabilities::CommandName::Configure) {
      request.configuration = snapshot;
    }
    if (!body.empty()) {
      try {
        request.arguments = nlohmann::json::parse(body);
        if (!request.arguments.is_object()) {
          return ErrorResponse(400, "bad_request",
                               "command arguments must be a JSON object");
        }
      } catch (const nlohmann::json::parse_error& error) {
        return ErrorResponse(
            400, "bad_request",
            std::string{"invalid JSON: "} + error.what());
      }
    }
    const auto descriptors = commands_->DiscoverCommands();
    const auto descriptor = std::ranges::find_if(
        descriptors, [command](const auto& candidate) {
          return candidate.name == *command;
        });
    if (descriptor == descriptors.end()) {
      return ErrorResponse(404, "not_found", "execution command not found");
    }
    if (request.arguments.size() > 32U) {
      return ErrorResponse(400, "bad_request",
                           "command arguments exceed the 32-field limit");
    }
    if (descriptor->arguments.empty() && !request.arguments.empty()) {
      return ErrorResponse(400, "bad_request",
                           "command does not accept arguments");
    }
    return CommandOperationResultResponse(commands_->Submit(request), true);
  }

  static nlohmann::json OptionalRevision(
      const std::optional<std::uint64_t> revision) {
    return revision ? nlohmann::json(*revision) : nlohmann::json(nullptr);
  }

  HttpResponse CommandOperationResultResponse(
      const capabilities::CommandOperationResult& result,
      const bool command_submission) const {
    if (!result.executor_available) {
      return ErrorResponse(503, "executor_unavailable",
                           "GraphExecutor is unavailable");
    }
    if (!result.success && command_submission) {
      return ErrorResponse(
          409, std::string{capabilities::ToString(result.command)} + "_failed",
          result.message);
    }
    const bool accepted =
        result.status == capabilities::OperationStatus::Accepted ||
        result.status == capabilities::OperationStatus::Running;
    auto response = JsonResponse(
        accepted && command_submission ? 202 : 200,
        {{"success", result.success},
         {"data",
          {{"command", std::string{capabilities::ToString(result.command)}},
           {"operation_id", result.operation_id},
           {"status", std::string{capabilities::ToString(result.status)}},
           {"state", GetExecutionStateName(result.executor_state)},
           {"coordinator_revision", result.coordinator_revision},
           {"configured_revision",
            OptionalRevision(result.configured_revision)},
           {"active_revision", OptionalRevision(result.active_revision)},
           {"graph_generation", result.graph_generation},
           {"executor_available", result.executor_available},
           {"configuration_dirty", result.configuration_dirty}}},
         {"message", result.message}});
    if (accepted && command_submission) {
      response.headers.push_back(
          {"Location", "/api/v1/execution/operations/" +
                           result.operation_id});
    }
    return response;
  }

  static std::string AllowedMethods(const std::string &path) {
    if (!path.starts_with("/api/") ||
        path == "/api/v1/graph" || path == "/api/v1/metrics" ||
        path == "/api/v1/nodes" ||
        path == "/api/v1/execution/state" ||
        path == "/api/v1/execution/commands" ||
        path.starts_with("/api/v1/execution/operations/") ||
        path.starts_with("/api/v1/nodes/type/")) {
      return "GET, OPTIONS";
    }
    if (path.starts_with("/api/v1/nodes/")) {
      return "GET, PATCH, OPTIONS";
    }
    constexpr std::string_view command_prefix =
        "/api/v1/execution/commands/";
    if (path.starts_with(command_prefix)) {
      const auto command = path.substr(command_prefix.size());
      if (capabilities::ParseCommandName(command) ||
          command == "pause" || command == "resume" ||
          command == "step") {
        return "POST, OPTIONS";
      }
      return "OPTIONS";
    }
    constexpr std::string_view operation_prefix =
        "/api/v1/execution/operations/";
    if (path.starts_with(operation_prefix)) {
      return "GET, OPTIONS";
    }
    constexpr std::string_view execution_prefix =
        "/api/v1/execution/";
    if (path.starts_with(execution_prefix)) {
      const auto command = path.substr(execution_prefix.size());
      if (capabilities::ParseCommandName(command) ||
          command == "pause" || command == "resume" ||
          command == "step") {
        return "POST, OPTIONS";
      }
    }
    return "OPTIONS";
  }

  static bool IsMethodAllowed(const std::string_view allowed_methods,
                              const std::string_view method) {
    if (method == "GET") {
      return allowed_methods == "GET, OPTIONS" ||
             allowed_methods == "GET, PATCH, OPTIONS";
    }
    if (method == "PATCH") {
      return allowed_methods == "GET, PATCH, OPTIONS";
    }
    if (method == "POST") {
      return allowed_methods == "POST, OPTIONS";
    }
    return false;
  }

  HttpResponse HandleGetStaticResource(const std::string &request_path) const {
    const auto decoded = DecodeStaticPath(request_path);
    if (!decoded) {
      return ErrorResponse(404, "not_found", "resource not found");
    }
    const auto relative_text =
        *decoded == "/" ? std::string{"index.html"} : decoded->substr(1U);
    const std::filesystem::path relative{relative_text};
    if (relative.empty() || relative.is_absolute() ||
        std::ranges::any_of(relative, [](const auto &component) {
          return component == "..";
        })) {
      return ErrorResponse(404, "not_found", "resource not found");
    }

    std::error_code error;
    const auto candidate =
        std::filesystem::weakly_canonical(resource_root_ / relative, error);
    if (!resource_root_.empty() && !error &&
        IsContainedPath(resource_root_, candidate) &&
        std::filesystem::is_regular_file(candidate, error) && !error) {
      std::ifstream input(candidate, std::ios::binary);
      if (input) {
        std::ostringstream content;
        content << input.rdbuf();
        return HttpResponse{.status = 200,
                            .content_type = StaticContentType(candidate),
                            .body = content.str()};
      }
    }
    if (relative == "index.html") {
      return ErrorResponse(503, "ui_unavailable",
                           "generic dashboard index.html is unavailable");
    }
    return ErrorResponse(404, "not_found", "resource not found");
  }

  std::shared_ptr<GraphCoordinator> coordinator_;
  std::shared_ptr<capabilities::CommandCapability> commands_;
  std::shared_ptr<capabilities::MetricsCapability> metrics_;
  std::set<std::string> allowed_node_ids_;
  std::set<std::string> allowed_edge_ids_;
  std::map<std::string, MetricDescriptorContract> allowed_metric_descriptors_;
  std::vector<app::metrics::NodeMetricsSchema> current_metric_schemas_;
  mutable std::mutex metrics_mutex_;
  std::map<std::string, CurrentMetric> current_metrics_;
  std::size_t current_metrics_bytes_{0U};
  std::array<std::atomic<std::uint64_t>,
             static_cast<std::size_t>(
                 capabilities::MetricsRejectionCategory::Count)>
      rejected_metric_categories_{};
  std::uint64_t metrics_generation_{0U};
  std::uint64_t snapshot_sequence_{0U};
  std::optional<std::chrono::system_clock::time_point> last_sample_time_;
  std::filesystem::path resource_root_;
  SimpleHttpServer server_;
  std::function<void()> metrics_snapshot_entry_hook_;
  std::function<void(const GraphHttpServer::MetricsCallbackObservation&)>
      metrics_callback_observer_;
  std::size_t metrics_body_limit_{kMaximumMetricsBodyBytes};
};

GraphHttpServer::GraphHttpServer(
    std::shared_ptr<GraphCoordinator> coordinator,
    std::shared_ptr<capabilities::CommandCapability> commands,
    std::shared_ptr<capabilities::MetricsCapability> metrics,
    const int port, std::string index_path)
    : impl_(std::make_unique<Impl>(
          std::move(coordinator), std::move(commands), std::move(metrics),
          port, std::move(index_path))) {}

GraphHttpServer::~GraphHttpServer() noexcept {
  if (impl_) {
    static_cast<void>(Stop());
  }
}
bool GraphHttpServer::Start() {
  if (impl_->IsRunning()) {
    return false;
  }
  impl_->Register(this);
  if (!impl_->Start()) {
    impl_->Unregister(this);
    return false;
  }
  return true;
}
bool GraphHttpServer::Stop() {
  impl_->Unregister(this);
  return impl_->Stop();
}
bool GraphHttpServer::IsRunning() const { return impl_->IsRunning(); }
void GraphHttpServer::OnMetricsEvent(
    const app::metrics::MetricsEvent& event) {
  impl_->OnMetricsEvent(event);
}
void GraphHttpServer::OnMetricsGenerationReset(const std::uint64_t generation) {
  impl_->OnMetricsGenerationReset(generation);
}
void GraphHttpServer::OnMetricsSchemasChanged(
    const std::uint64_t generation,
    const std::vector<app::metrics::NodeMetricsSchema>& schemas) {
  impl_->OnMetricsSchemasChanged(generation, schemas);
}
std::size_t GraphHttpServer::RetainedRequestWorkerCount() const {
  return impl_->RetainedRequestWorkerCount();
}
std::size_t GraphHttpServer::ActiveRequestCount() const {
  return impl_->ActiveRequestCount();
}
std::size_t GraphHttpServer::PendingRequestCount() const {
  return impl_->PendingRequestCount();
}
std::size_t GraphHttpServer::RejectedRequestCount() const {
  return impl_->RejectedRequestCount();
}
void GraphHttpServer::SetMetricsSnapshotEntryHookForTesting(
    std::function<void()> hook) {
  impl_->SetMetricsSnapshotEntryHookForTesting(std::move(hook));
}
void GraphHttpServer::SetMetricsBodyLimitForTesting(const std::size_t bytes) {
  impl_->SetMetricsBodyLimitForTesting(bytes);
}
void GraphHttpServer::SetMetricsCallbackObserverForTesting(
    std::function<void(const MetricsCallbackObservation&)> observer) {
  impl_->SetMetricsCallbackObserverForTesting(std::move(observer));
}
GraphHttpServer::MetricsCallbackObservation
GraphHttpServer::ProbeMetricsCallbackBoundariesForTesting() {
  return impl_->ProbeMetricsCallbackBoundariesForTesting();
}
} // namespace graph
