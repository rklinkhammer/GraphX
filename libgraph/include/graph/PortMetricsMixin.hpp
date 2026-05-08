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

#include <cstddef>

namespace graph {

// Forward declarations
class ThreadMetrics;
namespace core {
    struct QueueMetrics;
}  // namespace core

/**
 * @class OutputPortMetricsMixin
 * @brief Provides metrics access methods for nodes with output ports
 *
 * This mixin consolidates metrics methods (GetOutputQueueMetrics, GetOutputPortThreadMetrics, etc.)
 * across different node types using a unified template pattern.
 *
 * Template Parameters:
 * - Derived: The node class inheriting from this mixin
 * - NOutputPorts: Number of output ports (from derived class)
 *
 * Usage:
 *   class SourceNodeBase : public OutputPortMetricsMixin<SourceNodeBase, sizeof...(Outputs)> { ... }
 *
 * Derived class must provide:
 * - GetOutputQueueMetricsImpl<Port>() recursive template helper
 * - GetOutputPortThreadMetricsImpl<Port>() recursive template helper
 * - GetPortQueueSizeImpl<Port>() recursive template helper
 * - ResetMetricsImpl<Port>() recursive template helper
 * - GetOutputPortCount() returning NOutputs
 *
 * This mixin provides the PUBLIC interface, derived class provides PRIVATE helpers.
 */

template <typename Derived>
class OutputPortMetricsMixin {
public:
    // ========================================================================
    // Public Metrics Access Methods for Output Ports
    // ========================================================================

    /// Get queue metrics for a specific output port
    const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const {
        const core::QueueMetrics* result = nullptr;
        static_cast<const Derived*>(this)->GetOutputQueueMetricsImpl<0>(port_id, result);
        return result;
    }

    /// Get thread metrics for a specific output port
    const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const {
        const ThreadMetrics* result = nullptr;
        static_cast<const Derived*>(this)->GetOutputPortThreadMetricsImpl<0>(port_id, result);
        return result;
    }

    /// Get queue size for a specific output port
    std::size_t GetOutputPortQueueSize(std::size_t port_id) const {
        std::size_t size = 0;
        static_cast<const Derived*>(this)->GetOutputPortQueueSizeImpl<0>(port_id, size);
        return size;
    }

    /// Get total number of output ports
    int GetOutputPortCount() const {
        return static_cast<const Derived*>(this)->GetOutputPortCount();
    }
};

/**
 * @class InputPortMetricsMixin
 * @brief Provides metrics access methods for nodes with input ports
 *
 * This mixin consolidates metrics methods (GetInputQueueMetrics, GetInputPortThreadMetrics, etc.)
 * across different node types using a unified template pattern.
 *
 * Usage:
 *   class SinkNodeBase : public InputPortMetricsMixin<SinkNodeBase, sizeof...(Inputs)> { ... }
 *
 * Derived class must provide:
 * - GetInputQueueMetricsImpl<Port>() recursive template helper
 * - GetInputPortThreadMetricsImpl<Port>() recursive template helper
 * - GetInputPortQueueSizeImpl<Port>() recursive template helper
 * - ResetMetricsImpl<Port>() recursive template helper
 * - GetInputPortCount() returning NInputs
 */

template <typename Derived>
class InputPortMetricsMixin {
public:
    // ========================================================================
    // Public Metrics Access Methods for Input Ports
    // ========================================================================

    /// Get queue metrics for a specific input port
    const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const {
        const core::QueueMetrics* result = nullptr;
        static_cast<const Derived*>(this)->GetInputQueueMetricsImpl<0>(port_id, result);
        return result;
    }

    /// Get thread metrics for a specific input port
    const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const {
        const ThreadMetrics* result = nullptr;
        static_cast<const Derived*>(this)->GetInputPortThreadMetricsImpl<0>(port_id, result);
        return result;
    }

    /// Get queue size for a specific input port
    std::size_t GetInputPortQueueSize(std::size_t port_id) const {
        std::size_t size = 0;
        static_cast<const Derived*>(this)->GetInputPortQueueSizeImpl<0>(port_id, size);
        return size;
    }

    /// Get total number of input ports
    int GetInputPortCount() const {
        return static_cast<const Derived*>(this)->GetInputPortCount();
    }
};

/**
 * @class BidirectionalPortMetricsMixin
 * @brief Provides metrics access methods for nodes with both input and output ports
 *
 * Combines OutputPortMetricsMixin and InputPortMetricsMixin for nodes like InteriorNodeBase
 * and MergeNodeBase that have both input and output ports.
 *
 * Usage:
 *   class InteriorNodeBase : public BidirectionalPortMetricsMixin<InteriorNodeBase> { ... }
 */

template <typename Derived>
class BidirectionalPortMetricsMixin 
    : public OutputPortMetricsMixin<Derived>,
      public InputPortMetricsMixin<Derived> {
public:
    // Inherit all methods from both base classes
    using OutputPortMetricsMixin<Derived>::GetOutputQueueMetrics;
    using OutputPortMetricsMixin<Derived>::GetOutputPortThreadMetrics;
    using OutputPortMetricsMixin<Derived>::GetOutputPortQueueSize;
    using OutputPortMetricsMixin<Derived>::GetOutputPortCount;
    
    using InputPortMetricsMixin<Derived>::GetInputQueueMetrics;
    using InputPortMetricsMixin<Derived>::GetInputPortThreadMetrics;
    using InputPortMetricsMixin<Derived>::GetInputPortQueueSize;
    using InputPortMetricsMixin<Derived>::GetInputPortCount;
};

}  // namespace graph

