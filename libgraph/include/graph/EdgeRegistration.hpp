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

/**
 * @file EdgeRegistration.hpp
 * @brief Edge creator registration for known node type combinations
 *
 * Phase 2: Node & Edge Instantiation
 *
 * Provides templates and macros for registering all valid (SrcNode, SrcPort, DstNode, DstPort)
 * edge combinations at program startup. These registrations enable runtime edge instantiation
 * from JSON configurations via type-name lookup.
 *
 * @author Copilot
 * @date 2026-01-04
 */

#pragma once

#include "EdgeRegistry.hpp"
#include "graph/GraphManager.hpp"
#include <memory>
#include <typeinfo>

namespace graph::config {

/**
 * @class EdgeRegistration
 * @brief Register edge creators for runtime graph loading
 *
 * Template-based registration system that captures compile-time type information
 * and registers it with the runtime EdgeRegistry for JSON-based graph construction.
 *
 * Usage:
 * @code
 * // At program startup (before JSON loading):
 * EdgeRegistration::Register<
 *     nodes::DataInjectionAccelerometerNode, 0,    // Source node type and port
 *     nodes::FlightFSMNode, 0>()         // Dest node type and port
 * @endcode
 */
class EdgeRegistration {
public:
    /**
     * Register an edge creator for a specific (SrcNode, SrcPort, DstNode, DstPort) combination
     *
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source port index (0, 1, ...)
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination port index (0, 1, ...)
     *
     * Registers a creator function that can instantiate edges of this type at runtime.
     * The creator is stored with a key derived from type names and port indices.
     * If already registered, throws std::runtime_error.
     *
     * Thread safety: NOT thread-safe. Must be called during program initialization
     * before any JSON graph loading begins.
     */
    template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
    static void Register() {
        // Get human-readable type names
        std::string src_type_name = typeid(SrcNode).name();
        std::string dst_type_name = typeid(DstNode).name();
        
        // Create lambda that captures template specialization
        auto creator = [](GraphManager& graph, std::size_t src_node_idx, std::size_t dst_node_idx, std::size_t buffer_size) -> bool {
            try {
                // Get nodes from graph by index
                const auto& nodes = graph.GetNodes();
                if (src_node_idx >= nodes.size() || dst_node_idx >= nodes.size()) {
                    return false;
                }

                // Call the template method to add the edge
                auto src_node = std::dynamic_pointer_cast<SrcNode>(nodes[src_node_idx]);
                auto dst_node = std::dynamic_pointer_cast<DstNode>(nodes[dst_node_idx]);

                if (!src_node || !dst_node) {
                    return false;
                }

                graph.AddEdge<SrcNode, SrcPort, DstNode, DstPort>(src_node, dst_node, buffer_size);
                return true;
            } catch (const std::exception& e) {
                // Log error if needed, but don't throw (let caller handle)
                return false;
            }
        };
        
        // Register with EdgeRegistry
        graph::config::EdgeRegistry::Register<SrcNode, SrcPort, DstNode, DstPort>(
            src_type_name,
            dst_type_name,
            creator
        );
    }
};

}  // namespace graph::config

