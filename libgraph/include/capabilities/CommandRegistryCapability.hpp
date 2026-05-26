// MIT License
//
// Copyright (c) 2026 graphlib contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include "ui/CommandRegistry.hpp"

namespace capabilities {

/**
 * @class CommandRegistryCapability
 * @brief Capability wrapper for CommandRegistry with no Dashboard dependency
 *
 * Provides unified access to command registration and execution through
 * the CapabilityBus. Allows multiple policies to register custom commands
 * without direct dependencies on CommandRegistry.
 *
 * Thread Safety:
 * - RegisterCommand() is thread-safe (locks internal mutex)
 * - GetRegistry() returns const reference (read-only after initialization)
 * - Command execution via registry is thread-safe
 *
 * Lifecycle:
 * - Created in CommandPolicy::OnInit()
 * - Registered in CapabilityBus
 * - Available to all subsequent policies
 * - Destroyed with CapabilityBus cleanup
 *
 * Usage:
 * ```cpp
 * // In CommandPolicy::OnInit()
 * auto cmd_registry_cap = std::make_shared<CommandRegistryCapability>();
 * context.GetCapabilityBus()
 *     .Register<CommandRegistryCapability>(cmd_registry_cap);
 *
 * // In other policies::OnInit()
 * auto cmd_registry_cap = context.GetCapabilityBus()
 *     .Get<CommandRegistryCapability>();
 * if (cmd_registry_cap) {
 *     cmd_registry_cap->RegisterCommand(
 *         "my_command",
 *         "Description of my command",
 *         "Usage: my_command [args]",
 *         [](const auto& args) { return CommandResult(true, "OK"); }
 *     );
 * }
 * ```
 *
 * @see CommandRegistry, CommandResult, CommandInfo, IExecutionPolicy
 */
class CommandRegistryCapability {
public:
    /**
     * @brief Construct with an empty CommandRegistry
     */
    CommandRegistryCapability()
        : registry_(std::make_shared<CommandRegistry>()) {}

    /**
     * @brief Construct with an existing CommandRegistry
     *
     * @param registry Shared pointer to existing CommandRegistry instance
     * @throws std::invalid_argument if registry is nullptr
     */
    explicit CommandRegistryCapability(std::shared_ptr<CommandRegistry> registry)
        : registry_(registry) {
        if (!registry) {
            throw std::invalid_argument("CommandRegistry cannot be null");
        }
    }

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CommandRegistryCapability() = default;

    // ========================================================================
    // Command Registration
    // ========================================================================

    /**
     * @brief Register a new command with the registry
     *
     * Thread-safe wrapper around CommandRegistry::RegisterCommand().
     * Allows multiple policies to register commands without conflicts.
     *
     * @param name Command name (must be unique, case-sensitive)
     * @param description One-line description of what command does
     * @param usage Usage format string (e.g., "command [options] [args]")
     * @param handler Function to execute when command is invoked
     *
     * @return true if command registered successfully
     * @return false if command already exists or parameters invalid
     *
     * @note Name and description should not be empty
     * @note Handler should return valid CommandResult
     *
     * Example:
     * ```cpp
     * auto result = cmd_registry_cap->RegisterCommand(
     *     "pause",
     *     "Pause graph execution",
     *     "pause",
     *     [&graph](const auto& args) {
     *         graph->Pause();
     *         return CommandResult(true, "Graph paused");
     *     }
     * );
     * if (!result) {
     *     LOG_ERROR("Failed to register pause command");
     * }
     * ```
     *
     * @see CommandResult, CommandHandler, CommandInfo
     */
    bool RegisterCommand(
        const std::string& name,
        const std::string& description,
        const std::string& usage,
        CommandHandler handler) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->RegisterCommand(name, description, usage, handler);
    }

    // ========================================================================
    // Command Execution
    // ========================================================================

    /**
     * @brief Execute a registered command by name with parsed arguments
     *
     * Thread-safe execution of command handler.
     *
     * @param name Command name to execute
     * @param args Command arguments (may be empty)
     *
     * @return CommandResult with success status and output/error message
     *
     * Example:
     * ```cpp
     * auto result = cmd_registry_cap->ExecuteCommand("status", {});
     * if (result.success) {
     *     logger->info("Command output: {}", result.message);
     * } else {
     *     logger->error("Command failed: {}", result.message);
     * }
     * ```
     *
     * @see CommandResult, CommandHandler
     */
    CommandResult ExecuteCommand(
        const std::string& name,
        const std::vector<std::string>& args) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->ExecuteCommand(name, args);
    }

    // ========================================================================
    // Command Query & Discovery
    // ========================================================================

    /**
     * @brief Check if a command is registered
     *
     * @param name Command name to check
     * @return true if command exists, false otherwise
     */
    bool HasCommand(const std::string& name) const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->HasCommand(name);
    }

    /**
     * @brief Get metadata for a specific command
     *
     * @param name Command name to query
     * @return Pointer to CommandInfo if found, nullptr otherwise
     *
     * @note Returned pointer valid only during current call
     * @note Do not store returned pointer; copy data instead
     */
    const CommandInfo* GetCommandInfo(const std::string& name) const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->GetCommandInfo(name);
    }

    /**
     * @brief Get all registered commands
     *
     * Thread-safe snapshot of all commands at time of call.
     * Safe for use with C++20+ std::ranges operations.
     *
     * @return Vector of all CommandInfo structures
     *
     * Example with ranges (C++20):
     * ```cpp
     * auto all_cmds = cmd_registry_cap->GetAllCommands();
     * 
     * // Filter commands by prefix
     * auto matching = all_cmds
     *     | std::views::filter([prefix](const auto& cmd) {
     *         return app::ranges::StartsWithI(cmd.name, prefix);
     *     })
     *     | std::ranges::to<std::vector>();
     * ```
     *
     * @see RangesUtilities.hpp for helper functions
     */
    std::vector<CommandInfo> GetAllCommands() const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->GetAllCommands();
    }

    // ========================================================================
    // Registry Access
    // ========================================================================

    /**
     * @brief Get const reference to underlying CommandRegistry
     *
     * Provides direct access to CommandRegistry if needed.
     * Use this only for operations not exposed by this capability.
     *
     * @return Const reference to CommandRegistry
     *
     * @note For most operations, use this capability's methods instead
     * @note Safe to call after construction
     */
    const CommandRegistry& GetRegistry() const {
        return *registry_;
    }

    /**
     * @brief Get shared pointer to underlying CommandRegistry
     *
     * Returns the managed CommandRegistry instance.
     * Use for advanced operations or delegation to other components.
     *
     * @return Shared pointer to CommandRegistry
     *
     * @note Caller must not call Delete on returned pointer
     * @note Safe for thread-safe access via this capability's methods
     */
    std::shared_ptr<CommandRegistry> GetRegistryPtr() const {
        return registry_;
    }

    // ========================================================================
    // Capability Metadata
    // ========================================================================

    /**
     * @brief Get human-readable capability name
     *
     * @return String identifier for logging/debugging
     */
    static constexpr const char* GetCapabilityName() {
        return "CommandRegistryCapability";
    }

    /**
     * @brief Check if capability is ready for use
     *
     * @return true if CommandRegistry is initialized, false otherwise
     */
    bool IsReady() const {
        return registry_ != nullptr;
    }

private:
    /// Underlying CommandRegistry instance
    std::shared_ptr<CommandRegistry> registry_;

    /// Synchronization for thread-safe operations
    mutable std::mutex registry_mutex_;
};

}  // namespace capabilities
