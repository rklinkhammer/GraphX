/**
 * @file GraphCapability.hpp
 * @brief Graph Capability Graph runtime support.
 *
 * @details Provides capability API used to share runtime services between policies, nodes, and executors. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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

#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include "graph/CapabilityBus.hpp"
#include "graph/GraphConfig.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeProvider.hpp"


namespace capabilities {

/**
 * @class GraphCapability
 * @brief Graph Capability capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class GraphCapability {
public:
    /**
     * @brief Releases resources owned by Graph Capability.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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

    /**
     * @brief Updates the Node Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param provider Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void SetNodeProvider(std::shared_ptr<graph::INodeProvider> provider) {
        node_provider = std::move(provider);
    }

    /**
     * @brief Returns the Node Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::shared_ptr<graph::INodeProvider> GetNodeProvider() const {
        return node_provider;
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

    /**
     * @brief Returns the JSON Config Path.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    std::string GetJsonConfigPath() const {
        return json_config_path;
    }   
    
    /**
     * @brief Updates the JSON Config Path.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param path Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
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

    /// @brief Check if completion has been signaled
    /// @return true if completion signaled, false otherwise
    bool IsCompletionSignaled() const
    {        
        return completion_signaled.load();
    }   

    /// @brief Signal completion    
    void SetCompletionSignaled() const
    {
        completion_signaled.store(true);
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

    /**
     * @brief Reports whether Is GPU Bootstrap Enabled is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    bool IsGpuBootstrapEnabled() const {
        return gpu_bootstrap_enabled_;
    }

    /**
     * @brief Updates the GPU Bootstrap Enabled.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param enabled Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    void SetGpuBootstrapEnabled(bool enabled) {
        gpu_bootstrap_enabled_ = enabled;
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
    // Primary node creation/query contract used by graph orchestration.
    std::shared_ptr<graph::INodeProvider> node_provider {nullptr};
    std::shared_ptr<graph::GraphManager> graph_manager {nullptr};
    std::vector<std::string> node_names;
    std::vector<std::string> edge_descriptions;
    mutable std::atomic<bool> is_stopped{false};
    mutable std::atomic<bool> completion_signaled{false};
    bool cli_mode_{false};
    bool gpu_bootstrap_enabled_{true};
    graph::CapabilityBus capability_bus;
};

}  // namespace capabilities
