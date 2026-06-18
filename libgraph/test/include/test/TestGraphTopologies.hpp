// SPDX-License-Identifier: MIT

/**
 * @file TestGraphTopologies.hpp
 * @brief Test Graph Topologies Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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
    SourceOnly = 9,            ///< Single source node (edge case)
    MinimalIntProducer = 10,                ///< TestIntProducer -> TestIntSinkNode + CompletionNode
    LinearSequentialIntProducer = 11,       ///< TestIntProducer -> TestIntSinkNode with completion
    MinimalDoubleProducer = 12,             ///< TestDoubleProducer -> TestDoubleSinkNode + CompletionNode
    LinearSequentialDoubleProducer = 13     ///< TestDoubleProducer -> TestDoubleSinkNode with completion
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
/**
 * @class TopologyBuilder
 * @brief Topology Builder builder.
 *
 * @details Collects configuration and constructs GraphX runtime objects in a predictable order. Builder methods are intended to be chained before final construction.
 */
class TopologyBuilder {
public:
    /**
     * @brief Build a graph topology by type
     * @param type The topology type to build
     * @return Shared pointer to the constructed GraphManager
     * @throws std::invalid_argument if type is invalid
     */
/**
 * @brief Build topology.
 * @param type Parameter for build topology.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildTopology(TopologyType type);

    /**
     * @brief Get metadata for a topology type
     * @param type The topology type
     * @return TopologyMetadata describing the topology
     */
/**
 * @brief Get topology metadata.
 * @param type Parameter for get topology metadata.
 * @return Result of the operation.
 */
    static TopologyMetadata GetTopologyMetadata(TopologyType type);

    /**
     * @brief Get all available topology types
     * @return Vector of all TopologyType values
     */
/**
 * @brief Get all topology types.
 * @return Result of the operation.
 */
    static std::vector<TopologyType> GetAllTopologyTypes();

private:
    // Topology builders
/**
 * @brief Build linear sequential.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildLinearSequential();
/**
 * @brief Build merge simple.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildMergeSimple();
/**
 * @brief Build split simple.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildSplitSimple();
/**
 * @brief Build diamond complex.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildDiamondComplex();
/**
 * @brief Build multi path sequential.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildMultiPathSequential();
/**
 * @brief Build interior to merge.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildInteriorToMerge();
/**
 * @brief Build parallel merge with interior.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildParallelMergeWithInterior();
/**
 * @brief Build complex network.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildComplexNetwork();
/**
 * @brief Build minimal graph.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildMinimalGraph();
/**
 * @brief Build source only.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildSourceOnly();
    
    // Producer topology builders
/**
 * @brief Build minimal int producer.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildMinimalIntProducer();
/**
 * @brief Build linear sequential int producer.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildLinearSequentialIntProducer();
/**
 * @brief Build minimal double producer.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildMinimalDoubleProducer();
/**
 * @brief Build linear sequential double producer.
 * @return Result of the operation.
 */
    static std::shared_ptr<graph::GraphManager> BuildLinearSequentialDoubleProducer();
};

// ============================================================================
// Topology Descriptions
// ============================================================================

/**
 * @brief Get a human-readable ASCII diagram of a topology
 * @param type The topology type
 * @return ASCII diagram string
 */
/**
 * @brief Get topology diagram.
 * @param type Parameter for get topology diagram.
 * @return Result of the operation.
 */
std::string GetTopologyDiagram(TopologyType type);

/**
 * @brief Get detailed documentation for a topology
 * @param type The topology type
 * @return Detailed description string
 */
/**
 * @brief Get topology documentation.
 * @param type Parameter for get topology documentation.
 * @return Result of the operation.
 */
std::string GetTopologyDocumentation(TopologyType type);

} // namespace test
