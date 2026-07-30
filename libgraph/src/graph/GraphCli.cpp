#include "graph/GraphCli.hpp"
#include "capabilities/CommandCapability.hpp"
#include "graph/ExecutionState.hpp"
#include "graph/GraphExecutor.hpp"
#include "graph/GraphExecutorBuilder.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <utility>
#include <thread>

namespace graph {

bool GraphCli::LoadGraph(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not load file '" << filepath 
                  << "' (file not found or permission denied)\n";
        return false;
    }
    
    try {
        file >> graph_;
        
        // Validate graph structure
        if (!graph_.is_object()) {
            std::cerr << "Error: Graph file must contain a JSON object\n";
            graph_.clear();
            return false;
        }
        
        // Create coordinator for the loaded graph
        coordinator_ = std::make_unique<GraphCoordinator>(graph_);
        executor_.reset();
        commands_.reset();
        graph_path_ = filepath;
        dirty_ = false;
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error: Invalid JSON in file '" << filepath 
                  << "': " << e.what() << "\n";
        graph_.clear();
        coordinator_.reset();
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to load graph: " << e.what() << "\n";
        graph_.clear();
        coordinator_.reset();
        return false;
    }
}

bool GraphCli::SaveGraph(const std::string& filepath) {
    if (graph_.empty()) {
        std::cerr << "Error: No graph loaded. Load a graph before saving.\n";
        return false;
    }
    
    try {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Error: Could not write to file '" << filepath 
                      << "' (permission denied or invalid path)\n";
            return false;
        }
        
        graph_ = coordinator_->GetGraphJson();
        file << std::setw(2) << graph_ << std::endl;
        file.close();
        graph_path_ = filepath;
        dirty_ = false;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to save graph: " << e.what() << "\n";
        return false;
    }
}

std::string GraphCli::ShowGraph(const std::string& format) {
    if (!coordinator_) {
        return "Error: No graph loaded";
    }
    graph_ = coordinator_->GetGraphJson();
    
    if (format == "json") {
        return graph_.dump(2);
    }
    
    // Default table format
    std::ostringstream oss;
    if (graph_.contains("nodes") && graph_["nodes"].is_array()) {
        oss << "Nodes (" << graph_["nodes"].size() << " total):\n";
        oss << std::left << std::setw(20) << "ID" 
            << std::setw(20) << "Type" 
            << "Config\n";
        oss << std::string(60, '-') << "\n";
        
        for (const auto& node : graph_["nodes"]) {
            std::string id = node.value("id", "unknown");
            std::string type = node.value("type", "unknown");
            std::string config = node.value("node_config", nlohmann::json::object()).dump();
            if (config.length() > 30) {
                config = config.substr(0, 27) + "...";
            }
            
            oss << std::left << std::setw(20) << id 
                << std::setw(20) << type 
                << config << "\n";
        }
    }
    
    return oss.str();
}

std::string GraphCli::ListNodes(const std::string& type, const std::string& format) {
    if (!coordinator_) {
        return "Error: No graph loaded";
    }
    
    try {
        std::vector<nlohmann::json> nodes;
        
        if (type.empty()) {
            // List all nodes
            auto ids = coordinator_->GetNodeIds();
            for (const auto& id : ids) {
                auto node = coordinator_->GetNode(id);
                if (!node.is_null()) {
                    nodes.push_back(node);
                }
            }
        } else {
            // List nodes by type
            nodes = coordinator_->GetNodesByType(type);
        }
        
        if (format == "json") {
            nlohmann::json result = nlohmann::json::array();
            for (const auto& node : nodes) {
                result.push_back(node);
            }
            return result.dump(2);
        }
        
        // Default table format
        std::ostringstream oss;
        oss << "Nodes" << (type.empty() ? "" : " (type: " + type + ")") 
            << " (" << nodes.size() << " total):\n";
        oss << std::left << std::setw(20) << "ID" 
            << std::setw(20) << "Type" 
            << "Config Preview\n";
        oss << std::string(60, '-') << "\n";
        
        for (const auto& node : nodes) {
            std::string id = node.value("id", "unknown");
            std::string node_type = node.value("type", "unknown");
            std::string config = node.value("node_config", nlohmann::json::object()).dump();
            if (config.length() > 30) {
                config = config.substr(0, 27) + "...";
            }
            
            oss << std::left << std::setw(20) << id 
                << std::setw(20) << node_type 
                << config << "\n";
        }
        
        return oss.str();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

std::string GraphCli::GetNode(const std::string& id, const std::string& format) {
    if (!coordinator_) {
        return "Error: No graph loaded";
    }
    
    try {
        auto node = coordinator_->GetNode(id);
        if (node.is_null()) {
            return "Error: Node '" + id + "' not found";
        }
        
        if (format == "json") {
            return node.dump(2);
        }
        
        // Default table format
        std::ostringstream oss;
        oss << "Node: " << id << "\n";
        oss << std::string(40, '-') << "\n";
        oss << "ID: " << node.value("id", "unknown") << "\n";
        oss << "Type: " << node.value("type", "unknown") << "\n";
        oss << "Config:\n";
        oss << node.value("node_config", nlohmann::json::object()).dump(2) << "\n";
        
        return oss.str();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

bool GraphCli::UpdateNode(const std::string& id, const nlohmann::json& config) {
    if (!coordinator_) {
        std::cerr << "Error: No graph loaded\n";
        return false;
    }
    
    try {
        bool success = coordinator_->UpdateNodeConfig(id, config);
        if (!success) {
            std::cerr << "Error: Node '" << id << "' not found\n";
        } else {
            dirty_ = true;
            graph_ = coordinator_->GetGraphJson();
            executor_.reset();
            commands_.reset();
        }
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }
}

bool GraphCli::Init() {
    if (!coordinator_ || graph_path_.empty()) {
        std::cerr << "Error: No graph loaded\n";
        return false;
    }
    if (dirty_) {
        std::cerr << "Error: Graph has unsaved changes. Save before init.\n";
        return false;
    }
    try {
        executor_ = GraphExecutorBuilder()
                        .WithGraphSnapshot(coordinator_->Snapshot())
                        .WithPluginDirectory(plugin_directory_)
                        .Build();
        if (!executor_) {
            std::cerr << "Error: Could not build GraphExecutor\n";
            return false;
        }
        commands_ =
            executor_->GetCapability<capabilities::CommandCapability>();
        const auto result = commands_->Submit(
            {.name = capabilities::CommandName::Init,
             .coordinator_revision = coordinator_->Snapshot().Revision()});
        if (!result.success) {
            std::cerr << "Error: " << result.message << "\n";
        }
        return result.success;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        executor_.reset();
        return false;
    }
}

bool GraphCli::Start() {
    if (!commands_) {
        std::cerr << "Error: Executor is not initialized\n";
        return false;
    }
    const auto result = commands_->Submit(
        {.name = capabilities::CommandName::Start,
         .coordinator_revision = coordinator_->Snapshot().Revision()});
    if (!result.success) {
        std::cerr << "Error: " << result.message << "\n";
    }
    return result.success;
}

bool GraphCli::Run() {
    if (!commands_) {
        std::cerr << "Error: Executor is not initialized\n";
        return false;
    }
    auto result = commands_->Submit(
        {.name = capabilities::CommandName::Run,
         .coordinator_revision = coordinator_->Snapshot().Revision()});
    while (result.success &&
           (result.status == capabilities::OperationStatus::Accepted ||
            result.status == capabilities::OperationStatus::Running)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto current = commands_->GetOperation(result.operation_id);
        if (!current) {
            result.success = false;
            result.message = "Run operation result expired";
            break;
        }
        result = *current;
    }
    if (!result.success) {
        std::cerr << "Error: " << result.message << "\n";
    }
    return result.success;
}

bool GraphCli::Stop() {
    if (!commands_) {
        std::cerr << "Error: Executor is not initialized\n";
        return false;
    }
    const auto result = commands_->Submit(
        {.name = capabilities::CommandName::Stop,
         .coordinator_revision = coordinator_->Snapshot().Revision()});
    if (!result.success) {
        std::cerr << "Error: " << result.message << "\n";
    }
    return result.success;
}

bool GraphCli::Join() {
    if (!commands_) {
        std::cerr << "Error: Executor is not initialized\n";
        return false;
    }
    auto result = commands_->Submit(
        {.name = capabilities::CommandName::Join,
         .coordinator_revision = coordinator_->Snapshot().Revision()});
    while (result.success &&
           (result.status == capabilities::OperationStatus::Accepted ||
            result.status == capabilities::OperationStatus::Running)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto current = commands_->GetOperation(result.operation_id);
        if (!current) {
            result.success = false;
            result.message = "Join operation result expired";
            break;
        }
        result = *current;
    }
    if (!result.success) {
        std::cerr << "Error: " << result.message << "\n";
    }
    return result.success;
}

std::string GraphCli::GetState() {
    if (!executor_) {
        return "NOT_INITIALIZED";
    }
    switch (executor_->GetExecutionState()) {
    case ExecutionState::CONFIGURED:
        return "CONFIGURED";
    case ExecutionState::INITIALIZED:
        return "INITIALIZED";
    case ExecutionState::PAUSED:
        return "PAUSED";
    case ExecutionState::RUNNING:
        return "RUNNING";
    case ExecutionState::STEPPING:
        return "STEPPING";
    case ExecutionState::STOPPING:
        return "STOPPING";
    case ExecutionState::STOPPED:
        return "STOPPED";
    case ExecutionState::ERROR:
        return "ERROR";
    case ExecutionState::ANY:
        return "ANY";
    }
    return "UNKNOWN";
}

void GraphCli::SetPluginDirectory(std::string directory) {
    if (!directory.empty()) {
        plugin_directory_ = std::move(directory);
        executor_.reset();
        commands_.reset();
    }
}

}  // namespace graph
