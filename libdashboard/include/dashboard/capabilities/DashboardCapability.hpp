#pragma once

#include <string>
#include <memory>
#include "core/ActiveQueue.hpp"

namespace app::capabilities {

/**
 * @class DashboardCapability
 * @brief Manages command and log queues between business logic and UI
 *
 * Completely UI-agnostic:
 * - No ncurses, ftxui, or Dashboard dependencies
 * - Just thread-safe queues for command/log passing
 * - UI adapters (Terminal, Web, CLI) connect to these queues
 *
 * Responsibilities:
 * - Enqueue commands from UI to business logic
 * - Dequeue commands from business logic for execution
 * - Enqueue log messages from business logic to UI
 * - Dequeue log messages from UI for display
 */
class DashboardCapability {
public:
    DashboardCapability() = default;
    virtual ~DashboardCapability() = default;

    /**
     * Enqueue a command from the UI for execution
     * @param command Command string (e.g., "pause", "resume", "status")
     * @return true if enqueued successfully, false if queue full
     */
    bool EnqueueCommand(const std::string& command) {
        return command_queue_.Enqueue(command);
    }

    /**
     * Dequeue the next pending command
     * @param command Output parameter to receive command string
     * @return true if command was available, false if queue empty
     */
    bool DequeueCommand(std::string& command) {
        return command_queue_.Dequeue(command);
    }

    /**
     * Enqueue a log message for the UI to display
     * @param line Log message text
     * @return true if enqueued successfully, false if queue full
     */
    bool AddLog(const std::string& line) {
        return log_queue_.Enqueue(line);
    }

    /**
     * Disable the log queue (signals UI to stop reading)
     */
    void DisableLogQueue() {
        log_queue_.Disable();
    }

    /**
     * Dequeue the next pending log message
     * @param line Output parameter to receive log text
     * @return true if message was available, false if queue empty
     */
    bool DequeueLogQueue(std::string& line) {
        return log_queue_.Dequeue(line);
    }

    /**
     * Disable the command queue (signals to stop accepting commands)
     */
    void DisableCommandQueue() {
        command_queue_.Disable();
    }

private:
    /// Command queue: UI → Business Logic
    core::ActiveQueue<std::string> command_queue_;

    /// Log queue: Business Logic → UI
    core::ActiveQueue<std::string> log_queue_;
};

}  // namespace app::capabilities
