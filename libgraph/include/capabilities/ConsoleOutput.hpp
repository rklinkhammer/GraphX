/**
 * @file ConsoleOutput.hpp
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

#include "capabilities/ICommandOutput.hpp"
#include <mutex>
#include <iostream>

namespace capabilities {

/**
 * @brief Output implementation for console (stdout/stderr)
 *
 * Useful for CLI mode without Dashboard.
 * Writes directly to standard output streams with proper formatting.
 *
 * **Thread Safety**: Uses internal mutex for synchronized console access.
 * Safe for concurrent writes from multiple threads.
 *
 * **Phase 3 Bonus**: Console output implementation for testing and CLI mode.
 * Enables CommandRegistry to work in headless/CLI environments.
 *
 * **Output Destinations**:
 * - Messages: stdout with "[OK]" prefix
 * - Errors: stderr with "[ERROR]" prefix
 * - Warnings: stderr with "[WARN]" prefix
 * - Help: stdout with formatted command list
 *
 * @see ICommandOutput (interface)
 * @see CommandOutputCapability (bus wrapper)
 * @see DashboardOutput (Dashboard implementation)
 */
/**
 * @class ConsoleOutput
 * @brief ConsoleOutput class.
 */
/**
 * @class ConsoleOutput
 * @brief Console output implementation for GraphX.
 */
class ConsoleOutput : public ICommandOutput {
public:
    /**
     * @brief Construct console output
     *
     * Creates output that writes to standard streams.
     */
    ConsoleOutput() = default;

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~ConsoleOutput() = default;

    // =========================================================================
    // ICommandOutput Implementation
    // =========================================================================

    /**
     * @brief Write general message to stdout
     *
     * Formats as "[OK] message" to stdout.
     *
     * @param message Message to write
     */
/**
 * @brief Write message.
 * @param message Parameter for write message.
 */
    void WriteMessage(const std::string& message) override;

    /**
     * @brief Write error message to stderr
     *
     * Formats as "[ERROR] error" to stderr.
     *
     * @param error Error message to write
     */
/**
 * @brief Write error.
 * @param error Parameter for write error.
 */
    void WriteError(const std::string& error) override;

    /**
     * @brief Write warning message to stderr
     *
     * Formats as "[WARN] warning" to stderr.
     *
     * @param warning Warning message to write
     */
/**
 * @brief Write warning.
 * @param warning Parameter for write warning.
 */
    void WriteWarning(const std::string& warning) override;

    /**
     * @brief Write help to stdout
     *
     * Formats all commands as readable list with descriptions and usage.
     *
     * @param commands Vector of CommandInfo to write
     */
/**
 * @brief Write help.
 * @param commands Parameter for write help.
 */
    void WriteHelp(const std::vector<CommandInfo>& commands) override;

private:
    mutable std::mutex console_mutex_;
};

// =========================================================================
// Inline Implementations
// =========================================================================

inline void ConsoleOutput::WriteMessage(const std::string& message) {
/**
 * @brief Lock.
 * @param console_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cout << "[OK] " << message << std::endl;
}

inline void ConsoleOutput::WriteError(const std::string& error) {
/**
 * @brief Lock.
 * @param console_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cerr << "[ERROR] " << error << std::endl;
}

inline void ConsoleOutput::WriteWarning(const std::string& warning) {
/**
 * @brief Lock.
 * @param console_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cerr << "[WARN] " << warning << std::endl;
}

inline void ConsoleOutput::WriteHelp(const std::vector<CommandInfo>& commands) {
/**
 * @brief Lock.
 * @param console_mutex_ Parameter for lock.
 * @return Result of the operation.
 */
    std::lock_guard<std::mutex> lock(console_mutex_);
    
    std::cout << "[HELP] Available commands:" << std::endl;
    
    for (const auto& cmd : commands) {
        std::cout << "[HELP]   " << cmd.name << " - " << cmd.description
                  << std::endl;
        
        if (!cmd.usage.empty()) {
            std::cout << "[HELP]     Usage: " << cmd.usage << std::endl;
        }
    }
}

}  // namespace capabilities
