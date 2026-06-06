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
#include <algorithm>
#include <log4cxx/logger.h>

namespace graph {

log4cxx::LoggerPtr NodeFactory::logger_ = 
    log4cxx::Logger::getLogger("graph.NodeFactory");

std::string ErrorMessage(NodeFactory::NodeCreationError error) {
    using Error = NodeFactory::NodeCreationError;
    switch (error) {
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

bool NodeFactory::IsNodeTypeAvailable(const std::string& node_type_name) const {
    LOG4CXX_TRACE(logger_, "Checking availability of node type: " << node_type_name);

    if (node_factories_.contains(node_type_name)) {
        return true;
    }

    return plugin_registry_ && plugin_registry_->HasNodeType(node_type_name);
}

std::vector<std::string> NodeFactory::GetAvailableNodeTypes() const {
    LOG4CXX_TRACE(logger_, "Getting list of available node types");
    
    std::vector<std::string> available_types;

    available_types.reserve(node_factories_.size());
    for (const auto& [type_name, factory] : node_factories_) {
        (void)factory;
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

void NodeFactory::Initialize() {
    LOG4CXX_TRACE(logger_, "Initializing unified factory");
    
    if (initialized_) {
        LOG4CXX_WARN(logger_, "Factory already initialized");
        return;
    }
    
    try {
        node_factories_.clear();

        if (plugin_registry_) {
            LOG4CXX_TRACE(logger_, "Registering plugin nodes from registry");
            RegisterPluginNodes();
        } else {
            LOG4CXX_TRACE(logger_, "No plugin registry configured, skipping plugin registration");
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
            LOG4CXX_ERROR(logger_, "Failed to lazily initialize factory: " << e.what());
            return std::unexpected(NodeCreationError::NotInitialized);
        }
    }

    // Primary creation path after successful initialization.
    if (initialized_) {
        auto factory = node_factories_.find(node_type_name);
        if (factory == node_factories_.end()) {
            LOG4CXX_ERROR(logger_, "Node type not registered: " << node_type_name);
            return std::unexpected(NodeCreationError::TypeNotFound);
        }

        LOG4CXX_TRACE(logger_, "Creating node: " << node_type_name);
        try {
            return factory->second();
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Factory failed to create node " << node_type_name << ": " << e.what());
            return std::unexpected(NodeCreationError::CreationFailed);
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Factory failed to create node " << node_type_name << ": unknown error");
            return std::unexpected(NodeCreationError::Unknown);
        }
    }

    LOG4CXX_ERROR(logger_, "Unified factory is not initialized");
    return std::unexpected(NodeCreationError::NotInitialized);
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
            if (type_name.empty()) {
                throw std::runtime_error("Plugin registry returned an empty type name");
            }

            const bool replacing_existing = node_factories_.contains(type_name);
            node_factories_[type_name] = [registry = plugin_registry_, type_name]() {
                auto created = registry->CreateNodeExpected(type_name);
                if (!created) {
                    throw std::runtime_error("PluginRegistry creation failed for type: " + type_name);
                }

                auto [handle, facade] = *created;
                return NodeFacadeAdapter(handle, facade);
            };

            if (replacing_existing) {
                LOG4CXX_WARN(logger_, "Replacing existing node factory for type: " << type_name);
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

void NodeFactory::RegisterStaticNodes() {
    LOG4CXX_TRACE(logger_, "Registering static node factories");

    // Note: Static node registration is deferred.
    // Today Layer 5 nodes are expected to be supplied through plugin
    // registration and inserted into the provider map in RegisterPluginNodes().
    // This is acceptable as long as the plugin system provides these nodes.
    //
    // If direct static node registration is needed (without plugin dependency),
    // the pattern would be to create factory lambdas like:
    //
    //   node_factories_["FlightFSMNode"] = []() {
    //       auto node = std::make_shared<avionics::FlightFSMNode>();
    //       return config::StaticNodeAdapter::Adapt(node, "FlightFSMNode");
    //   };
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

    node_factories_.clear();
    initialized_ = false;
    return {};
}

}  // namespace graph
