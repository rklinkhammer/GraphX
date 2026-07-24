// SPDX-License-Identifier: MIT

#pragma once

#include "graph/dashboard/IGraphRuntimeOwner.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace graph {
class GraphManager;
}

namespace graph::dashboard {

[[nodiscard]] std::string
CanonicalEdgeId(const std::string &source_node_id, std::size_t source_port,
                const std::string &destination_node_id,
                std::size_t destination_port);

class GraphRuntimeSession {
public:
  enum class State {
    initializing,
    not_built,
    stopped,
    starting,
    running,
    completed,
    failed,
    rebuilding,
    cleanup_failed,
    shutting_down,
    dead
  };

  struct CommandResult {
    int status_code = 200;
    std::string code;
    std::string message;
  };

  struct StatusSnapshot {
    State state = State::initializing;
    bool ready = false;
    bool rebuild_allowed = false;
    bool rebuild_blocked = false;
    std::uint64_t active_generation = 0;
    std::uint64_t active_run_epoch = 0;
    std::uint64_t rebuild_attempts = 0;
    std::uint64_t successful_rebuilds = 0;
    std::string last_error_code;
    std::string last_error_message;
    std::uint64_t active_config_revision = 0;
    std::string active_config_etag;
    bool stop_requested = false;
    std::uint64_t terminal_generation = 0;
    std::string terminal_result_code;
    std::string terminal_result_message;
    std::string started_at;
    std::string terminal_at;
  };

  struct GenerationSnapshot {
    struct EdgeIdentity {
      std::string edge_id;
      std::string source_node_id;
      std::size_t source_port = 0;
      std::string destination_node_id;
      std::size_t destination_port = 0;
    };

    std::uint64_t generation = 0;
    std::uint64_t run_epoch = 0;
    std::uint64_t config_revision = 0;
    std::string config_etag;
    // These vectors are immutable for a graph generation. Their positions are
    // runtime lookup keys only; the string IDs are the canonical identities.
    std::vector<std::string> canonical_node_ids;
    std::vector<EdgeIdentity> canonical_edges;
    std::string identity_error;
    std::shared_ptr<graph::GraphManager> graph_manager;
  };

  explicit GraphRuntimeSession(std::shared_ptr<IGraphRuntimeOwner> owner = {});
  ~GraphRuntimeSession();

  [[nodiscard]] State GetState() const;
  [[nodiscard]] bool IsReady() const;
  [[nodiscard]] std::string StateString() const;
  [[nodiscard]] std::string LifecycleStateString() const;
  [[nodiscard]] StatusSnapshot SnapshotStatus() const;
  [[nodiscard]] GenerationSnapshot SnapshotGeneration() const;
  [[nodiscard]] static std::string StateToString(State state);

  void MarkReady();
  void MarkShuttingDown();
  void MarkDead();
  void SetLifecycleState(State state);
  void
  SetActiveGraphManager(std::shared_ptr<graph::GraphManager> graph_manager);
  [[nodiscard]] CommandResult
  Rebuild(IGraphRuntimeOwner::BuildSnapshot snapshot);
  [[nodiscard]] CommandResult Start();
  [[nodiscard]] CommandResult Stop();

  void SetStateForTesting(State state);
  void SetIdentityCountersForTesting(std::uint64_t active_generation,
                                     std::uint64_t active_run_epoch,
                                     std::uint64_t command_epoch,
                                     std::uint64_t rebuild_attempts,
                                     std::uint64_t successful_rebuilds);

  [[nodiscard]] bool IsRebuildAllowedInCurrentState() const;

private:
  static bool IsRebuildAllowedState(State state);
  void OwnerCompleted(std::uint64_t generation, std::uint64_t run_epoch,
                      bool success, std::string message);

  mutable std::mutex mutex_;
  State state_;
  bool rebuild_blocked_ = false;
  std::uint64_t active_generation_ = 0;
  std::uint64_t rebuild_attempts_ = 0;
  std::uint64_t successful_rebuilds_ = 0;
  std::string last_error_code_;
  std::string last_error_message_;
  std::shared_ptr<graph::GraphManager> active_graph_manager_;
  std::shared_ptr<IGraphRuntimeOwner> owner_;
  std::uint64_t command_epoch_ = 0;
  GenerationSnapshot active_snapshot_;
  bool stop_requested_ = false;
  std::uint64_t active_run_epoch_ = 0;
  std::uint64_t terminal_generation_ = 0;
  std::string terminal_result_code_, terminal_result_message_, started_at_,
      terminal_at_;
};

} // namespace graph::dashboard
