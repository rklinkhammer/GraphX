// MIT License
//
// Copyright (c) 2025 graphlib contributors
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

#include <atomic>
#include <csignal>
#include <functional>
#include <mutex>

namespace app {

/**
 * @class SignalHandler
 * @brief Thread-safe signal handling for graceful shutdown
 *
 * Provides a singleton signal handler that safely handles SIGINT, SIGTERM, and SIGHUP.
 * Uses atomic flag for thread-safe shutdown requests without blocking signal handlers.
 *
 * Usage:
 * ```cpp
 * auto& handler = SignalHandler::GetInstance();
 * handler.Register([]() { std::cout << "Shutting down...\n"; });
 * handler.RegisterSignals();
 * 
 * // In execution loop:
 * if (handler.ShouldShutdown()) {
 *     // Perform graceful shutdown
 * }
 * ```
 *
 * Thread Safety:
 * - All methods are thread-safe
 * - Shutdown request check is lock-free
 * - Safe to call from signal handlers
 *
 * Platform Support:
 * - POSIX systems (Linux, macOS, BSD)
 * - Windows (simulated via condition variables)
 */
class SignalHandler {
public:
    /// Callback type for shutdown notification
    using SignalCallback = std::function<void()>;

    /**
     * Get singleton instance
     * @return Reference to SignalHandler instance
     */
    static SignalHandler& GetInstance();

    /**
     * Register a callback to be called on shutdown request
     * @param callback Function to call when shutdown is requested
     *
     * Note: Callback is called from signal handler context.
     * Keep it simple and signal-safe.
     */
    void Register(SignalCallback callback);

    /**
     * Register a callback to be called on screen resize
     * @param callback Function to call when screen resize is requested
     *
     * Note: Callback is called from signal handler context.
     * Keep it simple and signal-safe.
     */
    void RegisterScreenResize(SignalCallback callback);

    /**
     * Register signal handlers for SIGINT, SIGTERM, SIGHUP
     *
     * Must be called from main thread (signal handling is thread-local in some cases).
     * Idempotent - safe to call multiple times.
     */
    void RegisterSignals();

    /**
     * Check if shutdown has been requested
     * @return true if shutdown signal was received
     *
     * Lock-free, safe to call from any thread frequently.
     */
    bool ShouldShutdown() const;

    /**
     * Request shutdown (typically called from signal handler)
     * @param signal_number Signal that triggered shutdown (for logging)
     */
    void RequestShutdown(int signal_number = 0);

    /**
     * Reset shutdown state (for testing or restart scenarios)
     */
    void Reset();

    /**
     * Get the signal number that triggered shutdown
     * @return Signal number or 0 if not triggered by signal
     */
    int GetTriggerSignal() const;

private:
    SignalHandler();
    ~SignalHandler() = default;

    /// Static signal handler function
    static void HandleSignal(int signal);
    /// Static screen resize handler function
    static void HandleResizeSignal(int signal);

    /// Shutdown flag (atomic for thread-safe reads)
    std::atomic<bool> shutdown_requested_{false};

    /// Signal that triggered shutdown
    std::atomic<int> trigger_signal_{0};

    /// Shutdown callback
    SignalCallback callback_;
    /// Resize callback
    SignalCallback resize_callback_;

    /// Prevent copying
    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    /// Prevent moving
    SignalHandler(SignalHandler&&) = delete;
    SignalHandler& operator=(SignalHandler&&) = delete;
};

}  // namespace app
