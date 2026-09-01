// SPDX-License-Identifier: MIT

#include "capabilities/CommandCapability.hpp"

#include "capabilities/MetricsCapability.hpp"
#include "graph/GraphExecutor.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace capabilities {

struct CommandWorkerCompletion {
    std::mutex mutex;
    std::condition_variable condition;
    bool complete = false;
    bool joined = false;
};

}  // namespace capabilities

namespace {

class CommandWorkerReaper {
public:
    CommandWorkerReaper()
        : reaper_([this] { Run(); }) {}

    ~CommandWorkerReaper() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        reaper_.join();
    }

    void Adopt(std::thread thread,
               std::shared_ptr<graph::GraphExecutor> executor,
               std::shared_ptr<capabilities::CommandWorkerCompletion>
                   completion) {
        {
            std::scoped_lock lock(mutex_);
            jobs_.push_back({.thread = std::move(thread),
                             .executor = std::move(executor),
                             .completion = std::move(completion)});
        }
        condition_.notify_one();
    }

private:
    struct Job {
        std::thread thread;
        std::shared_ptr<graph::GraphExecutor> executor;
        std::shared_ptr<capabilities::CommandWorkerCompletion> completion;
    };

    void Run() {
        while (true) {
            std::optional<Job> completed;
            {
                std::unique_lock lock(mutex_);
                if (jobs_.empty()) {
                    condition_.wait(lock, [this] {
                        return stopping_ || !jobs_.empty();
                    });
                } else {
                    condition_.wait_for(
                        lock, std::chrono::milliseconds(10));
                }
                for (auto job = jobs_.begin(); job != jobs_.end(); ++job) {
                    bool is_complete = false;
                    {
                        std::scoped_lock completion_lock(
                            job->completion->mutex);
                        is_complete = job->completion->complete;
                    }
                    if (!is_complete &&
                        (stopping_ || job->executor.use_count() == 1)) {
                        job->executor->RequestStop();
                    }
                    if (is_complete) {
                        completed.emplace(std::move(*job));
                        jobs_.erase(job);
                        break;
                    }
                }
                if (!completed && jobs_.empty() && stopping_) {
                    return;
                }
            }
            if (completed) {
                completed->thread.join();
                {
                    std::scoped_lock lock(
                        completed->completion->mutex);
                    completed->completion->joined = true;
                }
                completed->completion->condition.notify_all();
                completed->executor.reset();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
    std::thread reaper_;
};

CommandWorkerReaper& WorkerReaper() {
    static CommandWorkerReaper reaper;
    return reaper;
}

}  // namespace

namespace capabilities {

std::string_view ToString(const CommandName name) noexcept {
    switch (name) {
    case CommandName::Configure:
        return "configure";
    case CommandName::Init:
        return "init";
    case CommandName::Start:
        return "start";
    case CommandName::Run:
        return "run";
    case CommandName::Stop:
        return "stop";
    case CommandName::Join:
        return "join";
    }
    return "unknown";
}

std::string_view ToString(const OperationStatus status) noexcept {
    switch (status) {
    case OperationStatus::Accepted:
        return "accepted";
    case OperationStatus::Running:
        return "running";
    case OperationStatus::Completed:
        return "completed";
    case OperationStatus::Failed:
        return "failed";
    case OperationStatus::Cancelled:
        return "cancelled";
    }
    return "failed";
}

std::optional<CommandName>
ParseCommandName(const std::string_view name) noexcept {
    if (name == "configure") {
        return CommandName::Configure;
    }
    if (name == "init") {
        return CommandName::Init;
    }
    if (name == "start") {
        return CommandName::Start;
    }
    if (name == "run") {
        return CommandName::Run;
    }
    if (name == "stop") {
        return CommandName::Stop;
    }
    if (name == "join") {
        return CommandName::Join;
    }
    return std::nullopt;
}

CommandCapability::CommandCapability(
    std::shared_ptr<MetricsCapability> metrics,
    const std::size_t operation_retention)
    : metrics_(std::move(metrics)),
      operation_retention_(operation_retention == 0 ? 1 : operation_retention) {}

CommandCapability::~CommandCapability() noexcept {
    Shutdown();
}

void CommandCapability::Shutdown(
    graph::GraphExecutor* const executor_override) noexcept {
    std::shared_ptr<graph::GraphExecutor> executor;
    std::shared_ptr<CommandWorkerCompletion> completion;
    std::unique_lock lock(mutex_);
    executor = executor_.lock();
    auto* const executor_ptr =
        executor_override ? executor_override : executor.get();
    if (executor_ptr && worker_active_) {
            stop_requested_ = true;
            executor_ptr->RequestStop();
    }
    worker_condition_.wait(lock, [this] { return !worker_active_; });
    completion = active_completion_;
    lock.unlock();
    if (completion) {
        std::unique_lock completion_lock(completion->mutex);
        completion->condition.wait(
            completion_lock,
            [&completion] { return completion->joined; });
    }
}

void CommandCapability::BindExecutor(
    std::weak_ptr<graph::GraphExecutor> executor) {
    std::scoped_lock lock(mutex_);
    executor_ = std::move(executor);
}

std::vector<CommandDescriptor> CommandCapability::DiscoverCommands() const {
    const auto no_arguments = nlohmann::json::object();
    return {
        {CommandName::Configure, false, no_arguments,
         "Record the coordinator's current immutable snapshot"},
        {CommandName::Init, false, no_arguments,
         "Construct and initialize the configured graph"},
        {CommandName::Start, false, no_arguments,
         "Start the initialized graph"},
        {CommandName::Run, true, no_arguments,
         "Run until natural completion or cancellation"},
        {CommandName::Stop, true, no_arguments,
         "Request cooperative stop and complete teardown"},
        {CommandName::Join, true, no_arguments,
         "Observe the active teardown operation"},
    };
}

std::string CommandCapability::NextOperationId() {
    return "op-" + std::to_string(next_operation_id_++);
}

CommandOperationResult CommandCapability::SnapshotResult(
    const CommandName command, std::string operation_id,
    const OperationStatus status, const bool success, std::string message,
    const std::shared_ptr<graph::GraphExecutor>& executor) const {
    CommandOperationResult result;
    result.operation_id = std::move(operation_id);
    result.command = command;
    result.status = status;
    result.success = success;
    result.executor_available = static_cast<bool>(executor);
    result.message = std::move(message);
    if (executor) {
        result.executor_state = executor->GetExecutionState();
        result.coordinator_revision = executor->GetCoordinatorRevision();
        result.configured_revision = executor->GetConfiguredRevision();
        result.active_revision = executor->GetActiveRevision();
        result.graph_generation = executor->GetGraphGeneration();
        result.configuration_dirty = executor->IsConfigurationDirty();
    }
    return result;
}

void CommandCapability::StoreOperationLocked(const CommandOperationResult& result) {
    operations_.insert_or_assign(result.operation_id, result);
    operation_order_.push_back(result.operation_id);
    while (operation_order_.size() > operation_retention_) {
        const auto expired_operation_id = operation_order_.front();
        operations_.erase(expired_operation_id);
        std::erase(stop_operations_, expired_operation_id);
        std::erase(join_operations_, expired_operation_id);
        operation_order_.pop_front();
    }
}

void CommandCapability::ClearOperationsLocked() {
    operations_.clear();
    operation_order_.clear();
    stop_operations_.clear();
    join_operations_.clear();
    stop_requested_ = false;
}

void CommandCapability::StartRunWorkerLocked(
    const std::shared_ptr<graph::GraphExecutor>& executor,
    const std::string& run_operation_id) {
    worker_active_ = true;
    stop_requested_ = false;
    auto* const executor_ptr = executor.get();
    auto completion =
        std::make_shared<CommandWorkerCompletion>();
    active_completion_ = completion;
    std::thread worker(
        [this, executor_ptr, run_operation_id, completion] {
            {
                std::scoped_lock lock(mutex_);
                if (auto found = operations_.find(run_operation_id);
                    found != operations_.end()) {
                    found->second.status = OperationStatus::Running;
                }
            }
            const auto run = executor_ptr->Run();
            CompleteWorker(executor_ptr, run_operation_id, run.success,
                           run.message);
            {
                std::scoped_lock lock(completion->mutex);
                completion->complete = true;
            }
            completion->condition.notify_all();
        });
    WorkerReaper().Adopt(
        std::move(worker), executor, std::move(completion));
}

void CommandCapability::StartTeardownWorkerLocked(
    const std::shared_ptr<graph::GraphExecutor>& executor) {
    worker_active_ = true;
    stop_requested_ = true;
    auto* const executor_ptr = executor.get();
    auto completion =
        std::make_shared<CommandWorkerCompletion>();
    active_completion_ = completion;
    std::thread worker(
        [this, executor_ptr, completion] {
            CompleteWorker(executor_ptr, std::nullopt, true,
                           "Stop requested");
            {
                std::scoped_lock lock(completion->mutex);
                completion->complete = true;
            }
            completion->condition.notify_all();
        });
    WorkerReaper().Adopt(
        std::move(worker), executor, std::move(completion));
}

void CommandCapability::CompleteWorker(
    graph::GraphExecutor* const executor,
    const std::optional<std::string>& run_operation_id,
    const bool run_succeeded, std::string message) {
    const auto stop = executor->Stop();
    const auto join = stop.success ? executor->Join() : graph::ExecutionResult{};
    const bool teardown_succeeded = stop.success && join.success;
    const std::shared_ptr<graph::GraphExecutor> executor_observer(
        executor, [](graph::GraphExecutor*) {});

    std::scoped_lock lock(mutex_);
    if (run_operation_id) {
        if (auto found = operations_.find(*run_operation_id);
            found != operations_.end()) {
            found->second = SnapshotResult(
                CommandName::Run, *run_operation_id,
                !run_succeeded || !teardown_succeeded
                    ? OperationStatus::Failed
                    : (stop_requested_ ? OperationStatus::Cancelled
                                       : OperationStatus::Completed),
                run_succeeded && teardown_succeeded,
                !run_succeeded ? std::move(message)
                               : (stop_requested_ ? "Run cancelled by stop"
                                                  : "Run completed"),
                executor_observer);
        }
    }
    for (const auto& operation_id : stop_operations_) {
        if (auto found = operations_.find(operation_id);
            found != operations_.end()) {
            found->second = SnapshotResult(
                CommandName::Stop, operation_id,
                teardown_succeeded ? OperationStatus::Completed
                                   : OperationStatus::Failed,
                teardown_succeeded,
                teardown_succeeded ? "Graph stopped and joined"
                                   : "Graph teardown failed",
                executor_observer);
        }
    }
    for (const auto& operation_id : join_operations_) {
        if (auto found = operations_.find(operation_id);
            found != operations_.end()) {
            found->second = SnapshotResult(
                CommandName::Join, operation_id,
                teardown_succeeded ? OperationStatus::Completed
                                   : OperationStatus::Failed,
                teardown_succeeded,
                teardown_succeeded ? "Graph teardown joined"
                                   : "Graph teardown failed",
                executor_observer);
        }
    }
    stop_operations_.clear();
    join_operations_.clear();
    worker_active_ = false;
    stop_requested_ = false;
    worker_condition_.notify_all();
}

CommandOperationResult CommandCapability::Submit(const CommandRequest& request) {
    std::shared_ptr<CommandWorkerCompletion> prior_completion;
    {
        std::scoped_lock lock(mutex_);
        if (!worker_active_) {
            prior_completion = active_completion_;
        }
    }
    if (prior_completion) {
        std::unique_lock lock(prior_completion->mutex);
        prior_completion->condition.wait(
            lock, [&prior_completion] {
                return prior_completion->joined;
            });
        std::scoped_lock capability_lock(mutex_);
        if (active_completion_ == prior_completion) {
            active_completion_.reset();
        }
    }

    auto executor = executor_.lock();
    if (!executor) {
        return SnapshotResult(request.name, {}, OperationStatus::Failed, false,
                              "GraphExecutor is unavailable", executor);
    }
    if (request.coordinator_revision) {
        executor->ObserveCoordinatorRevision(*request.coordinator_revision);
    }

    if (request.name == CommandName::Stop) {
        std::scoped_lock lock(mutex_);
        const auto state = executor->GetExecutionState();
        const auto operation_id = NextOperationId();
        if (state != graph::ExecutionState::INITIALIZED &&
            state != graph::ExecutionState::RUNNING) {
            return SnapshotResult(
                request.name, operation_id,
                OperationStatus::Failed, false,
                "stop requires INITIALIZED or RUNNING state",
                executor);
        }
        if (stop_requested_) {
            return SnapshotResult(
                request.name, operation_id,
                OperationStatus::Failed, false,
                "stop is already in progress", executor);
        }
        stop_requested_ = true;
        auto accepted = SnapshotResult(
            request.name, operation_id,
            OperationStatus::Accepted, true,
            "Stop accepted", executor);
        StoreOperationLocked(accepted);
        stop_operations_.push_back(operation_id);
        executor->RequestStop();
        if (!worker_active_) {
            StartTeardownWorkerLocked(executor);
        }
        return accepted;
    }

    std::scoped_lock lock(mutex_);
    const std::string operation_id = NextOperationId();
    if (stop_requested_ && request.name != CommandName::Join) {
        return SnapshotResult(
            request.name, operation_id,
            OperationStatus::Failed, false,
            "graph teardown is already in progress", executor);
    }

    switch (request.name) {
    case CommandName::Configure: {
        if (!request.configuration) {
            return SnapshotResult(
                request.name, operation_id, OperationStatus::Failed, false,
                "configure requires an immutable graph snapshot", executor);
        }
        const auto configured =
            executor->ConfigureGraph(*request.configuration);
        auto result = SnapshotResult(
            request.name, operation_id,
            configured.success ? OperationStatus::Completed
                               : OperationStatus::Failed,
            configured.success, configured.message, executor);
        if (configured.success) {
            ClearOperationsLocked();
        }
        return result;
    }
    case CommandName::Init: {
        const auto initialized = executor->Init();
        return SnapshotResult(
            request.name, operation_id,
            initialized.success ? OperationStatus::Completed
                                : OperationStatus::Failed,
            initialized.success, initialized.message, executor);
    }
    case CommandName::Start: {
        const auto started = executor->Start();
        return SnapshotResult(
            request.name, operation_id,
            started.success ? OperationStatus::Completed
                            : OperationStatus::Failed,
            started.success, started.message, executor);
    }
    case CommandName::Run: {
        if (executor->GetExecutionState() != graph::ExecutionState::RUNNING ||
            executor->IsConfigurationDirty() || worker_active_) {
            return SnapshotResult(
                request.name, operation_id, OperationStatus::Failed, false,
                "run requires a clean RUNNING executor with no active worker",
                executor);
        }
        auto result = SnapshotResult(
            request.name, operation_id, OperationStatus::Accepted, true,
            "Run accepted", executor);
        StoreOperationLocked(result);
        StartRunWorkerLocked(executor, operation_id);
        return result;
    }
    case CommandName::Stop: {
        return SnapshotResult(
            request.name, operation_id,
            OperationStatus::Failed, false,
            "stop dispatch failed", executor);
    }
    case CommandName::Join: {
        const auto state = executor->GetExecutionState();
        if (state == graph::ExecutionState::ERROR) {
            return SnapshotResult(
                request.name, operation_id, OperationStatus::Failed, false,
                "join is unavailable in terminal ERROR state", executor);
        }
        if (state == graph::ExecutionState::STOPPED) {
            const auto joined = executor->Join();
            return SnapshotResult(
                request.name, operation_id,
                joined.success ? OperationStatus::Completed
                               : OperationStatus::Failed,
                joined.success, joined.message, executor);
        }
        if (!worker_active_ ||
            (!stop_requested_ &&
             state != graph::ExecutionState::STOPPING)) {
            return SnapshotResult(
                request.name, operation_id, OperationStatus::Failed, false,
                "join requires active teardown or joined STOPPED state",
                executor);
        }
        auto result = SnapshotResult(
            request.name, operation_id, OperationStatus::Accepted, true,
            "Join observation accepted", executor);
        StoreOperationLocked(result);
        join_operations_.push_back(operation_id);
        return result;
    }
    }
    return SnapshotResult(request.name, operation_id, OperationStatus::Failed,
                          false, "Unknown command", executor);
}

std::optional<CommandOperationResult> CommandCapability::GetOperation(
    const std::string_view operation_id) const {
    std::scoped_lock lock(mutex_);
    const auto found = operations_.find(std::string{operation_id});
    if (found == operations_.end()) {
        return std::nullopt;
    }
    return found->second;
}

CommandOperationResult CommandCapability::GetState(
    const std::optional<std::uint64_t> coordinator_revision) const {
    auto executor = executor_.lock();
    if (executor && coordinator_revision) {
        executor->ObserveCoordinatorRevision(*coordinator_revision);
    }
    return SnapshotResult(CommandName::Configure, {},
                          OperationStatus::Completed,
                          static_cast<bool>(executor),
                          executor ? "Executor state" : "GraphExecutor unavailable",
                          executor);
}

}  // namespace capabilities
