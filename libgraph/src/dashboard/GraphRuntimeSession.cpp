// SPDX-License-Identifier: MIT
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace graph::dashboard {
namespace {
std::string NowIso() {
  const auto value =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm time{};
  gmtime_r(&value, &time);
  std::ostringstream output;
  output << std::put_time(&time, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}
} // namespace

GraphRuntimeSession::GraphRuntimeSession(
    std::shared_ptr<IGraphRuntimeOwner> owner)
    : state_(State::initializing), owner_(std::move(owner)) {
  if (owner_)
    owner_->SetCompletionCallback(
        [this](auto generation, auto run_epoch, auto success, auto message) {
          OwnerCompleted(generation, run_epoch, success, std::move(message));
        });
}

GraphRuntimeSession::~GraphRuntimeSession() {
  if (!owner_)
    return;
  owner_->SetCompletionCallback({});
  std::uint64_t generation;
  {
    const std::lock_guard lock(mutex_);
    generation = active_generation_;
  }
  (void)owner_->Shutdown(generation);
}

void GraphRuntimeSession::OwnerCompleted(std::uint64_t generation,
                                         std::uint64_t run_epoch, bool success,
                                         std::string message) {
  const std::lock_guard lock(mutex_);
  if (generation != active_generation_ || run_epoch != active_run_epoch_ ||
      (state_ != State::starting && state_ != State::running))
    return;
  const bool cancelled = stop_requested_;
  state_ =
      cancelled ? State::stopped : (success ? State::completed : State::failed);
  terminal_generation_ = generation;
  terminal_result_code_ =
      cancelled ? "execution_cancelled"
                : (success ? "execution_completed" : "execution_failed");
  terminal_result_message_ = std::move(message);
  terminal_at_ = NowIso();
  if (!success && !cancelled) {
    last_error_code_ = "execution_failed";
    last_error_message_ = terminal_result_message_;
  }
}

GraphRuntimeSession::State GraphRuntimeSession::GetState() const {
  const std::lock_guard lock(mutex_);
  return state_;
}
bool GraphRuntimeSession::IsReady() const {
  const std::lock_guard lock(mutex_);
  return state_ != State::initializing && state_ != State::rebuilding &&
         state_ != State::shutting_down && state_ != State::dead;
}
std::string GraphRuntimeSession::StateString() const {
  const std::lock_guard lock(mutex_);
  if (state_ == State::initializing)
    return "initializing";
  if (state_ == State::rebuilding)
    return "rebuilding";
  if (state_ == State::shutting_down)
    return "shutting_down";
  if (state_ == State::dead)
    return "dead";
  return "ready";
}
std::string GraphRuntimeSession::LifecycleStateString() const {
  const std::lock_guard lock(mutex_);
  return StateToString(state_);
}

GraphRuntimeSession::StatusSnapshot
GraphRuntimeSession::SnapshotStatus() const {
  const std::lock_guard lock(mutex_);
  return {.state = state_,
          .ready = state_ != State::initializing &&
                   state_ != State::rebuilding &&
                   state_ != State::shutting_down && state_ != State::dead,
          .rebuild_allowed = IsRebuildAllowedState(state_) && !rebuild_blocked_,
          .rebuild_blocked = rebuild_blocked_,
          .active_generation = active_generation_,
          .rebuild_attempts = rebuild_attempts_,
          .successful_rebuilds = successful_rebuilds_,
          .last_error_code = last_error_code_,
          .last_error_message = last_error_message_,
          .active_config_revision = active_snapshot_.config_revision,
          .active_config_etag = active_snapshot_.config_etag,
          .stop_requested = stop_requested_,
          .terminal_generation = terminal_generation_,
          .terminal_result_code = terminal_result_code_,
          .terminal_result_message = terminal_result_message_,
          .started_at = started_at_,
          .terminal_at = terminal_at_};
}

GraphRuntimeSession::GenerationSnapshot
GraphRuntimeSession::SnapshotGeneration() const {
  const std::lock_guard lock(mutex_);
  return active_snapshot_;
}

void GraphRuntimeSession::MarkReady() {
  const std::lock_guard lock(mutex_);
  if (state_ == State::initializing)
    state_ = State::not_built;
}
void GraphRuntimeSession::MarkShuttingDown() {
  std::uint64_t generation;
  {
    const std::lock_guard lock(mutex_);
    state_ = State::shutting_down;
    generation = active_generation_;
    ++command_epoch_;
  }
  if (owner_)
    (void)owner_->Shutdown(generation);
}
void GraphRuntimeSession::MarkDead() {
  const std::lock_guard lock(mutex_);
  state_ = State::dead;
}
void GraphRuntimeSession::SetLifecycleState(State state) {
  const std::lock_guard lock(mutex_);
  state_ = state;
  if (state != State::cleanup_failed)
    rebuild_blocked_ = false;
}
void GraphRuntimeSession::SetActiveGraphManager(
    std::shared_ptr<graph::GraphManager> manager) {
  const std::lock_guard lock(mutex_);
  active_graph_manager_ = std::move(manager);
  active_snapshot_.graph_manager = active_graph_manager_;
}

GraphRuntimeSession::CommandResult
GraphRuntimeSession::Rebuild(IGraphRuntimeOwner::BuildSnapshot snapshot) {
  if (!owner_)
    return {503, "runtime_owner_unavailable",
            "runtime execution is not enabled"};
  std::uint64_t epoch;
  std::uint64_t generation;
  State previous;
  {
    const std::lock_guard lock(mutex_);
    if (rebuild_blocked_)
      return {409, "cleanup_failed",
              "rebuild is blocked after replacement cleanup failed"};
    if (!IsRebuildAllowedState(state_))
      return {409, "invalid_state",
              "rebuild is not allowed in current runtime state"};
    previous = state_;
    state_ = State::rebuilding;
    ++rebuild_attempts_;
    epoch = ++command_epoch_;
    generation = active_generation_ + 1;
  }
  auto result = owner_->Rebuild(generation, snapshot);
  {
    const std::lock_guard lock(mutex_);
    if (epoch != command_epoch_)
      return {409, "superseded", "runtime command was superseded"};
    if (result.status_code >= 400) {
      state_ = previous;
      last_error_code_ = result.code;
      last_error_message_ = result.message;
    } else {
      active_generation_ = generation;
      ++successful_rebuilds_;
      active_graph_manager_ = std::move(result.graph_manager);
      active_snapshot_ = {generation, snapshot.config_revision,
                          std::move(snapshot.config_etag),
                          active_graph_manager_};
      state_ = result.cleanup_failed ? State::cleanup_failed : State::stopped;
      rebuild_blocked_ = result.cleanup_failed;
      stop_requested_ = false;
      started_at_.clear();
      terminal_generation_ = 0;
      terminal_result_code_.clear();
      terminal_result_message_.clear();
      terminal_at_.clear();
      last_error_code_ = result.cleanup_failed ? result.code : "";
      last_error_message_ = result.cleanup_failed ? result.message : "";
    }
  }
  return {result.status_code, std::move(result.code),
          std::move(result.message)};
}

GraphRuntimeSession::CommandResult GraphRuntimeSession::Start() {
  if (!owner_)
    return {503, "runtime_owner_unavailable",
            "runtime execution is not enabled"};
  std::uint64_t generation;
  std::uint64_t run_epoch;
  State previous;
  {
    const std::lock_guard lock(mutex_);
    if (state_ != State::stopped && state_ != State::completed)
      return {409, "invalid_state", "runtime cannot start in current state"};
    generation = active_generation_;
    previous = state_;
    state_ = State::starting;
    run_epoch = ++active_run_epoch_;
    stop_requested_ = false;
    started_at_ = NowIso();
    terminal_at_.clear();
    terminal_generation_ = 0;
    terminal_result_code_.clear();
    terminal_result_message_.clear();
  }
  auto result = owner_->Start(generation, run_epoch);
  {
    const std::lock_guard lock(mutex_);
    const bool matching_run = generation == active_generation_ &&
                              run_epoch == active_run_epoch_;
    if (result.status_code >= 400 && matching_run &&
        state_ == State::starting) {
      state_ = previous;
      started_at_.clear();
    } else if (result.status_code < 400 && matching_run) {
      // A runtime owner may replace its single-run GraphExecutor while
      // retaining the same logical generation.  Republish the complete
      // snapshot under the session mutex so collectors cannot retain the
      // retired run's manager or observe mixed generation/config identity.
      // Publication must also occur when an immediate completion callback has
      // already advanced the state before owner Start returns.
      if (result.graph_manager) {
        active_graph_manager_ = std::move(result.graph_manager);
        active_snapshot_.graph_manager = active_graph_manager_;
      }
      if (state_ == State::starting)
        state_ = State::running;
    }
  }
  return {result.status_code, std::move(result.code),
          std::move(result.message)};
}

GraphRuntimeSession::CommandResult GraphRuntimeSession::Stop() {
  if (!owner_)
    return {503, "runtime_owner_unavailable",
            "runtime execution is not enabled"};
  std::uint64_t generation;
  {
    const std::lock_guard lock(mutex_);
    if (state_ == State::completed)
      return {200, "already_completed", "runtime already completed"};
    if (state_ != State::running)
      return {409, "invalid_state", "runtime is not running"};
    generation = active_generation_;
    stop_requested_ = true;
  }
  auto result = owner_->Stop(generation);
  if (result.status_code < 400) {
    const std::lock_guard lock(mutex_);
    if (generation == active_generation_ && state_ == State::running)
      state_ = State::stopped;
  }
  return {result.status_code, std::move(result.code),
          std::move(result.message)};
}

void GraphRuntimeSession::SetStateForTesting(State state) {
  SetLifecycleState(state);
}
bool GraphRuntimeSession::IsRebuildAllowedInCurrentState() const {
  const std::lock_guard lock(mutex_);
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
  case State::starting:
    return "starting";
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
