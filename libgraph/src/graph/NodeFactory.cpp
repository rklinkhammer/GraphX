// MIT License
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

#include "graph/NodeFactory.hpp"
#include "plugins/PluginRegistry.hpp"
#include "graph/StaticNodeAdapter.hpp"
#include <log4cxx/logger.h>

namespace graph {

log4cxx::LoggerPtr NodeFactory::logger_ = 
    log4cxx::Logger::getLogger("graph.NodeFactory");

NodeFacadeAdapter NodeFactory::CreateDynamicNode(const std::string& node_type_name) {
    LOG4CXX_TRACE(logger_, "CreateDynamicNode requested for: " << node_type_name);
    
    if (!plugin_registry_) {
        LOG4CXX_ERROR(logger_, "PluginRegistry not set - cannot create dynamic node");
        throw std::runtime_error("PluginRegistry not initialized");
    }
    
    LOG4CXX_TRACE(logger_, "plugin_registry_ is valid, calling CreateNode");
    
    try {
        auto [handle, facade] = plugin_registry_->CreateNode(node_type_name);
        LOG4CXX_TRACE(logger_, "Successfully created dynamic node: " << node_type_name);
        return NodeFacadeAdapter(handle, facade);
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create dynamic node: " << e.what());
        throw;
    }
}

bool NodeFactory::IsNodeTypeAvailable(const std::string& node_type_name) const {
    LOG4CXX_TRACE(logger_, "Checking availability of node type: " << node_type_name);
    
    if (!plugin_registry_) {
        LOG4CXX_TRACE(logger_, "PluginRegistry not set");
        return false;
    }
    
    bool available = plugin_registry_->HasNodeType(node_type_name);
    LOG4CXX_TRACE(logger_, "Node type " << node_type_name 
                  << " available: " << (available ? "YES" : "NO"));
    return available;
}

std::vector<std::string> NodeFactory::GetAvailableNodeTypes() const {
    LOG4CXX_TRACE(logger_, "Getting list of available node types");
    
    std::vector<std::string> available_types;
    
    if (!plugin_registry_) {
        LOG4CXX_TRACE(logger_, "PluginRegistry not set");
        return available_types;
    }
    
    available_types = plugin_registry_->GetRegisteredNodeTypes();
    LOG4CXX_TRACE(logger_, "Found " << available_types.size() << " available node types");
    
    return available_types;
}

void NodeFactory::Initialize() {
    LOG4CXX_TRACE(logger_, "Initializing unified factory");
    
    if (initialized_) {
        LOG4CXX_WARN(logger_, "Factory already initialized");
        return;
    }
    
    try {
        RegisterPluginNodes();
        RegisterStaticNodes();
        initialized_ = true;
        LOG4CXX_TRACE(logger_, "Unified factory initialized successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to initialize unified factory: " << e.what());
        throw std::runtime_error("Failed to initialize unified factory");
    }
}

NodeFacadeAdapter NodeFactory::CreateNode(const std::string& node_type_name) {
    LOG4CXX_TRACE(logger_, "CreateNode (unified) requested for: " << node_type_name);
    
    // Lazy initialization: if not yet initialized, do so now
    if (!initialized_) {
        LOG4CXX_TRACE(logger_, "Unified factory not yet initialized, initializing now");
        try {
            Initialize();
        } catch (const std::exception& e) {
            LOG4CXX_WARN(logger_, "Failed to lazily initialize factory, will attempt CreateDynamicNode: " << e.what());
            // Fall through to try CreateDynamicNode
        }
    }
    
    // First try the unified registry
    if (initialized_ && unified_registry_->IsAvailable(node_type_name)) {
        try {
            LOG4CXX_TRACE(logger_, "Creating node via unified registry: " << node_type_name);
            NodeFacadeAdapter adapter = unified_registry_->Create(node_type_name);
            LOG4CXX_TRACE(logger_, "Successfully created node via unified registry: " << node_type_name);
            return adapter;
        } catch (const std::exception& e) {
            LOG4CXX_WARN(logger_, "Unified registry failed, trying CreateDynamicNode: " << e.what());
            // Fall through to CreateDynamicNode
        }
    }
    
    // Fallback to CreateDynamicNode for backward compatibility
    LOG4CXX_TRACE(logger_, "Falling back to CreateDynamicNode for: " << node_type_name);
    try {
        NodeFacadeAdapter adapter = CreateDynamicNode(node_type_name);
        LOG4CXX_TRACE(logger_, "Successfully created node via CreateDynamicNode: " << node_type_name);
        return adapter;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create node via both paths: " << e.what());
        throw std::runtime_error("Failed to create node '" + node_type_name + "': " + e.what());
    }
}

void NodeFactory::RegisterPluginNodes() {
    LOG4CXX_TRACE(logger_, "Registering plugin nodes in unified factory");
    
    if (!plugin_registry_) {
        LOG4CXX_WARN(logger_, "PluginRegistry not set - skipping plugin node registration");
        return;
    }
    
    // Get list of all plugin node types
    auto plugin_types = plugin_registry_->GetRegisteredNodeTypes();
    LOG4CXX_TRACE(logger_, "Registering " << plugin_types.size() << " plugin node types");
    
    // For each plugin type, register a factory function
    for (const auto& type_name : plugin_types) {
        try {
            // Create a lambda that captures the type name and logger
            unified_registry_->Register(
                type_name,
                [this, type_name]() {
                    return this->CreateDynamicNode(type_name);
                }
            );
            LOG4CXX_TRACE(logger_, "Registered plugin node type: " << type_name);
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to register plugin node type '" 
                << type_name << "': " << e.what());
            throw;
        }
    }
    
    LOG4CXX_TRACE(logger_, "Successfully registered " << plugin_types.size() 
        << " plugin node types");
}

void NodeFactory::RegisterStaticNodes() {
    LOG4CXX_TRACE(logger_, "Registering static nodes in unified factory");

    // Note: Static node registration is deferred.
    // All Layer 5 nodes will be available through the unified factory via
    // the CreateNode(string) fallback to CreateDynamicNode().
    // This is acceptable as long as the plugin system provides these nodes.
    //
    // If direct static node registration is needed (without plugin dependency),
    // the pattern would be to create factory lambdas like:
    //
    //   unified_registry_->Register("FlightFSMNode", [this]() {
    //       auto node = std::make_shared<avionics::FlightFSMNode>();
    //       return config::StaticNodeAdapter::Adapt(node, "FlightFSMNode");
    //   });
    //
    // However, this requires careful lambda type handling due to
    // NodeFacadeAdapter being a move-only type.

    LOG4CXX_TRACE(logger_, "Finished registering static nodes");
}

}  // namespace graph
