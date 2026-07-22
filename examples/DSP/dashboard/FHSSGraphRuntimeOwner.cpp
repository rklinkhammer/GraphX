// SPDX-License-Identifier: MIT
#include "FHSSGraphRuntimeOwner.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>

namespace dsp::fhss::dashboard {
namespace {
constexpr auto kStopTimeout = std::chrono::seconds(5);

template <typename InputSnapshot>
std::optional<std::string>
ValidateReceiverInputAttempt(const InputSnapshot &input) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(input.path, error) || error)
    return "receiver_input_preflight_failed: IQ path is not a regular file";
  const auto byte_size = std::filesystem::file_size(input.path, error);
  if (error)
    return "receiver_input_preflight_failed: IQ byte length is unavailable";
  if (byte_size != input.byte_size)
    return "receiver_input_preflight_failed: IQ path changed after rebuild";
  const std::uint64_t bytes_per_sample =
      input.sample_format == "cf32_le" ? 8u : 16u;
  if (byte_size % bytes_per_sample != 0u)
    return "receiver_input_preflight_failed: IQ byte length is not aligned to " +
           input.sample_format + " complex samples";
  const auto total = static_cast<std::uint64_t>(byte_size / bytes_per_sample);
  if (input.first_complex_sample > total)
    return "receiver_input_preflight_failed: first_complex_sample exceeds IQ length";
  const auto available = total - input.first_complex_sample;
  const auto selected = input.max_complex_samples == 0u
                            ? available
                            : std::min(input.max_complex_samples, available);
  if (selected > input.max_read_complex_samples)
    return "receiver_input_preflight_failed: IQ selection exceeds max_read_complex_samples";
  return std::nullopt;
}
}

FHSSGraphRuntimeOwner::FHSSGraphRuntimeOwner(
    std::filesystem::path plugin_directory,
    std::filesystem::path runtime_directory)
    : plugin_directory_(std::move(plugin_directory)),
      runtime_directory_(std::move(runtime_directory)) {}

FHSSGraphRuntimeOwner::~FHSSGraphRuntimeOwner() {
  // Shutdown serializes access and does not use its generation argument.
  (void)Shutdown(0);
}

graph::dashboard::IGraphRuntimeOwner::Result
FHSSGraphRuntimeOwner::Rebuild(std::uint64_t generation,
                               const BuildSnapshot &snapshot) {
  const std::lock_guard operation_lock(operation_mutex_);
  try {
    if (execution_thread_.joinable()) {
      const std::lock_guard completion_lock(completion_mutex_);
      if (!execution_finished_)
        return {409, "execution_active",
                "cannot rebuild while receiver execution is active"};
      execution_thread_.join();
    }
    std::filesystem::create_directories(runtime_directory_);
    const auto path =
        runtime_directory_ /
        ("receiver-generation-" + std::to_string(generation) + ".json");
    const auto &graph_json = snapshot.receiver_graph;
    const auto source =
        std::find_if(graph_json.at("nodes").begin(),
                     graph_json.at("nodes").end(), [](const auto &node) {
                       return node.value("id", std::string{}) == "source";
                     });
    if (source == graph_json.at("nodes").end())
      return {400, "receiver_source_missing", "binary IQ source is missing"};
    const auto &source_config = source->at("node_config");
    const auto format = source_config.value("sample_format", std::string{});
    const auto iq_path =
        std::filesystem::path(source_config.value("file_path", std::string{}));
    if ((format != "cf32_le" && format != "cf64_le") ||
        !std::filesystem::is_regular_file(iq_path))
      return {400, "receiver_input_invalid",
              "binary IQ path or sample format is invalid"};
    std::error_code size_error;
    const auto input_byte_size = std::filesystem::file_size(iq_path, size_error);
    if (size_error)
      return {400, "receiver_input_invalid",
              "binary IQ byte length is unavailable"};
    {
      std::ofstream stream(path);
      if (!stream)
        return {500, "receiver_config_write_failed",
                "could not write immutable receiver configuration"};
      stream << graph_json.dump(2);
    }
    auto candidate = graph::GraphExecutorBuilder()
                         .WithJsonConfig(path.string())
                         .WithPluginDirectory(plugin_directory_.string())
                         .WithExecutorTimeout(executor_timeout_)
                         .Build();
    if (!candidate)
      return {500, "executor_construction_failed",
              "failed to construct receiver executor"};
    auto manager = candidate->GetGraphManager();
    if (!manager)
      return {500, "graph_manager_missing",
              "receiver executor has no graph manager"};
    manager->EnableMetrics(true);
    executor_ = std::move(candidate);
    receiver_config_path_ = path;
    receiver_input_snapshot_ = {
        .path = std::filesystem::absolute(iq_path).lexically_normal(),
        .sample_format = format,
        .byte_size = input_byte_size,
        .first_complex_sample =
            source_config.value("first_complex_sample", std::uint64_t{0}),
        .max_complex_samples =
            source_config.value("max_complex_samples", std::uint64_t{0}),
        .max_read_complex_samples = source_config.value(
            "max_read_complex_samples", std::uint64_t{4'194'304}),
    };
    executor_used_ = false;
    generation_ = generation;
    return {200, "rebuild_succeeded", "receiver generation built", manager,
            false};
  } catch (const std::exception &error) {
    return {500, "executor_construction_failed", error.what()};
  }
}

graph::dashboard::IGraphRuntimeOwner::Result
FHSSGraphRuntimeOwner::Start(std::uint64_t generation,
                             std::uint64_t run_epoch) {
  const std::lock_guard operation_lock(operation_mutex_);
  if (generation != generation_ || !executor_)
    return {409, "generation_mismatch", "generation is not active"};
  if (execution_thread_.joinable()) {
    {
      const std::lock_guard completion_lock(completion_mutex_);
      if (!execution_finished_)
        return {409, "execution_active",
                "receiver execution is already active"};
    }
    execution_thread_.join();
  }
  // GraphExecutor/GraphManager instances are single-run lifecycle owners.  A
  // same-generation restart keeps the immutable receiver configuration but
  // constructs a fresh runtime, so Init is never repeated on retired nodes.
  if (executor_used_) {
    auto candidate = graph::GraphExecutorBuilder()
                         .WithJsonConfig(receiver_config_path_.string())
                         .WithPluginDirectory(plugin_directory_.string())
                         .WithExecutorTimeout(executor_timeout_)
                         .Build();
    if (!candidate)
      return {500, "executor_construction_failed",
              "failed to reconstruct receiver executor for restart"};
    auto manager = candidate->GetGraphManager();
    if (!manager)
      return {500, "graph_manager_missing",
              "restarted receiver executor has no graph manager"};
    manager->EnableMetrics(true);
    executor_ = std::move(candidate);
  }
  {
    const std::lock_guard completion_lock(completion_mutex_);
    execution_finished_ = false;
  }
  auto executor = executor_;
  // operation_mutex_ proves the previous thread has retired before this exact
  // attempt is prepared.  Reset/publish therefore happens synchronously
  // before the new worker can reach Init or Start.
  const auto execution_attempt = executor->PrepareExecutionAttempt();
  executor_used_ = true;
  const auto before_execute = before_execute_hook_;
  const auto receiver_input = receiver_input_snapshot_;
  execution_thread_ = std::jthread(
      [this, executor, generation, run_epoch, before_execute,
       receiver_input](std::stop_token) {
        if (before_execute)
          before_execute();
        const auto preflight_error = ValidateReceiverInputAttempt(receiver_input);
        auto result = preflight_error
                          ? graph::ExecutionResult{.success = false,
                                                   .message = *preflight_error}
                          : executor->Execute();
        if (result.success && !executor->IsCompletionSignaled()) {
          result = {.success = false,
                    .message = "receiver_execution_incomplete: executor "
                               "stopped before graph completion signal"};
        }
        {
          const std::lock_guard completion_lock(completion_mutex_);
          execution_finished_ = true;
        }
        completion_cv_.notify_all();
        CompletionCallback callback;
        {
          const std::lock_guard callback_lock(callback_mutex_);
          callback = completion_callback_;
        }
        if (callback)
          callback(generation, run_epoch, result.success, result.message);
      });
  // Execute() performs GraphExecutor Init and Start inside the worker. Do not
  // expose a successful owner Start until that startup sequence has crossed
  // the RUNNING publication point (or has already terminated). Otherwise an
  // immediate API Stop can race GraphExecutor::StartExpected and GraphManager
  // startup. The atomic executor state is the release/acquire milestone; the
  // completion mutex covers the immediate-terminal case.
  for (;;) {
    {
      const std::lock_guard completion_lock(completion_mutex_);
      if (execution_finished_)
        break;
    }
    if (executor->HasStartedExecution(execution_attempt))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (after_startup_hook_)
    after_startup_hook_(executor->GetGraphManager());
  return {202, "start_accepted", "receiver execution started",
          executor_->GetGraphManager(), false};
}

graph::dashboard::IGraphRuntimeOwner::Result
FHSSGraphRuntimeOwner::Stop(std::uint64_t generation) {
  const std::lock_guard operation_lock(operation_mutex_);
  if (generation != generation_ || !executor_)
    return {409, "generation_mismatch", "generation is not active"};
  // Execute() owns the GraphExecutor lifecycle on execution_thread_.  Calling
  // Stop() here would run GraphManager::Stop concurrently with Execute's own
  // Stop()/Join() path and, more importantly, would place an unbounded call in
  // front of the advertised five-second wait.  Publish only the cooperative
  // cancellation intent; the execution thread alone enters GraphExecutor's
  // teardown. GraphManager may serially republish idempotent component stops
  // immediately before joining, but no control-thread teardown runs concurrently.
  executor_->RequestStop();
  if (execution_thread_.joinable()) {
    std::unique_lock completion_lock(completion_mutex_);
    if (!completion_cv_.wait_for(completion_lock, kStopTimeout,
                                 [this] { return execution_finished_; }))
      return {504, "stop_timeout",
              "receiver did not stop within the five-second bound"};
    completion_lock.unlock();
    execution_thread_.join();
  }
  return {200, "stop_completed", "receiver execution stopped",
          executor_->GetGraphManager(), false};
}

graph::dashboard::IGraphRuntimeOwner::Result
FHSSGraphRuntimeOwner::Shutdown(std::uint64_t) {
  const std::lock_guard operation_lock(operation_mutex_);
  if (executor_)
    executor_->RequestStop();
  if (execution_thread_.joinable()) {
    std::unique_lock completion_lock(completion_mutex_);
    if (!completion_cv_.wait_for(completion_lock, kStopTimeout,
                                 [this] { return execution_finished_; }))
      return {504, "shutdown_timeout",
              "receiver did not stop within the five-second bound"};
    completion_lock.unlock();
    execution_thread_.join();
  }
  return {200, "shutdown_complete", "runtime owner shut down"};
}

void FHSSGraphRuntimeOwner::SetCompletionCallback(CompletionCallback callback) {
  const std::lock_guard callback_lock(callback_mutex_);
  completion_callback_ = std::move(callback);
}

void FHSSGraphRuntimeOwner::SetBeforeExecuteHookForTesting(
    std::function<void()> hook) {
  const std::lock_guard operation_lock(operation_mutex_);
  before_execute_hook_ = std::move(hook);
}

void FHSSGraphRuntimeOwner::SetExecutorTimeoutForTesting(
    std::chrono::seconds timeout) {
  const std::lock_guard operation_lock(operation_mutex_);
  executor_timeout_ = timeout;
}

void FHSSGraphRuntimeOwner::SetAfterStartupHookForTesting(
    std::function<void(std::shared_ptr<graph::GraphManager>)> hook) {
  const std::lock_guard operation_lock(operation_mutex_);
  after_startup_hook_ = std::move(hook);
}

std::uint64_t FHSSGraphRuntimeOwner::StopSequenceCountForTesting() const {
  const std::lock_guard operation_lock(operation_mutex_);
  return executor_ ? executor_->GetStopSequenceCount() : 0u;
}
} // namespace dsp::fhss::dashboard
