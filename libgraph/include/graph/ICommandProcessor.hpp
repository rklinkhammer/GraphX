/**
 * @file ICommandProcessor.hpp
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

#include "ui/CommandRegistry.hpp"
#include <string>
#include <vector>

namespace graph {

/**
 * @brief Abstract interface for command processing
 *
 * Implementations can provide custom parsing, validation, or async execution.
 * Used by CommandPolicy to decouple thread logic from dispatch.
 *
 * **Phase 2: CommandProcessorCapability Abstraction**
 * Enables pluggable command processors with:
 * - Custom parsing strategies
 * - Async/batched execution
 * - Logging and validation
 * - Result callbacks
 *
 * @see DefaultCommandProcessor (Phase 2 reference implementation)
 * @see CommandProcessorCapability (bus wrapper)
 * @see CommandPolicy (consumer)
 */
/**
 * @class ICommandProcessor
 * @brief ICommandProcessor class.
 */
class ICommandProcessor {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~ICommandProcessor() = default;

    /**
     * @brief Process raw command string
     *
     * Parses "command_name arg1 arg2 ..." format and executes.
     * Responsibility: Handle full command lifecycle
     *   1. Parse raw string into name and arguments
     *   2. Validate command exists
     *   3. Invoke command handler
     *   4. Handle and format result
     *
     * @param raw_command Full command string (e.g., "help status", "pause")
     * @return CommandResult with success/failure and message
     *
     * @throws std::exception subclasses for processing errors (optional)
     */
/**
 * @brief Process command.
 * @param raw_command Parameter for process command.
 * @return Result of the operation.
 */
    virtual CommandResult ProcessCommand(const std::string& raw_command) = 0;

    /**
     * @brief Process pre-parsed command
     *
     * For callers that already have command name and arguments.
     * Useful for:
     * - Programmatic command invocation (not from user input)
     * - Complex parsing (quotes, escapes) done at call site
     * - Command forwarding between components
     *
     * @param name Command name (single token, required)
     * @param args Command arguments (not including command name)
     * @return CommandResult with success/failure and message
     *
     * @throws std::exception subclasses for execution errors (optional)
     */
    virtual CommandResult ProcessCommand(
        const std::string& name,
        const std::vector<std::string>& args) = 0;

    /**
     * @brief Check if command exists
     *
     * Used for validation before execution or dynamic help.
     * Should be fast (O(1) or O(log n)).
     *
     * @param name Command name
     * @return true if command is registered, false otherwise
     */
/**
 * @brief Has command.
 * @param name Parameter for has command.
 * @return Result of the operation.
 */
    virtual bool HasCommand(const std::string& name) const = 0;

    /**
     * @brief Get command metadata
     *
     * Returns detailed information about a command:
     * - Description
     * - Usage/syntax
     * - Handler function
     *
     * @param name Command name
     * @return Pointer to CommandInfo or nullptr if not found
     *
     * **Ownership**: Pointer is valid only during GetAllCommands() iteration
     * or while command is registered. Caller should not hold reference.
     */
/**
 * @brief Get command info.
 * @param name Parameter for get command info.
 * @return Result of the operation.
 */
    virtual const CommandInfo* GetCommandInfo(const std::string& name) const = 0;

    /**
     * @brief Get all registered commands
     *
     * Returns vector of all available commands.
     * Used for:
     * - Generating help output
     * - Discovering available functionality
     * - Validation and filtering
     *
     * @return Vector of CommandInfo structures (copies, safe to hold)
     *
     * @see CommandRegistry::GetAllCommands() for implementation detail
     */
/**
 * @brief Get all commands.
 * @return Result of the operation.
 */
    virtual std::vector<CommandInfo> GetAllCommands() const = 0;
};

}  // namespace graph
