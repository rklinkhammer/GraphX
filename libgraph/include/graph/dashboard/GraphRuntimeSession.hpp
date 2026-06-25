// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace graph {
class GraphManager;
}

namespace graph::dashboard {

class GraphRuntimeSession {
public:
  enum class State {
    initializing,
    not_built,
    stopped,
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
    std::uint64_t rebuild_attempts = 0;
    std::uint64_t successful_rebuilds = 0;
    std::string last_error_code;
    std::string last_error_message;
  };

  GraphRuntimeSession();

  [[nodiscard]] State GetState() const;
  [[nodiscard]] bool IsReady() const;
  [[nodiscard]] std::string StateString() const;
  [[nodiscard]] std::string LifecycleStateString() const;
  [[nodiscard]] StatusSnapshot SnapshotStatus() const;
  [[nodiscard]] std::shared_ptr<graph::GraphManager> GetActiveGraphManager() const;
  [[nodiscard]] static std::string StateToString(State state);

  void MarkReady();
  void MarkShuttingDown();
  void MarkDead();
  void SetLifecycleState(State state);
  void SetActiveGraphManager(std::shared_ptr<graph::GraphManager> graph_manager);

  [[nodiscard]] CommandResult Rebuild();
  [[nodiscard]] CommandResult Start();
  [[nodiscard]] CommandResult Stop();

  void InjectNextExecutorConstructionFailureForTesting();
  void InjectNextQueueDisableFailureForTesting();
  void InjectNextCleanupFailureForTesting();
  void InjectNextThreadInterruptionForTesting();
  void InjectShutdownDuringNextRebuildForTesting();
  void SetStateForTesting(State state);

  [[nodiscard]] bool IsRebuildAllowedInCurrentState() const;

private:
  static bool IsRebuildAllowedState(State state);

  mutable std::mutex mutex_;
  State state_;
  bool rebuild_blocked_ = false;
  std::uint64_t active_generation_ = 0;
  std::uint64_t rebuild_attempts_ = 0;
  std::uint64_t successful_rebuilds_ = 0;
  std::string last_error_code_;
  std::string last_error_message_;
  std::shared_ptr<graph::GraphManager> active_graph_manager_;

  bool fail_next_executor_construction_ = false;
  bool fail_next_queue_disable_ = false;
  bool fail_next_cleanup_ = false;
  bool interrupt_next_thread_flow_ = false;
  bool shutdown_during_next_rebuild_ = false;
};

} // namespace graph::dashboard
