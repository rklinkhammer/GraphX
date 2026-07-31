// SPDX-License-Identifier: MIT

#pragma once

#include "graph/ExecutionState.hpp"
#include "graph/GraphConfigurationSnapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace graph {
class GraphExecutor;
}

namespace capabilities {

class MetricsCapability;
struct CommandWorkerCompletion;

enum class CommandName : std::uint8_t {
    Configure,
    Init,
    Start,
    Run,
    Stop,
    Join,
};

enum class OperationStatus : std::uint8_t {
    Accepted,
    Running,
    Completed,
    Failed,
    Cancelled,
};

struct CommandDescriptor {
    CommandName name;
    bool asynchronous;
    nlohmann::json arguments;
    std::string description;
};

struct CommandRequest {
    CommandName name;
    nlohmann::json arguments = nlohmann::json::object();
    std::optional<graph::GraphConfigurationSnapshot> configuration = std::nullopt;
    std::optional<std::uint64_t> coordinator_revision = std::nullopt;
};

struct CommandOperationResult {
    std::string operation_id;
    CommandName command;
    OperationStatus status = OperationStatus::Failed;
    bool success = false;
    bool executor_available = false;
    graph::ExecutionState executor_state = graph::ExecutionState::ERROR;
    std::uint64_t coordinator_revision = 0;
    std::optional<std::uint64_t> configured_revision;
    std::optional<std::uint64_t> active_revision;
    std::uint64_t graph_generation = 0;
    bool configuration_dirty = false;
    std::string message;
};

[[nodiscard]] std::string_view ToString(CommandName name) noexcept;
[[nodiscard]] std::string_view ToString(OperationStatus status) noexcept;
[[nodiscard]] std::optional<CommandName>
ParseCommandName(std::string_view name) noexcept;

/**
 * Typed, UI-neutral lifecycle authority.
 *
 * Synchronous transitions are serialized.  Blocking Run and teardown execute
 * on one owned joinable worker.  The executor binding is weak so the
 * executor-capability registration does not form an ownership cycle.
 */
class CommandCapability {
public:
    explicit CommandCapability(
        std::shared_ptr<MetricsCapability> metrics,
        std::size_t operation_retention = 128);
    ~CommandCapability() noexcept;

    CommandCapability(const CommandCapability&) = delete;
    CommandCapability& operator=(const CommandCapability&) = delete;

    void BindExecutor(std::weak_ptr<graph::GraphExecutor> executor);
    void Shutdown(graph::GraphExecutor* executor_override = nullptr) noexcept;

    [[nodiscard]] std::vector<CommandDescriptor> DiscoverCommands() const;
    [[nodiscard]] CommandOperationResult Submit(const CommandRequest& request);
    [[nodiscard]] std::optional<CommandOperationResult>
    GetOperation(std::string_view operation_id) const;
    [[nodiscard]] CommandOperationResult
    GetState(std::optional<std::uint64_t> coordinator_revision = std::nullopt) const;

private:
    [[nodiscard]] std::string NextOperationId();
    [[nodiscard]] CommandOperationResult
    SnapshotResult(CommandName command, std::string operation_id,
                   OperationStatus status, bool success,
                   std::string message,
                   const std::shared_ptr<graph::GraphExecutor>& executor) const;
    void StoreOperationLocked(const CommandOperationResult& result);
    void ClearOperationsLocked();
    void StartRunWorkerLocked(
        const std::shared_ptr<graph::GraphExecutor>& executor,
        const std::string& run_operation_id);
    void StartTeardownWorkerLocked(
        const std::shared_ptr<graph::GraphExecutor>& executor);
    void CompleteWorker(
        graph::GraphExecutor* executor,
        const std::optional<std::string>& run_operation_id,
        bool run_succeeded, std::string message);

    std::weak_ptr<graph::GraphExecutor> executor_;
    std::shared_ptr<MetricsCapability> metrics_;
    const std::size_t operation_retention_;

    mutable std::mutex mutex_;
    std::condition_variable worker_condition_;
    std::shared_ptr<CommandWorkerCompletion> active_completion_;
    bool worker_active_ = false;
    bool stop_requested_ = false;
    std::uint64_t next_operation_id_ = 1;
    std::deque<std::string> operation_order_;
    std::unordered_map<std::string, CommandOperationResult> operations_;
    std::vector<std::string> stop_operations_;
    std::vector<std::string> join_operations_;
};

}  // namespace capabilities
