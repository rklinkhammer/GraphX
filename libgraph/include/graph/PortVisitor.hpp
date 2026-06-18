/**
 * @file PortVisitor.hpp
 * @brief Port Visitor Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

#include <cstddef>

namespace graph
{
    /**
     * @brief Generic port iterator that eliminates recursive template boilerplate
     * 
     * This template provides a reusable mechanism for iterating over numbered ports
     * and dispatching operations to the correct port index at compile-time.
     * 
     * Problem Solved:
     * - Eliminates repetitive recursive template methods in node base classes
     * - Centralizes the port dispatch pattern in one place
     * - Makes it easy to add new port operations without code duplication
     * 
     * Usage:
     * ```cpp
     * // Define an operation
     * struct GetQueueSizeOp {
     *     std::size_t result = 0;
     *     template<std::size_t Port>
     *     void Execute(SourceNodeBase* node) {
     *         using PortType = ... // extract port type
     *         auto* fn = static_cast<OutputFn<PortType>*>(node);
     *         result = fn->GetQueue().Size();
     *     }
     * };
     * 
     * // Use the iterator
     * GetQueueSizeOp op;
     * PortIterator<SourceNodeBase, 3>::ForEachPort(this, port_id, op);
     * return op.result;
     * ```
     * 
     * @tparam Derived The node class being iterated (for type safety)
     * @tparam MaxPorts Maximum number of ports (compile-time constant)
     */
    template <typename Derived, std::size_t MaxPorts>
/**
 * @class PortIterator
 * @brief Port Iterator type.
 *
 * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
 */
    /**
     * @class PortIterator
     * @brief Port Iterator type.
     *
     * @details Part of the GraphX public API for libgraph. The type documents its runtime role, ownership expectations, and interaction with neighboring graph components.
     */
    class PortIterator
    {
    private:
        /**
         * @brief Recursive helper that dispatches to correct port by ID
         * 
         * Performs a recursive compile-time search for the matching port index.
         * When port_id matches the current Port, executes the operation.
         * Otherwise recurses to the next port index.
         * 
         * @tparam Port Current port index being tested (starts at 0)
         * @tparam Operation Type implementing `void Execute<Port>(Derived*)`
         */
        template <std::size_t Port, typename Operation>
        static void ForEachPortImpl(Derived* node, std::size_t port_id, Operation& op)
        {
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param MaxPorts Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            if constexpr (Port < MaxPorts) {
                if (port_id == Port) {
                    // Found the matching port, execute operation
                    op.template Execute<Port>(node);
                } else {
                    // Recurse to next port
                    ForEachPortImpl<Port + 1>(node, port_id, op);
                }
            }
            // Base case: Port >= MaxPorts, iteration complete (do nothing)
        }

        /**
         * @brief Recursive helper that executes operation on all ports
         * 
         * Executes the operation for every port from 0 to MaxPorts-1.
         * Used for operations like EnableMetrics or ResetMetrics that apply to all ports.
         * 
         * @tparam Port Current port index
         * @tparam Operation Type implementing `void Execute<Port>(Derived*)`
         */
        template <std::size_t Port, typename Operation>
        static void ForAllPortsImpl(Derived* node, Operation& op)
        {
            /**
             * @brief Executes the Constexpr operation.
             *
             * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
             * @param MaxPorts Input or configuration value consumed by the method.
             * @return Method-specific result, status, or produced value when the signature provides one.
             */
            if constexpr (Port < MaxPorts) {
                // Execute operation for this port
                op.template Execute<Port>(node);
                // Recurse to next port
                ForAllPortsImpl<Port + 1>(node, op);
            }
            // Base case: Port >= MaxPorts, all ports visited (do nothing)
        }

    public:
        /**
         * @brief Execute operation on a specific port by ID
         * 
         * Dispatches to the correct port index at compile-time.
         * Port ID is matched at runtime, but recursion is compile-time.
         * 
         * @param node Pointer to the node instance
         * @param port_id Index of the port to operate on (0-based)
         * @param op Operation to execute (must have Execute<Port> method)
         */
        template <typename Operation>
        static void ForEachPort(Derived* node, std::size_t port_id, Operation& op)
        {
            ForEachPortImpl<0>(node, port_id, op);
        }

        /**
         * @brief Execute operation on all ports
         * 
         * Applies the operation to every port from 0 to MaxPorts-1.
         * Useful for bulk operations like reset, enable/disable metrics.
         * 
         * @param node Pointer to the node instance
         * @param op Operation to execute (must have Execute<Port> method)
         */
        template <typename Operation>
        static void ForAllPorts(Derived* node, Operation& op)
        {
            ForAllPortsImpl<0>(node, op);
        }
    };

} // namespace graph
