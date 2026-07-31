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
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
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
  }

  ~Impl() noexcept {
    // The request handler captures this Impl. Join every request while all
    // handler-visible state is still alive, before member destruction begins.
    static_cast<void>(server_.Stop());
  }

  bool Start() {
    return server_.Start(
        [this](const std::string &method, const std::string &path,
               const std::string &body) {
          return HandleRequest(method, path, body);
        });
  }

  bool Stop() { return server_.Stop(); }
  [[nodiscard]] bool IsRunning() const { return server_.IsRunning(); }
  [[nodiscard]] std::size_t RetainedRequestWorkerCount() const {
    return server_.RetainedWorkerCount();
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
      return JsonResponse(
          200, {{"success", true}, {"data", coordinator_->GetGraphJson()}});
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
        path == "/api/v1/graph" || path == "/api/v1/nodes" ||
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
  std::filesystem::path resource_root_;
  SimpleHttpServer server_;
};

GraphHttpServer::GraphHttpServer(
    std::shared_ptr<GraphCoordinator> coordinator,
    std::shared_ptr<capabilities::CommandCapability> commands,
    std::shared_ptr<capabilities::MetricsCapability> metrics,
    const int port, std::string index_path)
    : impl_(std::make_unique<Impl>(
          std::move(coordinator), std::move(commands), std::move(metrics),
          port, std::move(index_path))) {}

GraphHttpServer::~GraphHttpServer() noexcept = default;
bool GraphHttpServer::Start() { return impl_->Start(); }
bool GraphHttpServer::Stop() { return impl_->Stop(); }
bool GraphHttpServer::IsRunning() const { return impl_->IsRunning(); }
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

} // namespace graph
