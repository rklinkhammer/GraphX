#pragma once

#include "dashboard/interfaces/DashboardCommand.hpp"

namespace app::interfaces {

/**
 * @class ICommandExecutor
 * @brief Abstract interface for executing dashboard commands
 *
 * Implemented by UI adapters to execute commands from users.
 * Decouples command execution logic from UI framework details.
 *
 * Implementations:
 * - TerminalUIAdapter - Routes through CommandRegistry and Dashboard
 * - WebUIAdapter - Processes via REST handlers
 * - CLIAdapter - Executes via CLI parser
 *
 * Thread Safety:
 * - Execute() may be called from multiple UI threads concurrently
 * - Must use appropriate synchronization (locks, atomics, etc.)
 * - Should not block the calling thread for extended periods
 *
 * Example Usage:
 * ```cpp
 * auto executor = bus->Get<ICommandExecutor>();
 *
 * DashboardCommand cmd{"pause", {}};
 * CommandResult result = executor->Execute(cmd);
 *
 * if (result.IsSuccess()) {
 *     std::cout << "Success: " << result.message << "\n";
 * } else {
 *     std::cout << "Error: " << result.message << "\n";
 * }
 * ```
 */
class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;

    /**
     * @brief Execute a dashboard command
     *
     * Called when user issues a command via the UI. Executes the command
     * and returns result to caller.
     *
     * @param cmd Command to execute (name + arguments)
     * @return CommandResult with status and optional result data
     *
     * Supported commands:
     *
     * **System Control:**
     * - `pause` - Pause graph execution
     *   - Args: none
     *   - Returns: Success with "Paused" or ExecutionFailed
     *
     * - `resume` - Resume graph execution
     *   - Args: none
     *   - Returns: Success with "Resumed" or ExecutionFailed
     *
     * - `stop` - Stop and shutdown graph
     *   - Args: none
     *   - Returns: Success with "Stopping" message
     *
     * - `status` - Get current execution status
     *   - Args: none
     *   - Returns: Success with status JSON in data field
     *   - JSON: {"state": "running|paused|stopped", "nodes_running": 5, ...}
     *
     * - `help` [command] - Get help text
     *   - Args: [optional command name]
     *   - Returns: Success with help text
     *   - If no arg: general help for all commands
     *   - If arg: help for specific command
     *
     * **Metrics:**
     * - `metrics show` [node_id] - Display metrics
     *   - Args: [optional node ID or "all"]
     *   - Returns: Success with metrics JSON in data field
     *   - If no arg: all node metrics
     *   - If arg: specific node metrics
     *
     * - `metrics export` <format> <file> - Export metrics
     *   - Args: [csv|json|txt] <filepath>
     *   - Returns: Success with filename or ExecutionFailed
     *
     * **Logging:**
     * - `logs show` [count] - Show recent logs
     *   - Args: [optional number of lines, default 100]
     *   - Returns: Success with logs in data field
     *
     * - `logs filter` <pattern> - Filter logs by pattern
     *   - Args: regex pattern
     *   - Returns: Success with filtered logs in data field
     *
     * - `logs clear` - Clear log buffer
     *   - Args: none
     *   - Returns: Success
     *
     * **Graph:**
     * - `graph show` - Display graph structure
     *   - Args: none
     *   - Returns: Success with graph DOT format in data field
     *
     * - `graph export` <format> - Export graph
     *   - Args: [dot|json|png|svg]
     *   - Returns: Success with export in data, or ExecutionFailed
     *
     * **Node Control:**
     * - `node list` - List all nodes
     *   - Args: none
     *   - Returns: Success with node list JSON in data
     *
     * - `node status` <node_id> - Get node status
     *   - Args: node_id
     *   - Returns: Success with node status JSON, or UnknownCommand
     *
     * - `node inject` <node_id> <data> - Inject test data
     *   - Args: node_id, JSON data
     *   - Returns: Success or ExecutionFailed
     *
     * @note Implementations should handle unknown commands gracefully
     *       by returning CommandStatus::UnknownCommand with helpful message
     *
     * @note Commands are case-insensitive (implementations should normalize)
     *
     * @note Long-running commands (exports, large data operations)
     *       should complete within reasonable time or return partial results
     */
    virtual CommandResult Execute(const DashboardCommand& cmd) = 0;
};

}  // namespace app::interfaces
