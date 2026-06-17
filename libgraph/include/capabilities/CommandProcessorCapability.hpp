/**
 * @file CommandProcessorCapability.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
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

#include "graph/ICommandProcessor.hpp"
#include <memory>
#include <mutex>
#include <stdexcept>

namespace capabilities {

// Forward declaration
class CommandRegistryCapability;

/**
 * @brief Capability wrapper for command processing
 *
 * Registers an ICommandProcessor in the capability bus, allowing
 * CommandPolicy and other policies to process commands via a pluggable
 * processor abstraction.
 *
 * **Key Features**:
 * - Pluggable processor implementations (interface-based)
 * - Thread-safe public API (mutex-protected)
 * - Default processor wraps CommandRegistryCapability
 * - Custom processors can add logging, caching, async, etc.
 *
 * **Thread Safety**:
 * All public methods protected with std::lock_guard<std::mutex>.
 * Internal processor is stored as shared_ptr (atomic increment/decrement).
 *
 * **Phase 2 Implementation**:
 * Enables command processing abstraction separate from registry.
 * Supports:
 * - Raw command string processing (parsing + validation + execution)
 * - Pre-parsed command processing (programmatic invocation)
 * - Command discovery (HasCommand, GetCommandInfo, GetAllCommands)
 * - Custom processor implementations via ICommandProcessor interface
 *
 * **Usage Pattern**:
 * ```cpp
 * // Create with default processor (wraps registry)
 * auto proc_cap = std::make_shared<CommandProcessorCapability>();
 * context.GetCapabilityBus().Register<CommandProcessorCapability>(proc_cap);
 *
 * // Or create with custom processor
 * auto custom_proc = std::make_shared<MyCustomProcessor>(...);
 * auto proc_cap = std::make_shared<CommandProcessorCapability>(custom_proc);
 *
 * // Use from any policy
 * auto result = proc_cap->ProcessCommand("help status");
 * if (result.success) {
 *     LOG_INFO(result.message);
 * } else {
 *     LOG_ERROR(result.message);
 * }
 * ```
 *
 * @see ICommandProcessor (abstract processor interface)
 * @see DefaultCommandProcessor (reference implementation)
 * @see CommandPolicy (primary consumer)
 */
/**
 * @class CommandProcessorCapability
 * @brief CommandProcessorCapability class.
 */
class CommandProcessorCapability {
public:
    /**
     * @brief Construct with explicit processor
     *
     * Creates capability wrapping a custom ICommandProcessor implementation.
     * Useful for:
     * - Testing with mock processors
     * - Custom processing logic (caching, logging, async)
     * - Replacing default processor
     *
     * @param processor Custom ICommandProcessor implementation
     *
     * @throws std::invalid_argument if processor is null
     */
    explicit CommandProcessorCapability(
        std::shared_ptr<graph::ICommandProcessor> processor);

    /**
     * @brief Construct with default processor
     *
     * Creates CommandProcessorCapability with DefaultCommandProcessor,
     * which wraps CommandRegistryCapability.
     *
     * Requires CommandRegistryCapability to be registered in bus
     * (should happen via CommandPolicy::OnInit()).
     *
     * **Initialization Sequence**:
     * 1. CommandPolicy::OnInit() → creates CommandRegistryCapability
     * 2. CommandPolicy::OnInit() → creates CommandProcessorCapability() 
     *    (auto-creates DefaultCommandProcessor)
     * 3. Processor ready for command execution
     *
     * @throws std::invalid_argument if registry capability not available
     *
     * @see CommandPolicy::OnInit() (call site)
     * @see DefaultCommandProcessor (created processor)
     */
    CommandProcessorCapability();

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CommandProcessorCapability() = default;

    // =========================================================================
    // Command Processing (delegates to processor)
    // =========================================================================

    /**
     * @brief Process raw command string
     *
     * Parses and executes command in one call.
     * Thread-safe.
     *
     * @param raw_command Full command string (e.g., "help status")
     * @return CommandResult with success flag and message
     *
     * @throws std::exception if processor throws (depends on implementation)
     */
/**
 * @brief Process command.
 * @param raw_command Parameter for process command.
 * @return Result of the operation.
 */
    CommandResult ProcessCommand(const std::string& raw_command) const;

    /**
     * @brief Process pre-parsed command
     *
     * Directly executes command by name and arguments.
     * Thread-safe.
     *
     * @param name Command name
     * @param args Command arguments
     * @return CommandResult from handler or error if command not found
     *
     * @throws std::exception if processor throws (depends on implementation)
     */
    CommandResult ProcessCommand(
        const std::string& name,
        const std::vector<std::string>& args) const;

    // =========================================================================
    // Command Discovery (delegates to processor)
    // =========================================================================

    /**
     * @brief Check if command exists
     *
     * Thread-safe.
     *
     * @param name Command name
     * @return true if command is registered
     */
/**
 * @brief Has command.
 * @param name Parameter for has command.
 * @return Result of the operation.
 */
    bool HasCommand(const std::string& name) const;

    /**
     * @brief Get command metadata
     *
     * Thread-safe.
     *
     * @param name Command name
     * @return Pointer to CommandInfo or nullptr if not found
     */
/**
 * @brief Get command info.
 * @param name Parameter for get command info.
 * @return Result of the operation.
 */
    const CommandInfo* GetCommandInfo(const std::string& name) const;

    /**
     * @brief Get all registered commands
     *
     * Thread-safe (returns copy of vector).
     *
     * @return Vector of all CommandInfo structures
     */
/**
 * @brief Get all commands.
 * @return Result of the operation.
 */
    std::vector<CommandInfo> GetAllCommands() const;

    // =========================================================================
    // Processor Access
    // =========================================================================

    /**
     * @brief Get underlying processor implementation
     *
     * Returns shared_ptr to ICommandProcessor for advanced usage.
     * Useful for:
     * - Querying processor type (dynamic_cast for specific features)
     * - Storing processor reference
     * - Testing processor directly
     *
     * Thread-safe (shared_ptr increment/decrement is atomic).
     *
     * @return Shared pointer to ICommandProcessor
     */
/**
 * @brief Get processor.
 * @return Result of the operation.
 */
    std::shared_ptr<graph::ICommandProcessor> GetProcessor() const;

    /**
     * @brief Replace processor at runtime
     *
     * Swaps to a new processor implementation.
     * Useful for:
     * - Dynamic processor switching
     * - Updating processor strategy
     * - Testing processor swapping
     *
     * Thread-safe.
     *
     * @param new_processor New ICommandProcessor implementation
     *
     * @throws std::invalid_argument if new_processor is null
     */
/**
 * @brief Set processor.
 * @param new_processor Parameter for set processor.
 */
    void SetProcessor(std::shared_ptr<graph::ICommandProcessor> new_processor);

    // =========================================================================
    // Metadata
    // =========================================================================

    /**
     * @brief Check if capability is ready for use
     *
     * @return true if processor is initialized and ready
     */
    bool IsReady() const { return processor_ != nullptr; }

    /**
     * @brief Get capability name for debugging
     *
     * @return String identifier: "CommandProcessorCapability"
     */
    std::string GetCapabilityName() const {
        return "CommandProcessorCapability";
    }

private:
    std::shared_ptr<graph::ICommandProcessor> processor_;
    mutable std::mutex processor_mutex_;
};

}  // namespace capabilities

#include "graph/DefaultCommandProcessor.hpp"
#include "capabilities/CommandRegistryCapability.hpp"

namespace capabilities {

inline CommandProcessorCapability::CommandProcessorCapability(
    std::shared_ptr<graph::ICommandProcessor> processor) : processor_(processor) {
    if (!processor) {
        throw std::invalid_argument(
            "CommandProcessorCapability: processor cannot be null");
    }
}

inline CommandProcessorCapability::CommandProcessorCapability() {
    // Create default processor
    // Note: In a real implementation, this might need to get registry from bus
    // For now, we'll create a null processor that will be initialized later
    // OR we could require registry to be passed
    throw std::invalid_argument(
        "CommandProcessorCapability: default constructor requires registry capability");
}

inline CommandResult CommandProcessorCapability::ProcessCommand(
    const std::string& raw_command) const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    if (!processor_) {
        return CommandResult(false, "Processor not initialized");
    }
    return processor_->ProcessCommand(raw_command);
}

inline CommandResult CommandProcessorCapability::ProcessCommand(
    const std::string& name,
    const std::vector<std::string>& args) const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    if (!processor_) {
        return CommandResult(false, "Processor not initialized");
    }
    return processor_->ProcessCommand(name, args);
}

inline bool CommandProcessorCapability::HasCommand(
    const std::string& name) const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    if (!processor_) {
        return false;
    }
    return processor_->HasCommand(name);
}

inline const CommandInfo* CommandProcessorCapability::GetCommandInfo(
    const std::string& name) const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    if (!processor_) {
        return nullptr;
    }
    return processor_->GetCommandInfo(name);
}

inline std::vector<CommandInfo> CommandProcessorCapability::GetAllCommands() const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    if (!processor_) {
        return std::vector<CommandInfo>();
    }
    return processor_->GetAllCommands();
}

inline std::shared_ptr<graph::ICommandProcessor> CommandProcessorCapability::GetProcessor() const {
    std::lock_guard<std::mutex> lock(processor_mutex_);
    return processor_;
}

inline void CommandProcessorCapability::SetProcessor(
    std::shared_ptr<graph::ICommandProcessor> new_processor) {
    if (!new_processor) {
        throw std::invalid_argument(
            "CommandProcessorCapability::SetProcessor: processor cannot be null");
    }
    std::lock_guard<std::mutex> lock(processor_mutex_);
    processor_ = new_processor;
}

}  // namespace capabilities
