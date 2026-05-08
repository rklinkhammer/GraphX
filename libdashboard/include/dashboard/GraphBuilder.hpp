// MIT License
/// @file GraphBuilder.hpp
/// @brief Application-level GraphBuilder management

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

#include "graph/GraphConfig.hpp"
#include "graph/NodeFacade.hpp"  // Full definition needed for nodes_ vector
#include "graph/ExecutionState.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <log4cxx/logger.h>

// Forward declarations
namespace graph {
    class GraphManager;
}

namespace app {

// ============================================================================
// BuildResult Struct
// ============================================================================

/// @struct BuildResult
/// @brief Contains the result of a successful graph build
///
/// BuildResult holds the constructed graph and metadata about its structure.
/// Used as the output of GraphBuilder::Build() to provide access to the
/// fully-wired GraphManager and statistics about the graph.
///
/// Example:
/// @code
/// auto result = builder.Build();
/// if (result.success) {
///     std::cout << "Built graph with " << result.node_count << " nodes "
///               << "and " << result.edge_count << " edges\n";
///     // Use result.graph for execution
/// }
/// @endcode
struct BuildResult {
    /// Whether the build was successful
    bool success;
    
    /// Error message if build failed (empty if success)
    std::string error_message;
    
    /// The constructed and wired GraphManager
    /// Valid only if success == true
    std::shared_ptr<graph::GraphManager> graph;
    
    /// Number of nodes in the constructed graph
    size_t node_count;
    
    /// Number of edges in the constructed graph
    size_t edge_count;
    
    /// Names of all nodes in the graph (for debugging)
    std::vector<std::string> node_names;
    
    /// Edge descriptions: "source:port → dest:port" format
    std::vector<std::string> edge_descriptions;
};

// ============================================================================
// GraphBuilder Class
// ============================================================================

/// @class GraphBuilder
/// @brief Orchestrates the 6-step process of constructing a dataflow graph
///
/// GraphBuilder coordinates the construction of a complete dataflow graph from
/// JSON configuration. It follows a well-defined 6-step build process:
///
/// **Step 1**: Validate inputs (JSON file exists, nodes available)
/// **Step 2**: Load graph configuration from JSON
/// **Step 3**: Create node instances from factory
/// **Step 4**: Register nodes with GraphManager
/// **Step 5**: Wire edges between nodes
/// **Step 6**: Validate graph topology (optional cycle checking)
///
/// The builder maintains a reference to GraphCapability which provides:
/// - NodeFactory for creating nodes from the plugin system
/// - PluginLoader for plugin lifetime management (CRITICAL)
/// - ConfigManager configuration
///
/// Usage:
/// @code
/// app::GraphCapability capability;
/// // ... populate capability with configuration ...
///
/// app::GraphBuilder builder(capability);
///
/// // Validate before building (optional but recommended)
/// if (!builder.Validate()) {
///     std::cerr << "Validation failed\n";
///     return;
/// }
///
/// // Build the graph
/// auto result = builder.Build();
/// if (!result.success) {
///     std::cerr << "Build failed: " << result.error_message << "\n";
///     return;
/// }
///
/// // Use constructed graph
/// auto graph = result.graph;
/// std::cout << "Constructed graph with " << result.node_count << " nodes\n";
/// @endcode
///
/// @see GraphCapability
/// @see BuildResult
/// @see graph::GraphManager
/// @see graph::FactoryManager
class GraphBuilder {
public:
    // ========================================================================
    // Lifecycle Management
    // ========================================================================

    /// @brief Construct a GraphBuilder with application capability
    /// @param capability Reference to GraphCapability containing configuration
    ///
    /// The capability must contain:
    /// - graph_impl.json_config_path pointing to valid JSON file
    /// - node_factory pointing to valid NodeFactory instance
    /// - plugin_loader (for plugin lifetime management)
    ///
    /// @throws std::invalid_argument if capability is invalid
    explicit GraphBuilder(const std::shared_ptr<capabilities::GraphCapability>& capability);

    /// @brief Destructor
    virtual ~GraphBuilder() = default;

    // ========================================================================
    // Validation and Build Operations
    // ========================================================================

    /// @brief Validate that the graph can be built from configuration
    /// @return true if validation succeeds, false otherwise
    ///
    /// Performs pre-build validation:
    /// - JSON configuration file exists and is readable
    /// - All node types in JSON are available in NodeFactory
    /// - JSON structure is valid
    /// - Edge source/target references are valid
    ///
    /// Call this before Build() to catch errors early.
    /// (Build() calls Validate() internally, so this is optional)
    ///
    /// Example:
    /// @code
    /// GraphBuilder builder(capability);
    /// if (!builder.Validate()) {
    ///     std::cerr << "Configuration invalid\n";
    ///     return;
    /// }
    /// @endcode
    bool Validate();

    /// @brief Orchestrate the complete 6-step build process
    /// @return BuildResult containing graph or error details
    ///
    /// Performs the 6-step build process:
    /// 1. LoadGraphConfiguration() - Parse JSON configuration
    /// 2. CreateNodes() - Instantiate all nodes from factory
    /// 3. RegisterNodes() - Add nodes to GraphManager
    /// 4. WireEdges() - Connect nodes with edges
    /// 5. ValidateTopology() - Check graph structure (optional)
    /// 6. Return BuildResult with constructed graph
    ///
    /// If any step fails, returns BuildResult with success=false
    /// and error_message describing the failure.
    ///
    /// The returned graph is ready for initialization and execution.
    ///
    /// Example:
    /// @code
    /// auto result = builder.Build();
    /// if (result.success) {
    ///     std::cout << "Built " << result.node_count << " nodes\n";
    ///     // Use result.graph->Init() and result.graph->Start()
    /// } else {
    ///     std::cerr << "Build failed: " << result.error_message << "\n";
    /// }
    /// @endcode
    BuildResult Build();

    // ========================================================================
    // Query Operations
    // ========================================================================

    /// @brief Get the number of nodes in the last built graph
    /// @return Number of nodes, or 0 if no successful build
    ///
    /// Returns the node count from the most recent successful Build().
    /// If Build() has not been called or failed, returns 0.
    ///
    /// @see BuildResult::node_count
    size_t GetNodeCount() const { return node_count_; }

    /// @brief Get the number of edges in the last built graph
    /// @return Number of edges, or 0 if no successful build
    ///
    /// Returns the edge count from the most recent successful Build().
    /// If Build() has not been called or failed, returns 0.
    ///
    /// @see BuildResult::edge_count
    size_t GetEdgeCount() const { return edge_count_; }

    /// @brief Get error message from last failed operation
    /// @return Error message string, empty if last operation succeeded
    ///
    /// Contains the error message from the most recent failed Validate()
    /// or Build() operation. Empty string if no error has occurred.
    std::string GetLastError() const { return last_error_; }

    /// @brief Get the node adapters from the last built graph
    /// @return Reference to vector of NodeFacadeAdapters from the built graph
    ///
    /// Returns the actual NodeFacadeAdapters before they were wrapped for
    /// GraphManager. This allows tests and applications to access the original
    /// node instances and cast them to specific types or interfaces.
    ///
    /// @note Only valid after a successful Build(). Returns empty vector if
    ///       Build() has not been called or failed.
    /// @note The returned reference remains valid for the lifetime of the
    ///       GraphBuilder instance.
    std::vector<std::shared_ptr<graph::NodeFacadeAdapter>>& GetNodeAdapters() { 
        return nodes_; 
    }

protected:
    // ========================================================================
    // Protected Members
    // ========================================================================

    std::shared_ptr<capabilities::GraphCapability> capability_;
    std::shared_ptr<graph::GraphManager> graph_;
    std::vector<std::shared_ptr<graph::NodeFacadeAdapter>> nodes_;  // Preserved for lifetime of application
    size_t node_count_ = 0;
    size_t edge_count_ = 0;
    std::string last_error_;
    static log4cxx::LoggerPtr logger_;

private:
    // ========================================================================
    // Private Implementation Methods (6-Step Build Process)
    // ========================================================================

    /// @brief Step 1: Validate input configuration and files
    /// @return true if validation passes, false otherwise
    ///
    /// Validates:
    /// - JSON config file exists and is readable
    /// - GraphCapability has valid factory
    /// - JSON can be parsed as valid graph configuration
    ///
    /// Sets last_error_ on failure with descriptive message.
    bool LoadGraphConfiguration();

    /// @brief Step 2: Create all node instances from factory
    /// @param nodes_config Parsed node configurations from JSON
    /// @return Vector of created NodeFacadeAdapter instances
    ///
    /// Uses NodeFactory to create each node type from the configuration.
    /// Each node is created with its specified parameters.
    ///
    /// @throws std::runtime_error if any node creation fails
    /// (Caught by Build(), converted to BuildResult with error)
    std::vector<std::shared_ptr<graph::NodeFacadeAdapter>> CreateNodes(
        const std::vector<std::string>& node_types);

    /// @brief Step 3.5: Initialize EdgeRegistry with all 14 edge type registrations
    /// @return void
    ///
    /// Registers all 14 edge types (9 main pipeline + 5 completion path edges)
    /// with the EdgeRegistry singleton. This allows WireEdges() to create
    /// runtime edges using EdgeRegistry::CreateEdge() dispatch.
    ///
    /// Called immediately after GraphManager creation (Step 3) and before
    /// node registration (Step 4).
    ///
    /// Registers the following edge combinations:
    /// - 5 CSV sensor inputs to FlightFSMNode (ports 0-4)
    /// - FSM → Fusion → Pipeline → Monitor → Sink main pipeline
    /// - 5 CSV sensor completion signals to CompletionAggregatorNode (ports 0-4)
    ///
    /// @throws Nothing - logs errors internally, fails gracefully
    void InitializeEdgeRegistry();

    /// @brief Step 4: Register nodes with GraphManager
    /// @param nodes Vector of created node facades
    /// @return void
    ///
    /// Adds all created nodes to the GraphManager's internal registry.
    /// Must be done before edge wiring.
    ///
    /// Throws on error (caught by Build()).
    void RegisterNodes(std::vector<std::shared_ptr<graph::NodeFacadeAdapter>>& nodes);

    /// @brief Step 4: Wire edges between all specified node pairs
    /// @param edge_configs Parsed edge configurations from JSON
    /// @param node_type_map Mapping of node name → (index, type) for type-aware edge creation
    /// @return void
    ///
    /// Uses EdgeRegistry to create type-aware connections between nodes.
    /// Dispatches through EdgeRegistry::CreateEdge() which uses the registered
    /// creator lambdas to instantiate Edge<SrcNode, Port, DstNode, Port> instances.
    ///
    /// @throws std::runtime_error if:
    ///   - Edge configuration is invalid
    ///   - Edge references non-existent nodes
    ///   - Port numbers are out of range
    ///   - No creator is registered for edge type combination
    /// (Caught by Build(), converted to BuildResult with error)
    void WireEdges(
        const std::vector<graph::EdgeConfig>& edge_configs,
        const std::map<std::string, std::pair<size_t, std::string>>& node_type_map);

    /// @brief Step 5: Validate resulting graph topology (optional)
    /// @return true if topology is valid, false otherwise
    ///
    /// Performs validation:
    /// - No orphaned nodes (all reachable from source)
    /// - Cycle detection (if backpressure disabled)
    /// - No duplicate edges
    ///
    /// Can be skipped if topology validation is disabled.
    /// Sets last_error_ on failure.
    bool ValidateTopology();
};

}  // namespace app

