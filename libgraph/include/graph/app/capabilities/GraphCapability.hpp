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

#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include "graph/CapabilityBus.hpp"
#include "graph/GraphConfig.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeFactory.hpp"
#include "plugins/PluginLoader.hpp"
#include "plugins/PluginRegistry.hpp"

/**
 * @file MetricsCapability.hpp
 * @brief Capability interface for metrics discovery and subscription
 *
 * Provides methods for discovering node metrics schemas and registering
 * callbacks to receive metrics events published during graph execution.
 */

namespace app::capabilities {

class GraphCapability {
public:
    virtual ~GraphCapability() = default;
    
    /// @brief Set the GraphManager instance
    /// @param graph_manager Shared pointer to GraphManager
    void SetGraphManager(std::shared_ptr<graph::GraphManager> gm) {
        graph_manager = gm;
    }

    /// @brief Get the GraphManager instance
    /// @return Shared pointer to GraphManager
    std::shared_ptr<graph::GraphManager> GetGraphManager() const {
        return graph_manager;
    }

    void SetPluginRegistry(std::shared_ptr<graph::PluginRegistry> registry) {
        plugin_registry = registry;
    }
   
    std::shared_ptr<graph::PluginRegistry> GetPluginRegistry() const {
        return plugin_registry;
    }
    
    void SetPluginLoader(std::shared_ptr<graph::PluginLoader> loader) {
        plugin_loader = loader;
    }
    
    std::shared_ptr<graph::PluginLoader> GetPluginLoader() const {
        return plugin_loader;
    }
    
    void SetNodeFactory(std::shared_ptr<graph::NodeFactory> nf) {
        node_factory = nf;
    }
    
    std::shared_ptr<graph::NodeFactory> GetNodeFactory() const {
        return node_factory;
    }

    /// @brief Set the node names for the graph
    /// @param names Vector of node names
    void SetNodeNames(const std::vector<std::string>& names) {
        node_names = names;
    }

    /// @brief Get the node names from the graph
    /// @return Const reference to vector of node names
    const std::vector<std::string>& GetNodeNames() const {
        return node_names;
    }

    /// @brief Set the edge descriptions for the graph
    /// @param descriptions Vector of edge description strings in format "source:port → dest:port"
    void SetEdgeDescriptions(const std::vector<std::string>& descriptions) {
        edge_descriptions = descriptions;
    }

    /// @brief Get the edge descriptions from the graph
    /// @return Const reference to vector of edge descriptions
    const std::vector<std::string>& GetEdgeDescriptions() const {
        return edge_descriptions;
    }

    std::string GetJsonConfigPath() const {
        return json_config_path;
    }   
    
    void SetJsonConfigPath(const std::string& path) {
        json_config_path = path;
    }

    /// @brief Check if stop has been requested
    /// @return true if stop requested, false otherwise
    bool IsStopped() const
    {
        return is_stopped.load();
    }

    /// @brief Request application stop
    void SetStopped() const
    {
        is_stopped.store(true);
    }

    /// @brief Check if CLI mode is enabled
    /// @return true if CLI mode is enabled, false otherwise

    bool IsCliMode() const {
        return cli_mode_;
    }

    /// @brief Set CLI mode
    /// @param enabled If true, enable CLI mode; if false, disable CLI mode

    void SetCliMode(bool enabled) {
        cli_mode_ = enabled;
    }

    /// @brief Get the capability bus for inter-capability communication
    /// @return Reference to the capability bus
    graph::CapabilityBus& GetCapabilityBus()
    {
        return capability_bus;
    }

private:

    std::string json_config_path;
    //graph::GraphConfig graph_impl;
    std::shared_ptr<graph::PluginRegistry> plugin_registry  {nullptr};
    /// Plugin loader - CRITICAL: Must outlive all plugin function calls
    /// Keep this alive throughout entire application lifetime.
    /// Calling dlclose() on plugin_loader invalidates all plugin function pointers.
    /// Root cause of previous graphsim segfaults.
    std::shared_ptr<graph::PluginLoader> plugin_loader  {nullptr};
    std::shared_ptr<graph::NodeFactory> node_factory {nullptr};
    std::shared_ptr<graph::GraphManager> graph_manager {nullptr};
    std::vector<std::string> node_names;
    std::vector<std::string> edge_descriptions;
    mutable std::atomic<bool> is_stopped{false};
    bool cli_mode_{false};
    graph::CapabilityBus capability_bus;
};

}  // namespace app::capabilities

