// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/GraphConfigurationService.hpp"
#include "graph/dashboard/FHSSScenarioController.hpp"
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include "graph/dashboard/GraphSnapshotCollector.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace graph::dashboard {

class EmbeddedDashboardServer {
public:
  struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    std::filesystem::path asset_directory;
    std::filesystem::path artifact_root;
  };

  EmbeddedDashboardServer(Options options,
                          std::shared_ptr<GraphConfigurationService> configuration_service,
                          std::shared_ptr<GraphRuntimeSession> runtime_session,
                          std::shared_ptr<GraphSnapshotCollector> snapshot_collector,
                          std::shared_ptr<FHSSScenarioController> fhss_controller = nullptr);
  ~EmbeddedDashboardServer();

  EmbeddedDashboardServer(const EmbeddedDashboardServer &) = delete;
  EmbeddedDashboardServer &operator=(const EmbeddedDashboardServer &) = delete;

  [[nodiscard]] bool Start();
  void Stop();

  [[nodiscard]] bool IsRunning() const;
  [[nodiscard]] std::uint16_t BoundPort() const;
  [[nodiscard]] const std::string &LastError() const;

private:
  struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
  };

  struct Response {
    int status_code = 200;
    std::string content_type = "application/json";
    std::string body;
  };

  bool ValidateStartup();
  void RunLoop();

  static std::string StatusText(int status_code);
  static std::string GuessContentType(const std::filesystem::path &path);

  static bool ParseRequest(const std::string &request_text, Request &request);
  static std::string GetPathWithoutQuery(const std::string &target);
  static std::string GetQueryValue(const std::string &query, const std::string &key);
  Response HandleRequest(const Request &request) const;
  Response HandleApiRequest(const Request &request) const;
  Response HandleStaticAsset(const Request &request) const;

  static std::string BuildHttpResponse(const Response &response);

  Options options_;
  std::shared_ptr<GraphConfigurationService> configuration_service_;
  std::shared_ptr<FHSSScenarioController> fhss_controller_;
  std::shared_ptr<GraphRuntimeSession> runtime_session_;
  std::shared_ptr<GraphSnapshotCollector> snapshot_collector_;

  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread server_thread_;
  std::uint16_t bound_port_ = 0;
  std::string last_error_;
};

} // namespace graph::dashboard
