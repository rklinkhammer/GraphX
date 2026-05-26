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
#include "capabilities/CommandRegistryCapability.hpp"
#include <memory>
#include <string>
#include <vector>

namespace graph {

/**
 * @brief Default command processor implementation
 *
 * Wraps CommandRegistryCapability to provide ICommandProcessor interface.
 * Handles:
 * - Parsing raw command strings into name and arguments
 * - Validating command existence before execution
 * - Invoking registered command handlers
 * - Formatting and returning results
 *
 * Thread-safe when accessed via CommandProcessorCapability.
 * Does NOT add synchronization on top of CommandRegistryCapability
 * (relies on capability's internal mutex).
 *
 * **Phase 2 Reference Implementation**:
 * Demonstrates how to wrap CommandRegistry(Capability) with ICommandProcessor.
 * Other processors might add:
 * - Logging/auditing
 * - Caching
 * - Async execution
 * - Rate limiting
 * - Custom parsing (quotes, escapes)
 *
 * @see ICommandProcessor (abstract interface)
 * @see CommandRegistryCapability (wrapped capability)
 * @see CommandProcessorCapability (bus wrapper)
 */
class DefaultCommandProcessor : public ICommandProcessor {
public:
    /**
     * @brief Construct with registry capability
     *
     * Creates a processor that delegates to the provided registry.
     * The registry should already have commands registered via OnInit().
     *
     * @param registry CommandRegistryCapability with registered commands
     *
     * @throws std::invalid_argument if registry is null or not ready
     */
    explicit DefaultCommandProcessor(
        std::shared_ptr<capabilities::CommandRegistryCapability> registry);

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~DefaultCommandProcessor() = default;

    // =========================================================================
    // ICommandProcessor Implementation
    // =========================================================================

    /**
     * @brief Process raw command string
     *
     * **Processing Steps**:
     * 1. Parse raw_command into name and args (whitespace-separated)
     * 2. Validate: Check if command exists
     * 3. Execute: Call ProcessCommand(name, args)
     * 4. Return: CommandResult with success/message
     *
     * **Error Handling**:
     * - Empty command → error message
     * - Unknown command → error with command name
     * - Handler error → propagated as CommandResult.message
     *
     * @param raw_command Full command string (e.g., "help status")
     * @return CommandResult with success flag and message
     *
     * @see ProcessCommand(name, args) for pre-parsed variant
     */
    CommandResult ProcessCommand(const std::string& raw_command) override;

    /**
     * @brief Process pre-parsed command
     *
     * Directly executes command by name and arguments.
     * Useful for programmatic invocation without string parsing.
     *
     * **Processing Steps**:
     * 1. Validate: Check if command exists
     * 2. Execute: Invoke command handler from registry
     * 3. Return: CommandResult from handler
     *
     * @param name Command name
     * @param args Command arguments (not including command name)
     * @return CommandResult from handler or error if command not found
     *
     * @see ProcessCommand(raw_command) for string variant
     */
    CommandResult ProcessCommand(
        const std::string& name,
        const std::vector<std::string>& args) override;

    /**
     * @brief Check if command exists
     *
     * Delegates to CommandRegistryCapability::HasCommand().
     *
     * @param name Command name
     * @return true if command is registered
     */
    bool HasCommand(const std::string& name) const override;

    /**
     * @brief Get command metadata
     *
     * Delegates to CommandRegistryCapability::GetCommandInfo().
     *
     * @param name Command name
     * @return Pointer to CommandInfo or nullptr if not found
     */
    const CommandInfo* GetCommandInfo(const std::string& name) const override;

    /**
     * @brief Get all registered commands
     *
     * Delegates to CommandRegistryCapability::GetAllCommands().
     *
     * @return Vector of all CommandInfo structures
     */
    std::vector<CommandInfo> GetAllCommands() const override;

private:
    std::shared_ptr<capabilities::CommandRegistryCapability> registry_;

    /**
     * @brief Parse raw command string into name and arguments
     *
     * Simple whitespace-based tokenization.
     *
     * **Algorithm**:
     * 1. Skip leading whitespace
     * 2. Extract first token (command name)
     * 3. Remaining tokens are arguments
     * 4. Support empty args (command-only like "status")
     *
     * **Future Enhancements**:
     * - Quoted argument support ("help \"command name\"")
     * - Escape sequence handling
     * - Pipe/redirection parsing
     *
     * @param raw_command Input string (e.g., "help status")
     * @param out_name Command name (first token) [output]
     * @param out_args Arguments (remaining tokens) [output]
     * @return true if parsing succeeded (name extracted)
     *         false if raw_command empty or whitespace-only
     *
     * @post out_name and out_args cleared before parsing
     */
    bool ParseCommand(
        const std::string& raw_command,
        std::string& out_name,
        std::vector<std::string>& out_args) const;
};

}  // namespace graph
