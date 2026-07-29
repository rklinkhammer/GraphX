#pragma once

#include "GraphCoordinator.hpp"

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace graph {

class GraphExecutor;

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

    /// Select the plugin directory used when building the executor.
    void SetPluginDirectory(std::string directory);

    /// True when in-memory edits have not been saved to the active graph file.
    bool HasUnsavedChanges() const { return dirty_; }
    
    /// Get internal graph for direct access (for testing)
    const nlohmann::json& GetGraphJson() const { return graph_; }
    
    /// Check if graph is loaded
    bool IsGraphLoaded() const { return !graph_.empty(); }
    
private:
    nlohmann::json graph_;
    std::unique_ptr<GraphCoordinator> coordinator_;  // Created after graph loaded
    std::shared_ptr<GraphExecutor> executor_;
    std::string graph_path_;
    std::string plugin_directory_ = "./plugins";
    bool dirty_ = false;
    
    /// Format table for output
    std::string FormatAsTable(const std::vector<nlohmann::json>& nodes) const;
};

}  // namespace graph
