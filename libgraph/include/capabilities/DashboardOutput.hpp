/**
 * @file DashboardOutput.hpp
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
#include <memory>

// Forward declaration
namespace capabilities {
class DashboardCapability;
}

namespace capabilities {

/**
 * @brief Output implementation for Dashboard
 *
 * Writes output to DashboardCapability queue.
 * This enables CommandRegistry to output via Dashboard without
 * direct Dashboard* dependency.
 *
 * **Thread Safety**: DashboardCapability handles queue synchronization.
 * DashboardOutput is thread-safe for concurrent writes.
 *
 * **Phase 3**: Dashboard output implementation of ICommandOutput interface.
 * Enables CommandRegistry to write to Dashboard via abstraction.
 *
 * @see ICommandOutput (interface)
 * @see CommandOutputCapability (bus wrapper)
 * @see DashboardCapability (wrapped capability)
 */
/**
 * @class DashboardOutput
 * @brief DashboardOutput class.
 */
/**
 * @class DashboardOutput
 * @brief Dashboard output implementation for GraphX.
 */
class DashboardOutput : public ICommandOutput {
public:
    /**
     * @brief Construct with dashboard capability
     *
     * Creates an output that writes to DashboardCapability's log queue.
     *
     * @param dashboard DashboardCapability to write logs to
     *
     * @throws std::invalid_argument if dashboard is null or not ready
     */
/**
 * @brief Dashboard output.
 * @param dashboard Parameter for dashboard output.
 * @return Result of the operation.
 */
    explicit DashboardOutput(std::shared_ptr<DashboardCapability> dashboard);

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~DashboardOutput() = default;

    // =========================================================================
    // ICommandOutput Implementation
    // =========================================================================

    /**
     * @brief Write general message to dashboard
     *
     * Formats as "[OK] message" and writes to log queue.
     *
     * @param message Message to write
     */
/**
 * @brief Write message.
 * @param message Parameter for write message.
 */
    void WriteMessage(const std::string& message) override;

    /**
     * @brief Write error message to dashboard
     *
     * Formats as "[ERROR] error" and writes to log queue.
     *
     * @param error Error message to write
     */
/**
 * @brief Write error.
 * @param error Parameter for write error.
 */
    void WriteError(const std::string& error) override;

    /**
     * @brief Write warning message to dashboard
     *
     * Formats as "[WARN] warning" and writes to log queue.
     *
     * @param warning Warning message to write
     */
/**
 * @brief Write warning.
 * @param warning Parameter for write warning.
 */
    void WriteWarning(const std::string& warning) override;

    /**
     * @brief Write help to dashboard
     *
     * Formats all commands as "[HELP] command - description"
     * and writes each to log queue.
     *
     * @param commands Vector of CommandInfo to write
     */
/**
 * @brief Write help.
 * @param commands Parameter for write help.
 */
    void WriteHelp(const std::vector<CommandInfo>& commands) override;

private:
    std::shared_ptr<DashboardCapability> dashboard_;
};

}  // namespace capabilities
