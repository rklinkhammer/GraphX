/**
 * @file IExecutionCallback.hpp
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

namespace graph {

/**
 * @class IExecutionCallback
 * @brief Abstract callback interface for graph execution events
 *
 * Defines the contract between the graph execution engine (libgraph) and
 * external systems that need to observe or control execution. This interface
 * enables the graph layer to remain completely independent of dashboard/UI
 * implementations.
 *
 * Key Design Principles:
 * 1. **No Dashboard Knowledge**: Contains only execution-related operations
 * 2. **Pure Virtual**: No access to GraphCapability or dashboard structures
 * 3. **Inversion of Control**: Graph notifies callback, not vice versa
 * 4. **Extensible**: New callback types can inherit without graph layer changes
 *
 * Thread Safety:
 * - Callbacks may be invoked from graph execution thread or main thread
 * - Implementations must handle synchronization as needed
 * - No locks held by graph layer during callback invocation
 *
 * @see ExecutionPolicyChain, GraphExecutor
 */
/**
 * @class IExecutionCallback
 * @brief I execution callback implementation for GraphX.
 */
class IExecutionCallback {
public:
    virtual ~IExecutionCallback() = default;

    /**
     * @brief Called when graph execution is about to start
     *
     * Invoked after initialization but before the execution loop begins.
     * Implementations can use this to prepare resources, spawn threads, etc.
     *
     * @return true if execution should proceed, false to abort startup
     */
    virtual bool OnGraphStarted() { return true; }

    /**
     * @brief Called when graph execution is paused
     *
     * Invoked when execution is temporarily suspended but may resume.
     * Implementations can save state or notify observers.
     *
     * @return true if pause was successful, false on error
     */
    virtual bool OnGraphPaused() { return true; }

    /**
     * @brief Called when a node completes execution
     *
     * Invoked after a node's execute() method returns.
     * Useful for metrics collection, progress tracking, or dependency resolution.
     *
     * @param node_id The unique identifier of the completed node
     * @return true to continue execution, false to stop the graph
     */
    virtual bool OnNodeCompleted(const std::string& node_id) { (void)node_id; return true; }

    /**
     * @brief Check if execution should stop
     *
     * Called by the execution engine to check if graceful shutdown was requested.
     * Implementations can check user input, completion conditions, or external signals.
     *
     * @return true if execution should stop, false to continue
     */
    virtual bool ShouldStop() const { return false; }

    /**
     * @brief Signal that execution should stop
     *
     * Called by policies or external systems to request graceful shutdown.
     * Implementations should set internal state that ShouldStop() will return.
     */
    virtual void RequestStop() {}

    /**
     * @brief Called when graph execution stops
     *
     * Invoked when execution ends (by request, error, or completion).
     * Implementations can clean up resources, finalize reporting, etc.
     */
    virtual void OnGraphStopped() {}
};

}  // namespace graph
