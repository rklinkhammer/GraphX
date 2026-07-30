/**
 * @file GraphCoordinator.hpp
 * @brief Generic graph parameter viewer and editor with thread-safe access.
 *
 * GraphCoordinator provides an in-memory interface for viewing and editing node
 * parameters in any graph. It is domain-agnostic and works with any graph JSON
 * structure. Thread-safe operations are synchronized with std::mutex.
 *
 * Key capabilities:
 * - View nodes by ID, type, or list all nodes
 * - Edit node parameters (node_config) in-memory only
 * - Thread-safe concurrent access
 *
 * Key limitations (by design):
 * - Cannot add/remove nodes
 * - Cannot change node types or IDs
 * - No file I/O (caller's responsibility)
 * - No validation of node_config contents
 *
 * @details All read operations return copies (never references) to prevent
 * post-unlock mutations. The coordinator owns the document so no mutable alias
 * can bypass locking or revision accounting.
 */

#pragma once

#include "graph/GraphConfigurationSnapshot.hpp"

#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace graph {

/**
 * @brief Generic graph parameter editor with thread-safe access.
 *
 * Manages viewing and editing node parameters in a graph JSON object.
 * All operations are thread-safe using std::mutex.
 *
 * Usage:
 * @code
 * nlohmann::json graph = LoadGraphJson("graph.json");
 * graph::GraphCoordinator coordinator(graph);
 *
 * // View nodes
 * auto node = coordinator.GetNode("node_1");
 * auto count = coordinator.GetNodeCount();
 *
 * // Edit parameters
 * nlohmann::json new_config;
 * new_config["param1"] = 42;
 * coordinator.UpdateNodeConfig("node_1", new_config);
 *
 * // User saves graph to file (caller's responsibility)
 * SaveGraphJson("graph.json", coordinator.GetGraphJson());
 * @endcode
 */
class GraphCoordinator {
public:
    /**
     * @brief Construct a GraphCoordinator for the given graph.
     *
     * @param graph nlohmann::json graph object to own.
     *              The graph must contain a "nodes" array with node objects.
     *              Each node must have "id" and "type" fields.
     *
     */
    explicit GraphCoordinator(nlohmann::json graph);

    ~GraphCoordinator() = default;

    // ========== Node Inspection (Read-Only, Thread-Safe) ==========

    /**
     * @brief Get a copy of the entire graph JSON.
     *
     * @return nlohmann::json Copy of the entire graph object including all nodes.
     *         Returns empty object if graph_ is invalid or missing "nodes" array.
     */
    nlohmann::json GetGraphJson() const;

    /**
     * Return the complete document, revision, and deterministic content
     * identity from one critical section.
     */
    [[nodiscard]] GraphConfigurationSnapshot Snapshot() const;

    /**
     * @brief Get a copy of a single node by ID.
     *
     * @param id Node ID to look up.
     * @return nlohmann::json Copy of the node object (id, type, node_config),
     *         or null if node not found.
     */
    nlohmann::json GetNode(const std::string& id) const;

    /**
     * @brief Get a copy of a node's configuration/parameters.
     *
     * @param id Node ID to look up.
     * @return nlohmann::json Copy of node["node_config"], or null if not found.
     */
    nlohmann::json GetNodeConfig(const std::string& id) const;

    /**
     * @brief Get all nodes of a specific type.
     *
     * @param type Node type to filter by.
     * @return std::vector<nlohmann::json> Vector of matching node objects
     *         (copies, not references). Empty vector if no matches found.
     */
    std::vector<nlohmann::json> GetNodesByType(const std::string& type) const;

    /**
     * @brief Get the number of nodes in the graph.
     *
     * @return size_t Number of nodes in graph_["nodes"] array.
     *         Returns 0 if graph has no "nodes" array or is empty.
     */
    size_t GetNodeCount() const;

    /**
     * @brief Get a list of all node IDs in the graph.
     *
     * @return std::vector<std::string> Vector of node IDs.
     *         Empty vector if no nodes found.
     */
    std::vector<std::string> GetNodeIds() const;

    /**
     * @brief Check if a node with the given ID exists.
     *
     * @param id Node ID to check.
     * @return bool true if node found, false otherwise.
     */
    bool HasNode(const std::string& id) const;

    // ========== Parameter Editing (In-Memory, Thread-Safe) ==========

    /**
     * @brief Update a node's parameters (node_config only).
     *
     * Updates the node_config object for the node with the given ID.
     * This modifies the graph in-memory only. Changes are NOT automatically
     * persisted to disk - the caller is responsible for saving the graph.
     *
     * @param id Node ID to update.
     * @param node_config New parameter object (replaces entire node["node_config"]).
     * @return bool true if node found and updated, false if node not found.
     *
     * @note Node ID and type are immutable. Only node_config can be edited.
     * @note No validation is performed on node_config contents.
     *       Caller is responsible for validation.
     * @note If node["node_config"] doesn't exist, it will be created.
     */
    bool UpdateNodeConfig(const std::string& id,
                         const nlohmann::json& node_config);

    // Delete copy and move constructors
    GraphCoordinator(const GraphCoordinator&) = delete;
    GraphCoordinator(GraphCoordinator&&) = delete;
    GraphCoordinator& operator=(const GraphCoordinator&) = delete;
    GraphCoordinator& operator=(GraphCoordinator&&) = delete;

private:
    mutable std::mutex graph_lock_;  ///< Protects access to graph_
    nlohmann::json graph_;            ///< Authoritative owned graph document
    std::uint64_t revision_{0};
};

}  // namespace graph
