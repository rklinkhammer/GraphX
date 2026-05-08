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

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <ranges>
#include "app/capabilities/GraphCapability.hpp"
#include "core/RangesUtilities.hpp"

// Forward declaration
class CommandRegistry;
class Dashboard;

/**
 * @struct CommandResult
 * @brief Result of executing a dashboard command
 *
 * Indicates whether the command succeeded and provides output/error message.
 */
struct CommandResult {
    bool success;       ///< Whether command execution succeeded
    std::string message;///< Output message or error description
    
    /**
     * @brief Construct a command result
     *
     * @param ok Success status
     * @param msg Output or error message (optional)
     */
    CommandResult(bool ok, const std::string& msg="") 
        : success(ok), message(msg) {}
};

/**
 * @typedef CommandHandler
 * @brief Function signature for command implementations
 *
 * Takes a vector of arguments (first element is command name) and returns
 * a CommandResult indicating success and output message.
 *
 * @param args Command and arguments (args[0] is command name)
 * @return CommandResult with execution status and output
 *
 * @example
 *   CommandHandler handler = [&](const std::vector<std::string>& args) {
 *       if (args.size() < 2) return CommandResult(false, "Missing argument");
 *       int value = std::stoi(args[1]);
 *       return CommandResult(true, "Set to " + std::to_string(value));
 *   };
 */
using CommandHandler = std::function<CommandResult(const std::vector<std::string>&)>;

/**
 * @struct CommandInfo
 * @brief Metadata for a registered command
 *
 * Contains command name, description, usage information, and the handler function.
 */
struct CommandInfo {
    std::string name;           ///< Command name (e.g., "help", "run")
    std::string description;    ///< One-line description of what command does
    std::string usage;          ///< Usage format (e.g., "run_graph [timeout_ms]")
    CommandHandler handler;     ///< Function to execute when command is invoked
};

/**
 * @class CommandRegistry
 * @brief Extensible command registry for dashboard user input
 *
 * CommandRegistry manages a set of available commands that users can execute
 * from the dashboard's command window. Commands are registered with metadata
 * (name, description, usage) and a handler function.
 *
 * Built-in commands include:
 * - `help` - Display available commands
 * - `run` - Start graph execution
 * - `stop` - Stop graph execution
 * - `status` - Display execution status
 * - `filter_metrics` - Filter displayed metrics by pattern
 * - And others
 *
 * Custom commands can be registered programmatically. The registry is designed
 * to be extensible for domain-specific needs.
 *
 * @see CommandResult, CommandInfo
 *
 * @example
 *   auto registry = std::make_shared<CommandRegistry>();
 *   
 *   // Register built-in commands
 *   registry->RegisterBuiltinCommands(dashboard);
 *   
 *   // Register custom command
 *   registry->RegisterCommand(
 *       "my_command",
 *       "Does something custom",
 *       "Usage: my_command <arg>",
 *       [](const auto& args) {
 *           return CommandResult(true, "Command executed");
 *       }
 *   );
 *   
 *   // Execute command
 *   auto result = registry->ExecuteCommand("my_command arg1 arg2");
 */
class CommandRegistry {
public:
    /**
     * @brief Construct an empty command registry
     *
     * Use RegisterBuiltinCommands() and RegisterCommand() to add commands.
     */
    CommandRegistry() = default;
    
    /**
     * @brief Destructor
     */
    ~CommandRegistry() = default;

    // Register a command with handler
    // Returns false if command already exists
    bool RegisterCommand(
        const std::string& name,
        const std::string& description,
        const std::string& usage,
        CommandHandler handler);

    // Execute a command by name with arguments
    CommandResult ExecuteCommand(
        const std::string& name,
        const std::vector<std::string>& args);

    /**
     * @brief Get all registered commands (Phase 2: C++26 ranges-ready)
     *
     * Returns all commands in a vector for iteration or further filtering.
     *
     * **Modern C++26 Usage Pattern** (with ranges):
     * @code
     *   using namespace std::ranges;
     *   
     *   // Get all commands
     *   auto all = registry->GetAllCommands();
     *   
     *   // Filter commands by prefix
     *   auto matching = all 
     *       | views::filter([prefix](const auto& cmd) {
     *           return app::ranges::StartsWithI(cmd.name, prefix);
     *       })
     *       | to<std::vector>();
     *   
     *   // Count non-deprecated commands
     *   auto count = ranges::count_if(all, 
     *       [](const auto& cmd) { return !cmd.deprecated; });
     *   
     *   // Get names of all commands
     *   auto names = all 
     *       | views::transform([](const auto& cmd) { return cmd.name; })
     *       | to<std::vector>();
     * @endcode
     *
     * @return Vector of all registered CommandInfo structures
     * @see RangesUtilities.hpp for helper functions like StartsWithI()
     */
    std::vector<CommandInfo> GetAllCommands() const;

    /**
     * @brief Check if command exists (Phase 2: ranges-optimized)
     *
     * Efficiently checks command existence without creating temporary containers.
     *
     * **Modern Implementation**:
     * @code
     *   bool HasCommand(const std::string& name) const {
     *       return std::ranges::find_if(commands_,
     *           [&name](const auto& pair) { 
     *               return pair.first == name; 
     *           }) != commands_.end();
     *   }
     * @endcode
     *
     * @param name Command name to check
     * @return true if command is registered, false otherwise
     */
    bool HasCommand(const std::string& name) const;

    // Get command info
    const CommandInfo* GetCommandInfo(const std::string& name) const;

    /**
     * @brief Generate help text for all commands (Phase 2 enhancement)
     *
     * Displays all registered commands with their descriptions and usage.
     * Uses ranges algorithms internally for efficient filtering.
     *
     * @param dashboard Pointer to dashboard for output display
     */
    void GenerateHelpText(Dashboard* dashboard) const;

private:
    // Store commands in insertion order
    // Phase 2 Note: Updated to work efficiently with ranges views
    std::map<std::string, CommandInfo> commands_;
};

// ============================================================================
// Phase 2: Ranges Patterns for CommandRegistry (C++26 Modernization)
// ============================================================================
//
// The CommandRegistry is designed to work seamlessly with C++20+ std::ranges.
// 
// **Pattern 1: Get commands and filter by prefix (autocompletion)**
// ```cpp
// auto commands = registry->GetAllCommands();
// auto completions = commands
//     | views::filter([prefix](const auto& cmd) {
//         return app::ranges::StartsWithI(cmd.name, prefix);
//     })
//     | ranges::to<std::vector>();
// ```
//
// **Pattern 2: Find command by exact name**
// ```cpp
// auto it = ranges::find_if(registry->GetAllCommands(),
//     [name](const auto& cmd) { return cmd.name == name; });
// if (it != commands.end()) { /* found */ }
// ```
//
// **Pattern 3: Count commands matching criteria**
// ```cpp
// auto urgent = ranges::count_if(registry->GetAllCommands(),
//     [](const auto& cmd) { return cmd.priority > 5; });
// ```
//
// **Pattern 4: Group commands by category**
// ```cpp
// auto by_category = app::ranges::GroupBy(
//     registry->GetAllCommands(),
//     [](const auto& cmd) { return cmd.category; });
// ```
//
// **Benefits of ranges approach**:
// - No intermediate vector allocation for filters
// - Lazy evaluation of predicates
// - Composable and chainable operations
// - Better compiler optimizations
// - Clear algorithmic intent in code
//
