// SPDX-License-Identifier: MIT

/**
 * @file CommandCapability.hpp
 * @brief Command Capability Graph runtime support.
 *
 * @details Provides capability API used to share runtime services between policies, nodes, and executors. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#pragma once

#include <string>
#include <memory>
#include <functional>
#include "core/ActiveQueue.hpp"

namespace capabilities {

/**
 * @class CommandCapability
 * @brief Manages command execution with UI-agnostic queue interface
 *
 * Completely UI-agnostic:
 * - No CommandRegistry, ncurses, ftxui, or Dashboard dependencies
 * - Just a thread-safe queue for command passing
 * - UI adapters (Terminal, Web, CLI) connect to this queue
 *
 * Responsibilities:
 * - Accept commands from UI adapters via queue
 * - Process commands asynchronously
 * - Report command results back to UI via results queue
 *
 * @note This capability is the UI-agnostic counterpart to
 * the Terminal UI's CommandRegistry. Different UI adapters
 * can queue commands and receive results through this interface.
 */
/**
 * @class CommandCapability
 * @brief Command Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class CommandCapability {
public:
    /**
     * @brief Executes the Command Capability operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CommandCapability() = default;
    /**
     * @brief Releases resources owned by Command Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~CommandCapability() = default;

    /**
     * Enqueue a command for processing
     * @param command Command string to execute
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
     * Enqueue a command result for UI consumption
     * @param result Result message or output from command
     * @return true if enqueued successfully, false if queue full
     */
    bool EnqueueResult(const std::string& result) {
        return result_queue_.Enqueue(result);
    }

    /**
     * Dequeue the next command result
     * @param result Output parameter to receive result string
     * @return true if result was available, false if queue empty
     */
    bool DequeueResult(std::string& result) {
        return result_queue_.Dequeue(result);
    }

    /**
     * Disable the command queue (signals to stop accepting commands)
     */
    void DisableCommandQueue() {
        command_queue_.Disable();
    }

    /**
     * Disable the result queue (signals UI to stop reading results)
     */
    void DisableResultQueue() {
        result_queue_.Disable();
    }

private:
    /// Command queue: UI adapters → Command processor
    core::ActiveQueue<std::string> command_queue_;

    /// Result queue: Command processor → UI adapters
    core::ActiveQueue<std::string> result_queue_;
};

}  // namespace capabilities
