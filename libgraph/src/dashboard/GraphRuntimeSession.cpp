// SPDX-License-Identifier: MIT

#include "graph/dashboard/GraphRuntimeSession.hpp"

#include "graph/GraphManager.hpp"

namespace graph::dashboard {

GraphRuntimeSession::GraphRuntimeSession() : state_(State::initializing) {}

GraphRuntimeSession::State GraphRuntimeSession::GetState() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool GraphRuntimeSession::IsReady() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_ != State::initializing && state_ != State::rebuilding &&
         state_ != State::shutting_down && state_ != State::dead;
}

std::string GraphRuntimeSession::StateString() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  switch (state_) {
  case State::initializing:
    return "initializing";
  case State::rebuilding:
    return "rebuilding";
  case State::shutting_down:
    return "shutting_down";
  case State::dead:
    return "dead";
  default:
    return "ready";
  }
}

std::string GraphRuntimeSession::LifecycleStateString() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return StateToString(state_);
}

GraphRuntimeSession::StatusSnapshot GraphRuntimeSession::SnapshotStatus() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return StatusSnapshot{.state = state_,
                        .ready = state_ != State::initializing &&
                                 state_ != State::rebuilding &&
                                 state_ != State::shutting_down &&
                                 state_ != State::dead,
                        .rebuild_allowed = IsRebuildAllowedState(state_) && !rebuild_blocked_,
                        .rebuild_blocked = rebuild_blocked_,
                        .active_generation = active_generation_,
                        .rebuild_attempts = rebuild_attempts_,
                        .successful_rebuilds = successful_rebuilds_,
                        .last_error_code = last_error_code_,
                        .last_error_message = last_error_message_};
}

std::shared_ptr<graph::GraphManager> GraphRuntimeSession::GetActiveGraphManager() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return active_graph_manager_;
}

void GraphRuntimeSession::MarkReady() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::initializing) {
    state_ = State::not_built;
  }
}

void GraphRuntimeSession::MarkShuttingDown() {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::shutting_down;
}

void GraphRuntimeSession::MarkDead() {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_ = State::dead;
}

void GraphRuntimeSession::SetLifecycleState(State state) {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_ = state;
  if (state != State::cleanup_failed) {
    rebuild_blocked_ = false;
  }
}

void GraphRuntimeSession::SetActiveGraphManager(
    std::shared_ptr<graph::GraphManager> graph_manager) {
  const std::lock_guard<std::mutex> lock(mutex_);
  active_graph_manager_ = std::move(graph_manager);
}

GraphRuntimeSession::CommandResult GraphRuntimeSession::Rebuild() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (rebuild_blocked_) {
    return CommandResult{.status_code = 409,
                         .code = "cleanup_failed",
                         .message = "rebuild is blocked until cleanup retry or process restart"};
  }

  if (!IsRebuildAllowedState(state_)) {
    return CommandResult{.status_code = 409,
                         .code = "invalid_state",
                         .message = "rebuild is not allowed in current runtime state"};
  }

  const auto previous_state = state_;
  state_ = State::rebuilding;
  ++rebuild_attempts_;

  if (shutdown_during_next_rebuild_) {
    shutdown_during_next_rebuild_ = false;
    state_ = State::shutting_down;
    last_error_code_ = "shutdown_in_progress";
    last_error_message_ = "rebuild interrupted by shutdown";
    return CommandResult{.status_code = 503,
                         .code = last_error_code_,
                         .message = last_error_message_};
  }

  if (interrupt_next_thread_flow_) {
    interrupt_next_thread_flow_ = false;
    state_ = previous_state;
    last_error_code_ = "thread_interrupted";
    last_error_message_ = "rebuild interrupted around command/runtime-owner flow";
    return CommandResult{.status_code = 503,
                         .code = last_error_code_,
                         .message = last_error_message_};
  }

  if (fail_next_queue_disable_) {
    fail_next_queue_disable_ = false;
    state_ = previous_state;
    last_error_code_ = "queue_disable_failed";
    last_error_message_ = "queue disable failed during rebuild";
    return CommandResult{.status_code = 500,
                         .code = last_error_code_,
                         .message = last_error_message_};
  }

  if (fail_next_executor_construction_) {
    fail_next_executor_construction_ = false;
    state_ = previous_state;
    last_error_code_ = "executor_construction_failed";
    last_error_message_ = "replacement runtime construction failed";
    return CommandResult{.status_code = 500,
                         .code = last_error_code_,
                         .message = last_error_message_};
  }

  ++active_generation_;
  ++successful_rebuilds_;
  state_ = State::stopped;
  last_error_code_.clear();
  last_error_message_.clear();

  if (fail_next_cleanup_) {
    fail_next_cleanup_ = false;
    state_ = State::cleanup_failed;
    rebuild_blocked_ = true;
    last_error_code_ = "cleanup_failed";
    last_error_message_ = "old session cleanup failed after activation";
    return CommandResult{.status_code = 202,
                         .code = last_error_code_,
                         .message = last_error_message_};
  }

  return CommandResult{.status_code = 202,
                       .code = "rebuild_succeeded",
                       .message = "rebuild completed"};
}

GraphRuntimeSession::CommandResult GraphRuntimeSession::Start() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == State::running) {
    return CommandResult{.status_code = 409,
                         .code = "invalid_state",
                         .message = "runtime already running"};
  }
  if (state_ == State::rebuilding || state_ == State::shutting_down ||
      state_ == State::dead || state_ == State::initializing) {
    return CommandResult{.status_code = 409,
                         .code = "invalid_state",
                         .message = "runtime cannot start in current state"};
  }
  state_ = State::running;
  return CommandResult{.status_code = 202,
                       .code = "start_accepted",
                       .message = "runtime start accepted"};
}

GraphRuntimeSession::CommandResult GraphRuntimeSession::Stop() {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != State::running) {
    return CommandResult{.status_code = 409,
                         .code = "invalid_state",
                         .message = "runtime is not running"};
  }
  state_ = State::stopped;
  return CommandResult{.status_code = 202,
                       .code = "stop_accepted",
                       .message = "runtime stop accepted"};
}

void GraphRuntimeSession::InjectNextExecutorConstructionFailureForTesting() {
  const std::lock_guard<std::mutex> lock(mutex_);
  fail_next_executor_construction_ = true;
}

void GraphRuntimeSession::InjectNextQueueDisableFailureForTesting() {
  const std::lock_guard<std::mutex> lock(mutex_);
  fail_next_queue_disable_ = true;
}

void GraphRuntimeSession::InjectNextCleanupFailureForTesting() {
  const std::lock_guard<std::mutex> lock(mutex_);
  fail_next_cleanup_ = true;
}

void GraphRuntimeSession::InjectNextThreadInterruptionForTesting() {
  const std::lock_guard<std::mutex> lock(mutex_);
  interrupt_next_thread_flow_ = true;
}

void GraphRuntimeSession::InjectShutdownDuringNextRebuildForTesting() {
  const std::lock_guard<std::mutex> lock(mutex_);
  shutdown_during_next_rebuild_ = true;
}

void GraphRuntimeSession::SetStateForTesting(State state) {
  SetLifecycleState(state);
}

bool GraphRuntimeSession::IsRebuildAllowedInCurrentState() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return !rebuild_blocked_ && IsRebuildAllowedState(state_);
}

bool GraphRuntimeSession::IsRebuildAllowedState(State state) {
  return state == State::not_built || state == State::stopped ||
         state == State::completed || state == State::failed;
}

std::string GraphRuntimeSession::StateToString(State state) {
  switch (state) {
  case State::initializing:
    return "initializing";
  case State::not_built:
    return "not_built";
  case State::stopped:
    return "stopped";
  case State::running:
    return "running";
  case State::completed:
    return "completed";
  case State::failed:
    return "failed";
  case State::rebuilding:
    return "rebuilding";
  case State::cleanup_failed:
    return "cleanup_failed";
  case State::shutting_down:
    return "shutting_down";
  case State::dead:
    return "dead";
  }
  return "unknown";
}

} // namespace graph::dashboard
