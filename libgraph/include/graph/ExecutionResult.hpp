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
#include <cstdint>
#include "graph/ExecutionState.hpp"
namespace graph {

/**
 * @brief Result of graph initialization
 *
 * Contains success/failure information and per-node initialization details.
 */
struct InitializationResult {
    /// True if all nodes initialized successfully
    bool success = false;

    /// Human-readable status message
    std::string message;

    /// Number of nodes initialized
    uint32_t nodes_initialized = 0;

    /// Number of nodes that failed initialization
    uint32_t nodes_failed = 0;

    /// Per-node initialization times (milliseconds)
    std::map<std::string, uint32_t> node_init_times_ms;

    /// Total initialization time (milliseconds)
    uint32_t elapsed_time_ms = 0;

    /// Error details if initialization failed
    std::string error_details;
};

/**
 * @brief Result of execution operations (Start, Stop, etc.)
 *
 * Indicates whether the operation succeeded and provides status information.
 */
struct ExecutionResult {
    /// True if operation succeeded
    bool success = false;

    /// Human-readable status message
    std::string message;

    /// Current execution state after operation
    graph::ExecutionState current_state = graph::ExecutionState::ERROR;

    /// Time taken for operation (milliseconds)
    uint32_t elapsed_time_ms = 0;

    /// Error details if operation failed
    std::string error_details;
};

} // namespace graph
