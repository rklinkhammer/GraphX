// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace graph {
class GraphManager;
}

namespace graph::dashboard {

class IGraphRuntimeOwner {
public:
  struct BuildSnapshot {
    nlohmann::json receiver_graph;
    std::uint64_t config_revision = 0;
    std::string config_etag;
  };
  struct Result {
    int status_code = 200;
    std::string code;
    std::string message;
    std::shared_ptr<graph::GraphManager> graph_manager;
    bool cleanup_failed = false;
  };
  virtual ~IGraphRuntimeOwner() = default;
  virtual Result Rebuild(std::uint64_t generation,
                         const BuildSnapshot &snapshot) = 0;
  virtual Result Start(std::uint64_t generation, std::uint64_t run_epoch) = 0;
  virtual Result Stop(std::uint64_t generation) = 0;
  virtual Result Shutdown(std::uint64_t generation) = 0;
  using CompletionCallback = std::function<void(
      std::uint64_t, std::uint64_t, bool, std::string)>;
  virtual void SetCompletionCallback(CompletionCallback callback) = 0;
};

} // namespace graph::dashboard
