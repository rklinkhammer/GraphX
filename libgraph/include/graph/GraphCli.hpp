#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "GraphCoordinator.hpp"

namespace graph {

/// GraphCli: Command-line interface for graph management
/// Provides file I/O, querying, and editing operations on graphs
class GraphCli {
public:
    /// Default constructor
    GraphCli() = default;
    
    /// Session management
    bool LoadGraph(const std::string& filepath);
    bool SaveGraph(const std::string& filepath);
    
    /// Query operations
    std::string ShowGraph(const std::string& format = "table");
    std::string ListNodes(const std::string& type = "", const std::string& format = "table");
    std::string GetNode(const std::string& id, const std::string& format = "table");
    
    /// Edit operations
    bool UpdateNode(const std::string& id, const nlohmann::json& config);
    
    /// Execution operations
    bool Init();
    bool Start();
    bool Run();
    bool Stop();
    bool Join();
    std::string GetState();
    
    /// Get internal graph for direct access (for testing)
    const nlohmann::json& GetGraphJson() const { return graph_; }
    
    /// Check if graph is loaded
    bool IsGraphLoaded() const { return !graph_.empty(); }
    
private:
    nlohmann::json graph_;
    std::unique_ptr<GraphCoordinator> coordinator_;  // Created after graph loaded
    
    /// Format table for output
    std::string FormatAsTable(const std::vector<nlohmann::json>& nodes) const;
};

}  // namespace graph
