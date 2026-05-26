/**
 * @file TestGraphTopologies.hpp
 * @brief Graph topologies for testing GraphExecutorBuilder and related classes
 *
 * Provides a collection of predefined graph topologies using FluentGraphBuilder
 * for comprehensive testing of graph construction, execution, and validation.
 *
 * Topologies:
 * 1. Linear/Sequential: Source -> Interior -> Sink
 * 2. Merge Topology: Source + Source -> Merge -> Sink
 * 3. Split Topology: Source -> Split -> Sink + Sink
 * 4. Diamond Topology: Source -> Split -> Interior + Interior -> Merge -> Sink
 * 5. Multi-Path Sequential: Source -> Interior -> Interior -> Interior -> Sink
 * 6. Interior to Merge: Source -> Interior -> Merge -> Sink
 * 7. Parallel Merge: Source + Source + Interior -> 3-way merge -> Sink
 * 8. Complex Network: Multiple interleaved merge/split operations
 * 9. Minimal Graph: Source -> Sink (baseline)
 * 10. Source Only: Single source node (edge case)
 *
 * Each topology is independent and can be instantiated for specific tests.
 *
 * @author Test Suite
 * @date 2026-05-11
 */

#pragma once

#include "graph/FluentGraphBuilder.hpp"
#include "test/AdvancedTestNodes.hpp"
#include <memory>
#include <string>

namespace test {

/// @struct TopologyMetadata
/// @brief Metadata describing a graph topology
struct TopologyMetadata {
    std::string name;                ///< Topology name
    std::string description;         ///< Human-readable description
    size_t expected_node_count;      ///< Expected number of nodes
    size_t expected_edge_count;      ///< Expected number of edges
    std::vector<std::string> node_names;  ///< List of node names in topology
};

/// @enum TopologyType
/// @brief Enumeration of all available topologies
enum class TopologyType {
    LinearSequential = 0,      ///< Source -> Interior -> Sink
    MergeSimple = 1,           ///< Source + Source -> Merge -> Sink
    SplitSimple = 2,           ///< Source -> Split -> Sink + Sink
    DiamondComplex = 3,        ///< Source -> Split -> Interior + Interior -> Merge -> Sink
    MultiPathSequential = 4,   ///< Source -> Interior -> Interior -> Interior -> Sink
    InteriorToMerge = 5,       ///< Source -> Interior -> Merge -> Sink
    ParallelMergeWithInterior = 6,  ///< Source + Source + Interior -> Merge -> Sink
    ComplexNetwork = 7,        ///< Complex interleaved merge/split operations
    MinimalGraph = 8,          ///< Source -> Sink (baseline)
    SourceOnly = 9             ///< Single source node (edge case)
};

// ============================================================================
// Topology Builders
// ============================================================================

/**
 * @class TopologyBuilder
 * @brief Factory for creating various graph topologies
 *
 * Provides static methods to build different graph topologies for testing.
 */
class TopologyBuilder {
public:
    /**
     * @brief Build a graph topology by type
     * @param type The topology type to build
     * @return Shared pointer to the constructed GraphManager
     * @throws std::invalid_argument if type is invalid
     */
    static std::shared_ptr<graph::GraphManager> BuildTopology(TopologyType type);

    /**
     * @brief Get metadata for a topology type
     * @param type The topology type
     * @return TopologyMetadata describing the topology
     */
    static TopologyMetadata GetTopologyMetadata(TopologyType type);

    /**
     * @brief Get all available topology types
     * @return Vector of all TopologyType values
     */
    static std::vector<TopologyType> GetAllTopologyTypes();

private:
    // Topology builders
    static std::shared_ptr<graph::GraphManager> BuildLinearSequential();
    static std::shared_ptr<graph::GraphManager> BuildMergeSimple();
    static std::shared_ptr<graph::GraphManager> BuildSplitSimple();
    static std::shared_ptr<graph::GraphManager> BuildDiamondComplex();
    static std::shared_ptr<graph::GraphManager> BuildMultiPathSequential();
    static std::shared_ptr<graph::GraphManager> BuildInteriorToMerge();
    static std::shared_ptr<graph::GraphManager> BuildParallelMergeWithInterior();
    static std::shared_ptr<graph::GraphManager> BuildComplexNetwork();
    static std::shared_ptr<graph::GraphManager> BuildMinimalGraph();
    static std::shared_ptr<graph::GraphManager> BuildSourceOnly();
};

// ============================================================================
// Topology Descriptions
// ============================================================================

/**
 * @brief Get a human-readable ASCII diagram of a topology
 * @param type The topology type
 * @return ASCII diagram string
 */
std::string GetTopologyDiagram(TopologyType type);

/**
 * @brief Get detailed documentation for a topology
 * @param type The topology type
 * @return Detailed description string
 */
std::string GetTopologyDocumentation(TopologyType type);

} // namespace test
