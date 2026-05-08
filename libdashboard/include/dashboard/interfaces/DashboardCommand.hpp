#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace app::interfaces {

/**
 * @struct DashboardCommand
 * @brief Encapsulates a user command with arguments
 *
 * UI-agnostic command structure that can be used by any UI implementation
 * (Terminal, Web, CLI) to represent user input.
 *
 * Supported commands (shared across all UIs):
 * - System Control:
 *   - "pause" - Pause graph execution
 *   - "resume" - Resume graph execution
 *   - "stop" - Stop and shutdown graph
 *   - "status" - Get current execution status
 *   - "help" [command] - Get help
 *
 * - Metrics:
 *   - "metrics show" [node] - Show metrics for node(s)
 *   - "metrics export" <format> <file> - Export metrics (csv, json)
 *
 * - Logging:
 *   - "logs show" [count] - Show recent logs
 *   - "logs filter" <pattern> - Filter logs
 *
 * - Graph:
 *   - "graph show" - Display graph structure
 *   - "graph export" <format> - Export as DOT/JSON/PNG
 */
struct DashboardCommand {
    /// Command name (e.g., "pause", "resume", "status", "help")
    std::string name;

    /// Command arguments (e.g., for "help": ["node"], for "metrics show": ["acceleration_fusion"])
    std::vector<std::string> args;

    /// Optional context ID for UI-specific tracking (e.g., "metrics_panel_3")
    /// Used by adapters to route responses back to correct UI element
    std::string context_id;

    DashboardCommand() = default;

    explicit DashboardCommand(const std::string& n)
        : name(n) {}

    DashboardCommand(const std::string& n, const std::vector<std::string>& a)
        : name(n), args(a) {}

    DashboardCommand(const std::string& n, const std::vector<std::string>& a,
                     const std::string& ctx)
        : name(n), args(a), context_id(ctx) {}

    /// Check if command has any arguments
    bool HasArgs() const { return !args.empty(); }

    /// Get first argument or empty string
    std::string GetFirstArg() const {
        return args.empty() ? "" : args[0];
    }

    /// Get argument by index with bounds checking
    std::string GetArg(size_t index, const std::string& default_val = "") const {
        return index < args.size() ? args[index] : default_val;
    }

    /// Get argument count
    size_t ArgCount() const { return args.size(); }
};

/**
 * @enum CommandStatus
 * @brief Result status of command execution
 */
enum class CommandStatus {
    Success = 0,           /// Command executed successfully
    UnknownCommand = 1,    /// Command not recognized
    InvalidArguments = 2,  /// Invalid argument count or format
    ExecutionFailed = 3,   /// Command executed but failed
    NotSupported = 4,      /// Command not supported in current state
};

/**
 * @struct CommandResult
 * @brief Result of command execution
 *
 * Returned by ICommandExecutor::Execute() to indicate
 * success/failure and provide result data or error messages.
 */
struct CommandResult {
    /// Execution status (success, error, etc.)
    CommandStatus status = CommandStatus::Success;

    /// Human-readable message for UI display
    /// For success: confirmation or result description
    /// For failure: error message with details
    std::string message;

    /// Result data in JSON format for complex results
    /// e.g., for "metrics show": JSON object with metrics
    /// e.g., for "graph show": DOT format graph description
    std::string data;

    /// Execution time in milliseconds (for diagnostics)
    uint64_t execution_time_ms = 0;

    CommandResult() = default;

    explicit CommandResult(CommandStatus s)
        : status(s) {}

    CommandResult(CommandStatus s, const std::string& msg)
        : status(s), message(msg) {}

    CommandResult(CommandStatus s, const std::string& msg, const std::string& d)
        : status(s), message(msg), data(d) {}

    /// Check if command succeeded
    bool IsSuccess() const { return status == CommandStatus::Success; }

    /// Check if command failed
    bool IsFailed() const { return status != CommandStatus::Success; }

    /// Get status as string for logging
    std::string GetStatusString() const {
        switch (status) {
            case CommandStatus::Success:
                return "Success";
            case CommandStatus::UnknownCommand:
                return "UnknownCommand";
            case CommandStatus::InvalidArguments:
                return "InvalidArguments";
            case CommandStatus::ExecutionFailed:
                return "ExecutionFailed";
            case CommandStatus::NotSupported:
                return "NotSupported";
            default:
                return "Unknown";
        }
    }
};

}  // namespace app::interfaces
