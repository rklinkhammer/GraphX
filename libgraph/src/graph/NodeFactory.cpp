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
#include "plugins/PluginLoader.hpp"
#include "graph/StaticNodeAdapter.hpp"
#include <log4cxx/logger.h>

namespace graph {

log4cxx::LoggerPtr NodeFactory::logger_ = 
    log4cxx::Logger::getLogger("graph.NodeFactory");

std::string ErrorMessage(NodeFactory::NodeCreationError error) {
    using Error = NodeFactory::NodeCreationError;
    switch (error) {
        case Error::PluginRegistryMissing:
            return "PluginRegistry not initialized";
        case Error::TypeNotFound:
            return "Node type not found";
        case Error::NotInitialized:
            return "NodeFactory not initialized";
        case Error::CreationFailed:
            return "Node creation failed";
        case Error::InvalidArgument:
            return "Invalid node factory argument";
        case Error::Unknown:
            return "Unknown node creation error";
        default:
            return "Unrecognized node creation error";
    }
}

std::expected<NodeFacadeAdapter, NodeFactory::NodeCreationError>
NodeFactory::CreateDynamicNodeExpected(const std::string& node_type_name) noexcept {
    LOG4CXX_TRACE(logger_, "CreateDynamicNode requested for: " << node_type_name);

    if (node_type_name.empty()) {
        LOG4CXX_ERROR(logger_, "Cannot create dynamic node with empty type name");
        return std::unexpected(NodeCreationError::InvalidArgument);
    }
    
    if (!plugin_registry_) {
        LOG4CXX_ERROR(logger_, "PluginRegistry not set - cannot create dynamic node");
        return std::unexpected(NodeCreationError::PluginRegistryMissing);
    }

    if (!plugin_registry_->HasNodeType(node_type_name)) {
        LOG4CXX_ERROR(logger_, "Dynamic node type not found: " << node_type_name);
        return std::unexpected(NodeCreationError::TypeNotFound);
    }
    
    LOG4CXX_TRACE(logger_, "plugin_registry_ is valid, calling CreateNodeExpected");

    auto created = plugin_registry_->CreateNodeExpected(node_type_name);
    if (!created) {
        if (created.error() == PluginRegistry::PluginRegistryError::TypeNotRegistered) {
            LOG4CXX_ERROR(logger_, "Dynamic node type not found: " << node_type_name);
            return std::unexpected(NodeCreationError::TypeNotFound);
        }
        if (created.error() == PluginRegistry::PluginRegistryError::Unknown) {
            LOG4CXX_ERROR(logger_, "Failed to create dynamic node: unknown error");
            return std::unexpected(NodeCreationError::Unknown);
        }
        LOG4CXX_ERROR(logger_, "Failed to create dynamic node");
        return std::unexpected(NodeCreationError::CreationFailed);
    }

    auto [handle, facade] = *created;
    LOG4CXX_TRACE(logger_, "Successfully created dynamic node: " << node_type_name);
    return NodeFacadeAdapter(handle, facade);
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
        // If we have loaders, register their plugins
        if (!loaders_.empty()) {
            LOG4CXX_TRACE(logger_, "Registering plugins from " << loaders_.size() 
                         << " loaders");
            RegisterPluginNodes();
        } else {
            LOG4CXX_TRACE(logger_, "No plugin loaders configured, skipping plugin registration");
        }
        
        RegisterStaticNodes();
        initialized_ = true;
        LOG4CXX_TRACE(logger_, "Unified factory initialized successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to initialize unified factory: " << e.what());
        throw std::runtime_error("Failed to initialize unified factory");
    }
}

std::expected<NodeFacadeAdapter, NodeFactory::NodeCreationError>
NodeFactory::CreateNodeExpected(const std::string& node_type_name) noexcept {
    LOG4CXX_TRACE(logger_, "CreateNode (unified) requested for: " << node_type_name);

    if (node_type_name.empty()) {
        LOG4CXX_ERROR(logger_, "Cannot create node with empty type name");
        return std::unexpected(NodeCreationError::InvalidArgument);
    }
    
    // Lazy initialization: if not yet initialized, do so now
    if (!initialized_) {
        LOG4CXX_TRACE(logger_, "Unified factory not yet initialized, initializing now");
        try {
            Initialize();
        } catch (const std::exception& e) {
            LOG4CXX_WARN(logger_, "Failed to lazily initialize factory, will attempt CreateDynamicNode: " << e.what());
        }
    }
    
    // First try the unified registry
    if (initialized_ && unified_registry_->IsAvailable(node_type_name)) {
        LOG4CXX_TRACE(logger_, "Creating node via unified registry: " << node_type_name);
        auto adapter = unified_registry_->CreateExpected(node_type_name);
        if (adapter) {
            LOG4CXX_TRACE(logger_, "Successfully created node via unified registry: " << node_type_name);
            return std::move(adapter).value();
        }

        LOG4CXX_WARN(logger_, "Unified registry failed, trying CreateDynamicNode");
        // Fall through to CreateDynamicNode.
    }
    
    // Fall back to CreateDynamicNode when the unified registry cannot serve the request.
    LOG4CXX_TRACE(logger_, "Falling back to CreateDynamicNode for: " << node_type_name);
    auto adapter = CreateDynamicNodeExpected(node_type_name);
    if (!adapter) {
        LOG4CXX_ERROR(logger_, "Failed to create node via both paths: "
                      << ErrorMessage(adapter.error()));
        return std::unexpected(adapter.error());
    }

    LOG4CXX_TRACE(logger_, "Successfully created node via CreateDynamicNode: " << node_type_name);
    return std::move(adapter).value();
}

void NodeFactory::RegisterPluginNodes() {
    LOG4CXX_TRACE(logger_, "Registering plugin nodes in unified factory");
    
    if (!plugin_registry_) {
        LOG4CXX_WARN(logger_, "PluginRegistry not set - skipping plugin node registration");
        return;
    }
    
    // Get list of all plugin node types from the registry
    // The registry contains all types from all loaders
    auto plugin_types = plugin_registry_->GetRegisteredNodeTypes();
    LOG4CXX_TRACE(logger_, "Registering " << plugin_types.size() 
                 << " plugin node types from " << loaders_.size() << " loaders");
    
    // For each plugin type, register a factory function
    for (const auto& type_name : plugin_types) {
        try {
            // Create a lambda that captures the type name and logger
            auto registration = unified_registry_->RegisterExpected(
                type_name,
                [this, type_name]() {
                    auto node = this->CreateDynamicNodeExpected(type_name);
                    if (!node) {
                        throw std::runtime_error(ErrorMessage(node.error()));
                    }
                    return std::move(node).value();
                }
            );
            if (!registration) {
                throw std::runtime_error("Failed to register plugin node type");
            }
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

std::expected<void, NodeFactory::PluginDirectoryError>
NodeFactory::AddPluginDirectoryExpected(const std::string& directory_path) noexcept {
    LOG4CXX_TRACE(logger_, "Adding plugin directory: " << directory_path);
    
    if (directory_path.empty()) {
        LOG4CXX_ERROR(logger_, "Cannot add empty directory path");
        return std::unexpected(PluginDirectoryError::EmptyPath);
    }
    
    // First directory becomes the primary registry/loader if not already set
    if (!plugin_registry_) {
        LOG4CXX_TRACE(logger_, "Creating default PluginRegistry for first directory");
        try {
            plugin_registry_ = std::make_shared<PluginRegistry>();
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Failed to create default PluginRegistry");
            return std::unexpected(PluginDirectoryError::RegistryCreationFailed);
        }
    }
    
    // Create a new loader for this directory
    try {
        loaders_.push_back(std::make_shared<PluginLoader>(directory_path, plugin_registry_));
    } catch (...) {
        LOG4CXX_ERROR(logger_, "Failed to create PluginLoader for directory: " << directory_path);
        return std::unexpected(PluginDirectoryError::LoaderCreationFailed);
    }
    
    LOG4CXX_TRACE(logger_, "Added plugin directory (total directories: " 
                 << loaders_.size() << ")");
    return {};
}

std::expected<void, NodeFactory::PluginDirectoryError>
NodeFactory::LoadAllPluginsFromDirectoriesExpected() noexcept {
    LOG4CXX_TRACE(logger_, "Loading plugins from " << loaders_.size() 
                 << " registered directories");
    
    if (loaders_.empty()) {
        LOG4CXX_ERROR(logger_, "No plugin directories have been added");
        return std::unexpected(PluginDirectoryError::NoDirectoriesRegistered);
    }
    
    if (!plugin_registry_) {
        LOG4CXX_ERROR(logger_, "PluginRegistry not initialized");
        return std::unexpected(PluginDirectoryError::PluginRegistryMissing);
    }
    
    size_t total_loaded = 0;
    size_t total_failed = 0;
    
    for (size_t i = 0; i < loaders_.size(); ++i) {
        auto& loader = loaders_[i];
        LOG4CXX_TRACE(logger_, "Loading plugins from directory " << (i + 1) 
                     << " of " << loaders_.size());
        
        auto loaded = loader->LoadAllPluginsSafe();
        if (!loaded) {
            LOG4CXX_WARN(logger_, "Failed to load plugins from directory " << (i + 1));
            total_failed++;
            continue;
        }

        total_loaded += *loaded;
        
        LOG4CXX_TRACE(logger_, "Directory " << (i + 1) << " loaded " 
                     << *loaded << " plugin files");
    }
    
    LOG4CXX_TRACE(logger_, "Plugin loading complete: " << total_loaded 
                 << " loaded, " << total_failed << " directories failed");
    return {};
}

}  // namespace graph
