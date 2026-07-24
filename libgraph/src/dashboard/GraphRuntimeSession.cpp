// SPDX-License-Identifier: MIT
#include "graph/dashboard/GraphRuntimeSession.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

namespace graph::dashboard {
namespace {
constexpr std::uint64_t kMaximumJsonSafeInteger = 9'007'199'254'740'991ULL;

std::string NowIso() {
  const auto value =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm time{};
  gmtime_r(&value, &time);
  std::ostringstream output;
  output << std::put_time(&time, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

GraphRuntimeSession::GenerationSnapshot IdentitySnapshot(
    std::uint64_t generation, std::uint64_t config_revision,
    std::string config_etag, const nlohmann::json &graph) {
  GraphRuntimeSession::GenerationSnapshot result;
  result.generation = generation;
  result.config_revision = config_revision;
  result.config_etag = std::move(config_etag);
  if (!graph.is_object() || !graph.contains("nodes") ||
      !graph.at("nodes").is_array() || !graph.contains("edges") ||
      !graph.at("edges").is_array()) {
    result.identity_error =
        "effective receiver graph does not contain node and edge arrays";
    return result;
  }
  std::set<std::string> node_ids;
  for (const auto &node : graph.at("nodes")) {
    const auto id = node.value("id", std::string{});
    if (id.empty() || !node_ids.insert(id).second) {
      result.identity_error =
          "effective receiver graph contains a missing or duplicate node ID";
      result.canonical_node_ids.clear();
      return result;
    }
    result.canonical_node_ids.push_back(id);
  }
  std::set<std::string> edge_ids;
  for (const auto &edge : graph.at("edges")) {
    const auto source = edge.value("source_node_id", std::string{});
    const auto destination = edge.value("target_node_id", std::string{});
    if (!node_ids.contains(source) || !node_ids.contains(destination) ||
        !edge.contains("source_port") ||
        !edge.at("source_port").is_number_unsigned() ||
        !edge.contains("target_port") ||
        !edge.at("target_port").is_number_unsigned()) {
      result.identity_error =
          "effective receiver graph contains an invalid edge identity";
      result.canonical_edges.clear();
      return result;
    }
    const auto source_port = edge.at("source_port").get<std::size_t>();
    const auto destination_port = edge.at("target_port").get<std::size_t>();
    const auto edge_id =
        CanonicalEdgeId(source, source_port, destination, destination_port);
    if (!edge_ids.insert(edge_id).second) {
      result.identity_error =
          "effective receiver graph contains a duplicate edge identity";
      result.canonical_edges.clear();
      return result;
    }
    result.canonical_edges.push_back(
        {.edge_id = edge_id,
         .source_node_id = source,
         .source_port = source_port,
         .destination_node_id = destination,
         .destination_port = destination_port});
  }
  return result;
}
} // namespace

std::string CanonicalEdgeId(const std::string &source_node_id,
                            std::size_t source_port,
                            const std::string &destination_node_id,
                            std::size_t destination_port) {
  return source_node_id + ":" + std::to_string(source_port) + "->" +
         destination_node_id + ":" + std::to_string(destination_port);
}

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
          .active_run_epoch = active_run_epoch_,
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
    if (command_epoch_ < kMaximumJsonSafeInteger)
      command_epoch_ += 1;
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
    if (snapshot.config_revision > kMaximumJsonSafeInteger ||
        active_generation_ >= kMaximumJsonSafeInteger ||
        command_epoch_ >= kMaximumJsonSafeInteger ||
        rebuild_attempts_ >= kMaximumJsonSafeInteger ||
        successful_rebuilds_ >= kMaximumJsonSafeInteger)
      return {409, "identity_space_exhausted",
              "runtime identity or published command counter cannot advance "
              "within JavaScript safe integer bounds"};
    previous = state_;
    state_ = State::rebuilding;
    rebuild_attempts_ += 1;
    command_epoch_ += 1;
    epoch = command_epoch_;
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
      successful_rebuilds_ += 1;
      active_graph_manager_ = std::move(result.graph_manager);
      active_snapshot_ =
          IdentitySnapshot(generation, snapshot.config_revision,
                           std::move(snapshot.config_etag),
                           snapshot.receiver_graph);
      active_snapshot_.graph_manager = active_graph_manager_;
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
    if (active_run_epoch_ >= kMaximumJsonSafeInteger)
      return {409, "identity_space_exhausted",
              "runtime run identity cannot advance within JavaScript safe "
              "integer bounds"};
    generation = active_generation_;
    previous = state_;
    state_ = State::starting;
    active_run_epoch_ += 1;
    run_epoch = active_run_epoch_;
    active_snapshot_.run_epoch = run_epoch;
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
void GraphRuntimeSession::SetIdentityCountersForTesting(
    std::uint64_t active_generation, std::uint64_t active_run_epoch,
    std::uint64_t command_epoch, std::uint64_t rebuild_attempts,
    std::uint64_t successful_rebuilds) {
  const std::lock_guard lock(mutex_);
  active_generation_ = active_generation;
  active_run_epoch_ = active_run_epoch;
  command_epoch_ = command_epoch;
  rebuild_attempts_ = rebuild_attempts;
  successful_rebuilds_ = successful_rebuilds;
  active_snapshot_.generation = active_generation;
  active_snapshot_.run_epoch = active_run_epoch;
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
