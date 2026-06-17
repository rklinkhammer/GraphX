/**
 * @file ICommandOutput.hpp
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

namespace capabilities {

/**
 * @brief Abstract interface for command output
 *
 * Implementations provide different output destinations:
 * - Dashboard (via queue)
 * - Console (stdout/stderr)
 * - File (log file)
 * - Logger (log4cxx)
 * - Queue (for programmatic access)
 *
 * **Phase 3: Output Abstraction**
 * Enables CommandRegistry to output without knowing about Dashboard.
 * Supports multiple concurrent outputs to different destinations.
 *
 * Thread-safety: Implementations must handle concurrent writes.
 * (Each implementation protects itself, caller doesn't need to synchronize.)
 *
 * **Usage Pattern**:
 * ```cpp
 * auto output = std::make_shared<DashboardOutput>(dashboard_capability);
 * output->WriteMessage("Command executed");
 * output->WriteError("Unknown command");
 * output->WriteHelp(commands);
 * ```
 *
 * **Benefits**:
 * - CommandRegistry is UI-agnostic
 * - Multiple outputs simultaneously (console + dashboard + file)
 * - Easy to test (mock output)
 * - Easy to extend (custom outputs)
 *
 * @see CommandOutputCapability (bus wrapper)
 * @see DashboardOutput (Dashboard implementation)
 * @see ConsoleOutput (Console implementation)
 * @see CommandRegistry (consumer)
 */
/**
 * @class ICommandOutput
 * @brief ICommandOutput class.
 */
/**
 * @class ICommandOutput
 * @brief I command output implementation for GraphX.
 */
class ICommandOutput {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~ICommandOutput() = default;

    /**
     * @brief Write general message
     *
     * For informational messages and command results.
     * Example: "Command executed successfully"
     *
     * @param message Message to write
     */
/**
 * @brief Write message.
 * @param message Parameter for write message.
 */
    virtual void WriteMessage(const std::string& message) = 0;

    /**
     * @brief Write error message
     *
     * For errors and failures.
     * Example: "Unknown command: foo"
     *
     * @param error Error message to write
     */
/**
 * @brief Write error.
 * @param error Parameter for write error.
 */
    virtual void WriteError(const std::string& error) = 0;

    /**
     * @brief Write warning message
     *
     * For warnings and deprecations.
     * Example: "Command 'old_name' is deprecated"
     *
     * @param warning Warning message to write
     */
/**
 * @brief Write warning.
 * @param warning Parameter for write warning.
 */
    virtual void WriteWarning(const std::string& warning) = 0;

    /**
     * @brief Write help for available commands
     *
     * Formats and writes help text for all commands.
     * Implementations can customize formatting:
     * - Dashboard: colored output in UI
     * - Console: plain text with alignment
     * - Logger: structured with metadata
     *
     * @param commands Vector of CommandInfo with descriptions
     *
     * @see CommandRegistry::GetAllCommands()
     */
/**
 * @brief Write help.
 * @param commands Parameter for write help.
 */
    virtual void WriteHelp(const std::vector<CommandInfo>& commands) = 0;
};

}  // namespace capabilities
