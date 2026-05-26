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
#include <mutex>
#include <stdexcept>

namespace capabilities {

/**
 * @brief Capability wrapper for command output
 *
 * Registers an ICommandOutput implementation in the capability bus,
 * allowing CommandRegistry and other components to write output
 * without knowing the destination.
 *
 * **Key Features**:
 * - Pluggable output implementations (interface-based)
 * - Thread-safe public API (mutex-protected)
 * - Runtime output swapping via SetOutput()
 * - Multiple concurrent outputs via composition
 *
 * **Thread Safety**:
 * All public methods protected with std::lock_guard<std::mutex>.
 * Internal output is stored as shared_ptr (atomic increment/decrement).
 *
 * **Phase 3 Implementation**:
 * Enables command output abstraction separate from Dashboard.
 * Supports:
 * - Dashboard output (via queue)
 * - Console output (stdout/stderr)
 * - File output (log file)
 * - Logger output (log4cxx)
 * - Custom outputs (extensible)
 *
 * **Usage Pattern**:
 * ```cpp
 * // Create with dashboard output
 * auto dashboard_out = std::make_shared<DashboardOutput>(dashboard_cap);
 * auto output_cap = std::make_shared<CommandOutputCapability>(dashboard_out);
 * context.GetCapabilityBus().Register<CommandOutputCapability>(output_cap);
 *
 * // Or create with console output
 * auto console_out = std::make_shared<ConsoleOutput>();
 * auto output_cap = std::make_shared<CommandOutputCapability>(console_out);
 * context.GetCapabilityBus().Register<CommandOutputCapability>(output_cap);
 *
 * // Use from CommandRegistry
 * output_cap->WriteMessage("Command completed");
 * output_cap->WriteHelp(commands);
 * ```
 *
 * @see ICommandOutput (abstract interface)
 * @see DashboardOutput (Dashboard implementation)
 * @see ConsoleOutput (Console implementation)
 * @see CommandRegistry (primary consumer)
 */
class CommandOutputCapability {
public:
    /**
     * @brief Construct with explicit output implementation
     *
     * Creates capability wrapping a custom ICommandOutput implementation.
     * Useful for:
     * - Using specific output type (Dashboard, Console, File)
     * - Testing with mock outputs
     * - Custom output implementations
     *
     * @param output ICommandOutput implementation
     *
     * @throws std::invalid_argument if output is null
     */
    explicit CommandOutputCapability(std::shared_ptr<ICommandOutput> output);

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CommandOutputCapability() = default;

    // =========================================================================
    // Output Operations (delegates to output)
    // =========================================================================

    /**
     * @brief Write general message
     *
     * Thread-safe.
     *
     * @param message Message to write
     */
    void WriteMessage(const std::string& message) const;

    /**
     * @brief Write error message
     *
     * Thread-safe.
     *
     * @param error Error message to write
     */
    void WriteError(const std::string& error) const;

    /**
     * @brief Write warning message
     *
     * Thread-safe.
     *
     * @param warning Warning message to write
     */
    void WriteWarning(const std::string& warning) const;

    /**
     * @brief Write help for available commands
     *
     * Thread-safe.
     *
     * @param commands Vector of CommandInfo to format and write
     */
    void WriteHelp(const std::vector<CommandInfo>& commands) const;

    // =========================================================================
    // Output Access
    // =========================================================================

    /**
     * @brief Get underlying output implementation
     *
     * Returns shared_ptr to ICommandOutput for advanced usage.
     * Useful for:
     * - Querying output type (dynamic_cast for specific features)
     * - Storing output reference
     * - Testing output directly
     *
     * Thread-safe (shared_ptr increment/decrement is atomic).
     *
     * @return Shared pointer to ICommandOutput
     */
    std::shared_ptr<ICommandOutput> GetOutput() const;

    /**
     * @brief Replace output at runtime
     *
     * Swaps to a new output implementation.
     * Useful for:
     * - Dynamic output switching
     * - Enabling/disabling outputs
     * - Testing output swapping
     *
     * Thread-safe.
     *
     * @param new_output New ICommandOutput implementation
     *
     * @throws std::invalid_argument if new_output is null
     */
    void SetOutput(std::shared_ptr<ICommandOutput> new_output);

    // =========================================================================
    // Metadata
    // =========================================================================

    /**
     * @brief Check if capability is ready for use
     *
     * @return true if output is initialized and ready
     */
    bool IsReady() const { return output_ != nullptr; }

    /**
     * @brief Get capability name for debugging
     *
     * @return String identifier: "CommandOutputCapability"
     */
    std::string GetCapabilityName() const {
        return "CommandOutputCapability";
    }

private:
    std::shared_ptr<ICommandOutput> output_;
    mutable std::mutex output_mutex_;
};

// =========================================================================
// Inline Implementations
// =========================================================================

inline CommandOutputCapability::CommandOutputCapability(
    std::shared_ptr<ICommandOutput> output) : output_(output) {
    if (!output) {
        throw std::invalid_argument(
            "CommandOutputCapability: output cannot be null");
    }
}

inline void CommandOutputCapability::WriteMessage(
    const std::string& message) const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (output_) {
        output_->WriteMessage(message);
    }
}

inline void CommandOutputCapability::WriteError(
    const std::string& error) const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (output_) {
        output_->WriteError(error);
    }
}

inline void CommandOutputCapability::WriteWarning(
    const std::string& warning) const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (output_) {
        output_->WriteWarning(warning);
    }
}

inline void CommandOutputCapability::WriteHelp(
    const std::vector<CommandInfo>& commands) const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    if (output_) {
        output_->WriteHelp(commands);
    }
}

inline std::shared_ptr<ICommandOutput> CommandOutputCapability::GetOutput() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return output_;
}

inline void CommandOutputCapability::SetOutput(
    std::shared_ptr<ICommandOutput> new_output) {
    if (!new_output) {
        throw std::invalid_argument(
            "CommandOutputCapability::SetOutput: output cannot be null");
    }
    std::lock_guard<std::mutex> lock(output_mutex_);
    output_ = new_output;
}

}  // namespace capabilities
