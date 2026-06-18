/**
 * @file FluentGraphBuilder.hpp
 * @brief Fluent Graph Builder Graph runtime support.
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

#include "graph/GraphManager.hpp"
#include "graph/EdgeRegistry.hpp"
#include "graph/INode.hpp"
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace graph {

/**
 * @class FluentGraphBuilder
 * @brief Type-safe fluent API for constructing dataflow graphs
 *
 * Provides a method-chaining interface for building graphs programmatically
 * with full compile-time type safety. This is an alternative to JSON-based
 * configuration that offers better IDE support and type checking.
 *
 * Usage Example:
 * @code
 * using namespace graph;
 *
 * auto graph = FluentGraphBuilder<graph::RegisteredNodeProvider>()
 *     .AddNode<DataInjectionAccelerometerNode>("accel")
 *     .AddNode<FlightFSMNode>("fsm")
 *     .AddNode<AltitudeFusionNode>("altitude")
 *     .Connect<DataInjectionAccelerometerNode, 0, FlightFSMNode, 0>("accel", "fsm")
 *     .Connect<FlightFSMNode, 0, AltitudeFusionNode, 0>("fsm", "altitude")
 *     .Build();
 * @endcode
 *
 * Template Parameters:
 * - RegisteredNodeProvider: Provider class for creating node instances
 *
 * The builder accumulates node specifications and edge connections,
 * then creates the final GraphManager with all connections wired.
 */
/**
 * @class FluentGraphBuilder
 * @brief Fluent Graph Builder builder.
 *
 * @details Collects configuration and constructs GraphX runtime objects in a predictable order. Builder methods are intended to be chained before final construction.
 */
template <typename NodeProviderType = void>
class FluentGraphBuilder {
public:
    /**
     * @struct NodeSpec
     * @brief Specification for a node in the graph
     */
    /**
     * @struct NodeSpec
     * @brief Node Spec data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    struct NodeSpec {
        std::string name;                    ///< Unique node name
        std::size_t index = 0;               ///< Index in graph (set during build)
        std::function<std::shared_ptr<INode>()> creator;  ///< Creator lambda
    };

    /**
     * @struct EdgeSpec
     * @brief Specification for an edge in the graph
     */
    /**
     * @struct EdgeSpec
     * @brief Edge Spec data record.
     *
     * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.
     */
    struct EdgeSpec {
        std::string src_name;    ///< Source node name
        std::string dst_name;    ///< Destination node name
        std::size_t src_port;    ///< Source port index
        std::size_t dst_port;    ///< Destination port index
    };

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Construct a new fluent builder
    /// @param provider Optional node provider (uses default if not provided)
    explicit FluentGraphBuilder(
        std::shared_ptr<NodeProviderType> provider = nullptr)
        : provider_(provider) {
    }

    /// @brief Destructor
    ~FluentGraphBuilder() = default;

    // ========================================================================
    // Fluent API Methods
    // ========================================================================

    /**
     * @brief Add a node to the graph
     * 
     * @tparam NodeType The concrete node type to instantiate
     * @param name Unique name for this node instance
     * @return Reference to this builder for method chaining
     *
     * @throws std::invalid_argument if name is empty or already exists
     *
     * Example:
     * @code
     * builder.AddNode<MySourceNode>("source1")
     *        .AddNode<MyProcessorNode>("processor1");
     * @endcode
     */
    template <typename NodeType>
    /**
     * @brief Executes the Add Node operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    FluentGraphBuilder& AddNode(std::string_view name) {
        if (name.empty()) {
            throw std::invalid_argument("Node name cannot be empty");
        }

/**
 * @brief Name str.
 * @param name Parameter for name str.
 * @return Result of the operation.
 */
        std::string name_str(name);
        if (nodes_.count(name_str) > 0) {
            throw std::invalid_argument("Node with name '" + name_str + "' already exists");
        }

        // Create the node spec with lambda creator
        NodeSpec spec;
        spec.name = name_str;
        spec.creator = []() -> std::shared_ptr<INode> {
            return std::make_shared<NodeType>();
        };

        nodes_[name_str] = spec;
        node_order_.push_back(name_str);

        return *this;
    }

    /**
     * @brief Connect two nodes with a typed edge
     *
     * @tparam SrcNode Source node type
     * @tparam SrcPort Source port index
     * @tparam DstNode Destination node type
     * @tparam DstPort Destination port index
     * @param src_name Name of source node (must be already added)
     * @param dst_name Name of destination node (must be already added)
     * @return Reference to this builder for method chaining
     *
     * @throws std::invalid_argument if source or destination node doesn't exist
     * @throws std::runtime_error if edge type is not registered
     *
     * The template parameters provide compile-time type safety, ensuring
     * that the edge connection is valid before the graph is built.
     *
     * Example:
     * @code
     * builder.Connect<SourceNode, 0, ProcessorNode, 0>("source", "processor")
     *        .Connect<ProcessorNode, 0, SinkNode, 0>("processor", "sink");
     * @endcode
     */
    template <typename SrcNode, std::size_t SrcPort,
              typename DstNode, std::size_t DstPort>
    FluentGraphBuilder& Connect(
        std::string_view src_name,
        std::string_view dst_name) {
        
/**
 * @brief Src str.
 * @param src_name Parameter for src str.
 * @return Result of the operation.
 */
        std::string src_str(src_name);
/**
 * @brief Dst str.
 * @param dst_name Parameter for dst str.
 * @return Result of the operation.
 */
        std::string dst_str(dst_name);

        // Validate that nodes exist
        if (nodes_.count(src_str) == 0) {
            throw std::invalid_argument("Source node '" + src_str + "' not found");
        }
        if (nodes_.count(dst_str) == 0) {
            throw std::invalid_argument("Destination node '" + dst_str + "' not found");
        }

        // Check if edge type is registered
        if (!config::EdgeRegistry::IsRegistered(
                typeid(SrcNode).name(), SrcPort,
                typeid(DstNode).name(), DstPort)) {
            
            throw std::runtime_error(
                std::string("Edge type not registered: ") +
                typeid(SrcNode).name() + "::" + std::to_string(SrcPort) +
                " -> " + typeid(DstNode).name() + "::" + std::to_string(DstPort));
        }

        // Store edge specification
        EdgeSpec edge;
        edge.src_name = src_str;
        edge.dst_name = dst_str;
        edge.src_port = SrcPort;
        edge.dst_port = DstPort;
        
        edges_.push_back(edge);

        return *this;
    }

    /**
     * @brief Build the configured graph
     *
     * @return Shared pointer to the constructed GraphManager
     *
     * @throws std::runtime_error if:
     *   - Graph has no nodes
     *   - Edge references non-existent nodes
     *   - Graph construction fails for any reason
     *
     * The returned GraphManager is fully wired and ready for execution.
     *
     * Example:
     * @code
     * auto graph = builder
     *     .AddNode<SourceNode>("source")
     *     .AddNode<SinkNode>("sink")
     *     .Connect<SourceNode, 0, SinkNode, 0>("source", "sink")
     *     .Build();
     *
     * // graph is now ready to use
     * auto executor = GraphExecutor(graph);
     * @endcode
     */
    std::shared_ptr<GraphManager> Build() {
        if (nodes_.empty()) {
            throw std::runtime_error("Cannot build graph: no nodes added");
        }

        // Create GraphManager
        auto graph = std::make_shared<GraphManager>();

        // Add all nodes to the graph in order and track their indices
        std::map<std::string, std::size_t> node_indices;
        for (const auto& name : node_order_) {
            auto& spec = nodes_[name];
            auto node = spec.creator();
            if (!node) {
                throw std::runtime_error("Failed to create node: " + name);
            }
            
            // Track the node index
            node_indices[name] = graph->GetNodes().size();
            spec.index = graph->GetNodes().size();
            
            graph->AddNode(node);
        }

        // Connect all edges
        for (const auto& edge : edges_) {
            auto src_it = node_indices.find(edge.src_name);
            auto dst_it = node_indices.find(edge.dst_name);
            
            if (src_it == node_indices.end()) {
                throw std::runtime_error("Source node not found: " + edge.src_name);
            }
            if (dst_it == node_indices.end()) {
                throw std::runtime_error("Destination node not found: " + edge.dst_name);
            }

            // Create edge using EdgeRegistry
            auto success = config::EdgeRegistry::CreateEdgeExpected(
                *graph,
                typeid(*graph->GetNodes()[src_it->second]).name(), edge.src_port,
                typeid(*graph->GetNodes()[dst_it->second]).name(), edge.dst_port,
                src_it->second,
                dst_it->second,
                1024  // Default buffer size
            );

            if (!success) {
                throw std::runtime_error(
                    "Failed to create edge from " + edge.src_name +
                    " to " + edge.dst_name);
            }
        }

        return graph;
    }

    // ========================================================================
    // Query Methods
    // ========================================================================

    /// @brief Get the number of nodes that have been added
    /// @return Number of nodes
    size_t GetNodeCount() const {
        return nodes_.size();
    }

    /// @brief Get the number of edges that have been added
    /// @return Number of edges
    size_t GetEdgeCount() const {
        return edges_.size();
    }

    /// @brief Get list of all node names in order
    /// @return Vector of node names
    std::vector<std::string> GetNodeNames() const {
        return node_order_;
    }

private:
    // ========================================================================
    // Member Variables
    // ========================================================================

    /// Map of node name to node specification
    std::map<std::string, NodeSpec> nodes_;

    /// Order in which nodes were added (for consistent iteration)
    std::vector<std::string> node_order_;

    /// List of edge specifications
    std::vector<EdgeSpec> edges_;

    /// Optional node provider (for future extensibility)
    std::shared_ptr<NodeProviderType> provider_;
};

}  // namespace graph
