/**
 * @file GraphCoordinator.cpp
 * @brief Implementation of generic graph parameter viewer and editor.
 */

#include "graph/GraphCoordinator.hpp"
#include <mutex>

namespace graph {

GraphCoordinator::GraphCoordinator(nlohmann::json graph)
    : graph_(std::move(graph)) {}

nlohmann::json GraphCoordinator::GetGraphJson() const {
    std::lock_guard<std::mutex> lock(graph_lock_);
    return graph_;
}

GraphConfigurationSnapshot GraphCoordinator::Snapshot() const {
    std::lock_guard<std::mutex> lock(graph_lock_);
    return GraphConfigurationSnapshot(graph_, revision_);
}

nlohmann::json GraphCoordinator::GetNode(const std::string& id) const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return nlohmann::json();
    }

    const auto& nodes = graph_["nodes"];
    for (const auto& node : nodes) {
        if (node.contains("id") && node["id"].is_string() &&
            node["id"].get<std::string>() == id) {
            return node;  // Return copy
        }
    }

    return nlohmann::json();  // Not found
}

nlohmann::json GraphCoordinator::GetNodeConfig(const std::string& id) const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return nlohmann::json();
    }

    const auto& nodes = graph_["nodes"];
    for (const auto& node : nodes) {
        if (node.contains("id") && node["id"].is_string() &&
            node["id"].get<std::string>() == id) {
            if (node.contains("node_config")) {
                return node["node_config"];  // Return copy
            }
            return nlohmann::json();
        }
    }

    return nlohmann::json();  // Not found
}

std::vector<nlohmann::json> GraphCoordinator::GetNodesByType(
    const std::string& type) const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    std::vector<nlohmann::json> results;

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return results;
    }

    const auto& nodes = graph_["nodes"];
    for (const auto& node : nodes) {
        if (node.contains("type") && node["type"].is_string() &&
            node["type"].get<std::string>() == type) {
            results.push_back(node);  // Push copy
        }
    }

    return results;
}

size_t GraphCoordinator::GetNodeCount() const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return 0;
    }

    return graph_["nodes"].size();
}

std::vector<std::string> GraphCoordinator::GetNodeIds() const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    std::vector<std::string> ids;

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return ids;
    }

    const auto& nodes = graph_["nodes"];
    for (const auto& node : nodes) {
        if (node.contains("id") && node["id"].is_string()) {
            ids.push_back(node["id"].get<std::string>());
        }
    }

    return ids;
}

bool GraphCoordinator::HasNode(const std::string& id) const {
    std::lock_guard<std::mutex> lock(graph_lock_);

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return false;
    }

    const auto& nodes = graph_["nodes"];
    for (const auto& node : nodes) {
        if (node.contains("id") && node["id"].is_string() &&
            node["id"].get<std::string>() == id) {
            return true;
        }
    }

    return false;
}

bool GraphCoordinator::UpdateNodeConfig(const std::string& id,
                                       const nlohmann::json& node_config) {
    std::lock_guard<std::mutex> lock(graph_lock_);

    if (!graph_.contains("nodes") || !graph_["nodes"].is_array()) {
        return false;
    }

    auto& nodes = graph_["nodes"];
    for (auto& node : nodes) {
        if (node.contains("id") && node["id"].is_string() &&
            node["id"].get<std::string>() == id) {
            const auto current = node.contains("node_config")
                                     ? node["node_config"]
                                     : nlohmann::json{};
            if (current == node_config) {
                return true;
            }
            node["node_config"] = node_config;
            ++revision_;
            return true;
        }
    }

    return false;
}

}  // namespace graph
