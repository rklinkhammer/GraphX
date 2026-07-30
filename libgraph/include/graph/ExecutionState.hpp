/**
 * @file ExecutionState.hpp
 * @brief Execution State Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include <memory>


namespace graph {

    /**
 * @brief Enumeration of execution states for the interactive CLI
 *
 * Represents the public GraphExecutor lifecycle:
 * - CONFIGURED: Immutable configuration recorded; no graph constructed
 * - INITIALIZED: ExecutionController ready, graph prepared
 * - RUNNING: Active execution (graph nodes processing)
 * - STOPPING: Graceful shutdown in progress
 * - STOPPED: Execution stopped and joined; reconfiguration is allowed
 * - ERROR: Fatal error occurred
 *
 * PAUSED and STEPPING remain compatibility enum values but are not Phase 0
 * command transitions.
 *
 * State Transitions:
 *   CONFIGURED → INITIALIZED (Init after ConfigureGraph)
 *   INITIALIZED → RUNNING (Start with continuous execution)
 *   INITIALIZED/RUNNING → STOPPING (Stop or worker-owned teardown)
 *   STOPPING → STOPPED (Graph shutdown complete)
 *   STOPPED → CONFIGURED (ConfigureGraph after joined teardown)
 *   Any → ERROR (Fatal error detected)
 *
 * Thread-Safety: ExecutionState is queried via thread-safe getter but not
 * directly mutable - transitions are managed by ExecutionController methods.
 */
/**
 * @enum ExecutionState
 * @brief Execution State values.
 *
 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.
 */
enum class ExecutionState : uint8_t {
  CONFIGURED = 0,   ///< Immutable configuration recorded; no graph constructed
  INITIALIZED = 1,  ///< Ready to start, graph initialized
  PAUSED = 2,       ///< Running but temporarily paused
  RUNNING = 3,      ///< Active execution (continuous mode)
  STEPPING = 4,     ///< Stepping mode (manual row injection)
  STOPPING = 5,     ///< Graceful shutdown in progress
  STOPPED = 6,      ///< Execution stopped, nodes joined
  ERROR = 7,        ///< Fatal error occurred
  ANY = 99        ///< Wildcard for any state
};

/**
 * @brief Convert ExecutionState enum to human-readable string
 * @param state The execution state
 * @return String representation (e.g., "RUNNING", "PAUSED")
 *
 * Thread-Safety: Safe to call from any thread (stateless function)
 */
/**
 * @brief Get execution state name.
 * @param state Parameter for get execution state name.
 * @return Result of the operation.
 */
std::string GetExecutionStateName(ExecutionState state);

}  // namespace graph
