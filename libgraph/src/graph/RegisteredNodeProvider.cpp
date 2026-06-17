/**
 * @file RegisteredNodeProvider.cpp
 * @brief GraphX source file.
 */

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

#include "graph/RegisteredNodeProvider.hpp"
#include "plugins/PluginRegistry.hpp"
#include "graph/StaticNodeAdapter.hpp"
#include <algorithm>
#include <log4cxx/logger.h>

namespace graph {

log4cxx::LoggerPtr RegisteredNodeProvider::logger_ = 
    log4cxx::Logger::getLogger("graph.RegisteredNodeProvider");

/**
 * @brief Error message.
 * @param error Parameter for error message.
 */
std::string ErrorMessage(RegisteredNodeProvider::NodeCreationError error) {
    using Error = RegisteredNodeProvider::NodeCreationError;
    switch (error) {
        case Error::TypeNotFound:
            return "Node type not found";
        case Error::NotInitialized:
            return "RegisteredNodeProvider not initialized";
        case Error::CreationFailed:
            return "Node creation failed";
        case Error::InvalidArgument:
            return "Invalid node provider argument";
        case Error::Unknown:
            return "Unknown node creation error";
        default:
            return "Unrecognized node creation error";
    }
}

/**
 * @brief Is node type available.
 * @param node_type_name Parameter for is node type available.
 */
bool RegisteredNodeProvider::IsNodeTypeAvailable(const std::string& node_type_name) const {
    LOG4CXX_TRACE(logger_, "Checking availability of node type: " << node_type_name);

    if (node_creators_.contains(node_type_name)) {
        return true;
    }

    return plugin_registry_ && plugin_registry_->HasNodeType(node_type_name);
}

/**
 * @brief Get available node types.
 */
std::vector<std::string> RegisteredNodeProvider::GetAvailableNodeTypes() const {
    LOG4CXX_TRACE(logger_, "Getting list of available node types");
    
    std::vector<std::string> available_types;

    available_types.reserve(node_creators_.size());
    for (const auto& [type_name, creator] : node_creators_) {
        (void)creator;
        available_types.push_back(type_name);
    }

    if (!initialized_ && plugin_registry_) {
        auto plugin_types = plugin_registry_->GetRegisteredNodeTypes();
        available_types.insert(
            available_types.end(),
            plugin_types.begin(),
            plugin_types.end());
    }

    std::sort(available_types.begin(), available_types.end());
    available_types.erase(
        std::unique(available_types.begin(), available_types.end()),
        available_types.end());

    LOG4CXX_TRACE(logger_, "Found " << available_types.size() << " available node types");
    return available_types;
}

/**
 * @brief Initialize.
 */
void RegisteredNodeProvider::Initialize() {
    LOG4CXX_TRACE(logger_, "Initializing unified provider");
    
    if (initialized_) {
        LOG4CXX_WARN(logger_, "Provider already initialized");
        return;
    }
    
    try {
        node_creators_.clear();

        if (plugin_registry_) {
            LOG4CXX_TRACE(logger_, "Registering plugin nodes from registry");
            RegisterPluginNodes();
        } else {
            LOG4CXX_TRACE(logger_, "No plugin registry configured, skipping plugin registration");
        }
        
        RegisterStaticNodes();
        initialized_ = true;
        LOG4CXX_TRACE(logger_, "Unified provider initialized successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to initialize unified provider: " << e.what());
        throw std::runtime_error("Failed to initialize unified provider");
    }
}

std::expected<NodeFacadeAdapter, RegisteredNodeProvider::NodeCreationError>
RegisteredNodeProvider::CreateNodeExpected(const std::string& node_type_name) noexcept {
    LOG4CXX_TRACE(logger_, "CreateNode (unified) requested for: " << node_type_name);

    if (node_type_name.empty()) {
        LOG4CXX_ERROR(logger_, "Cannot create node with empty type name");
        return std::unexpected(NodeCreationError::InvalidArgument);
    }
    
    // Lazy initialization: if not yet initialized, do so now
    if (!initialized_) {
        LOG4CXX_TRACE(logger_, "Unified provider not yet initialized, initializing now");
        try {
            Initialize();
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to lazily initialize provider: " << e.what());
            return std::unexpected(NodeCreationError::NotInitialized);
        }
    }

    // Primary creation path after successful initialization.
    if (initialized_) {
        auto creator = node_creators_.find(node_type_name);
        if (creator == node_creators_.end()) {
            LOG4CXX_ERROR(logger_, "Node type not registered: " << node_type_name);
            return std::unexpected(NodeCreationError::TypeNotFound);
        }

        LOG4CXX_TRACE(logger_, "Creating node: " << node_type_name);
        try {
            return creator->second();
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Provider failed to create node " << node_type_name << ": " << e.what());
            return std::unexpected(NodeCreationError::CreationFailed);
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Provider failed to create node " << node_type_name << ": unknown error");
            return std::unexpected(NodeCreationError::Unknown);
        }
    }

    LOG4CXX_ERROR(logger_, "Unified provider is not initialized");
    return std::unexpected(NodeCreationError::NotInitialized);
}

/**
 * @brief Register plugin nodes.
 */
void RegisteredNodeProvider::RegisterPluginNodes() {
    LOG4CXX_TRACE(logger_, "Registering plugin nodes in unified provider");
    
    if (!plugin_registry_) {
        LOG4CXX_WARN(logger_, "PluginRegistry not set - skipping plugin node registration");
        return;
    }
    
    // Get list of all plugin node types from the registry
    // The registry contains all types from all loaders
    auto plugin_types = plugin_registry_->GetRegisteredNodeTypes();
    LOG4CXX_TRACE(logger_, "Registering " << plugin_types.size() 
                 << " plugin node types");
    
    // For each plugin type, register a node creator
    for (const auto& type_name : plugin_types) {
        try {
            if (type_name.empty()) {
                throw std::runtime_error("Plugin registry returned an empty type name");
            }

            const bool replacing_existing = node_creators_.contains(type_name);
            node_creators_[type_name] = [registry = plugin_registry_, type_name]() {
                auto created = registry->CreateNodeExpected(type_name);
                if (!created) {
                    throw std::runtime_error("PluginRegistry creation failed for type: " + type_name);
                }

                auto [handle, facade] = *created;
                return NodeFacadeAdapter(handle, facade);
            };

            if (replacing_existing) {
                LOG4CXX_WARN(logger_, "Replacing existing node creator for type: " << type_name);
            } else {
                LOG4CXX_TRACE(logger_, "Registered plugin node type: " << type_name);
            }
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Failed to register plugin node type '" 
                << type_name << "': " << e.what());
            throw;
        }
    }
    
    LOG4CXX_TRACE(logger_, "Successfully registered " << plugin_types.size() 
        << " plugin node types");
}

/**
 * @brief Register static nodes.
 */
void RegisteredNodeProvider::RegisterStaticNodes() {
    LOG4CXX_TRACE(logger_, "Registering static node creators");

    // Note: Static node registration is deferred.
    // Today Layer 5 nodes are expected to be supplied through plugin
    // registration and inserted into the creator map in RegisterPluginNodes().
    // This is acceptable as long as the plugin system provides these nodes.
    //
    // If direct static node registration is needed (without plugin dependency),
    // the pattern would be to create node creator lambdas like:
    //
    //   node_creators_["FlightFSMNode"] = []() {
    //       auto node = std::make_shared<avionics::FlightFSMNode>();
    //       return config::StaticNodeAdapter::Adapt(node, "FlightFSMNode");
    //   };
    //
    // However, this requires careful lambda type handling due to
    // NodeFacadeAdapter being a move-only type.

    LOG4CXX_TRACE(logger_, "Finished registering static nodes");
}

}  // namespace graph
