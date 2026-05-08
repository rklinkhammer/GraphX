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

#include "app/FactoryManager.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include "graph/NodeFactory.hpp"
#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <filesystem>

namespace app {

// ============================================================================
// Static Initialization
// ============================================================================

static log4cxx::LoggerPtr logger_ = log4cxx::Logger::getLogger("FactoryManager");

// ============================================================================
// FactoryManager::CreateFactory()
// ============================================================================

std::pair<std::shared_ptr<graph::NodeFactory>, 
          std::shared_ptr<graph::PluginLoader>>
FactoryManager::CreateFactory(const std::string& plugin_directory) {
    LOG4CXX_TRACE(logger_, "Creating NodeFactory with plugin directory: " 
                          << plugin_directory);
    
    // Step 1: Validate plugin directory exists
    if (!std::filesystem::exists(plugin_directory)) {
        LOG4CXX_WARN(logger_, "Plugin directory does not exist: " 
                            << plugin_directory << ". Proceeding with empty registry.");
    } else if (!std::filesystem::is_directory(plugin_directory)) {
        LOG4CXX_ERROR(logger_, "Plugin path exists but is not a directory: " 
                             << plugin_directory);
        return {nullptr, nullptr};
    }
    
    // Step 2: Create PluginRegistry
    std::shared_ptr<graph::PluginRegistry> registry;
    try {
        registry = std::make_shared<graph::PluginRegistry>();
        LOG4CXX_TRACE(logger_, "Created PluginRegistry successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create PluginRegistry: " << e.what());
        return {nullptr, nullptr};
    }
    
    // Step 3: Create PluginLoader with registry
    // CRITICAL: Must return this loader to caller to prevent dlclose() segfaults
    std::shared_ptr<graph::PluginLoader> loader;
    try {
        loader = std::make_shared<graph::PluginLoader>(plugin_directory, registry);
        LOG4CXX_TRACE(logger_, "Created PluginLoader successfully");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create PluginLoader: " << e.what());
        return {nullptr, nullptr};
    }
    
    // Step 4: Load all plugins from directory (graceful degradation on individual failures)
    if (std::filesystem::exists(plugin_directory)) {
        try {
            LOG4CXX_TRACE(logger_, "Loading plugins from directory: " << plugin_directory);
            loader->LoadAllPlugins();
            LOG4CXX_TRACE(logger_, "Plugin loading completed");
        } catch (const std::exception& e) {
            // Log but don't fail - some plugins may have loaded successfully
            LOG4CXX_WARN(logger_, "Plugin loading encountered errors: " << e.what()
                                  << ". Some plugins may not be available.");
        }
    } else {
        LOG4CXX_WARN(logger_, "Plugin directory does not exist, skipping plugin loading: "
                            << plugin_directory);
    }
    
    // Step 5: Create NodeFactory with loaded registry
    std::shared_ptr<graph::NodeFactory> factory;
    try {
        factory = std::make_shared<graph::NodeFactory>(registry);
        LOG4CXX_TRACE(logger_, "Created NodeFactory successfully");
        
        // Initialize the factory with plugin node types
        factory->Initialize();
        LOG4CXX_TRACE(logger_, "Initialized NodeFactory with plugins");
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to create or initialize NodeFactory: " 
                             << e.what());
        return {nullptr, nullptr};
    }
    
    LOG4CXX_TRACE(logger_, "FactoryManager::CreateFactory completed successfully. "
                         << "Returning (factory, loader) pair. "
                         << "CRITICAL: Caller must keep loader alive!");
    
    return {factory, loader};
}

// ============================================================================
// FactoryManager::GetAvailableNodeTypes()
// ============================================================================

std::vector<std::string> FactoryManager::GetAvailableNodeTypes(
    const std::shared_ptr<graph::NodeFactory>& factory) {
    
    if (!factory) {
        LOG4CXX_ERROR(logger_, "GetAvailableNodeTypes called with null factory");
        throw std::runtime_error("NodeFactory cannot be null");
    }
    
    LOG4CXX_TRACE(logger_, "Querying available node types from factory");
    
    try {
        auto node_types = factory->GetAvailableNodeTypes();
        LOG4CXX_TRACE(logger_, "Found " << node_types.size() 
                             << " available node types");
        
        if (!node_types.empty()) {
            std::string types_str;
            for (size_t i = 0; i < node_types.size(); ++i) {
                if (i > 0) types_str += ", ";
                types_str += node_types[i];
            }
            LOG4CXX_TRACE(logger_, "Available types: " << types_str);
        }
        
        return node_types;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to get available node types: " << e.what());
        throw std::runtime_error(
            std::string("Failed to query available node types: ") + e.what());
    }
}

// ============================================================================
// FactoryManager::IsNodeTypeAvailable()
// ============================================================================

bool FactoryManager::IsNodeTypeAvailable(
    const std::shared_ptr<graph::NodeFactory>& factory,
    const std::string& type_name) {
    
    if (!factory) {
        LOG4CXX_ERROR(logger_, "IsNodeTypeAvailable called with null factory");
        throw std::runtime_error("NodeFactory cannot be null");
    }
    
    if (type_name.empty()) {
        LOG4CXX_WARN(logger_, "IsNodeTypeAvailable called with empty type_name");
        return false;
    }
    
    LOG4CXX_TRACE(logger_, "Checking availability of node type: " << type_name);
    
    try {
        bool available = factory->IsNodeTypeAvailable(type_name);
        
        if (available) {
            LOG4CXX_TRACE(logger_, "Node type '" << type_name << "' is available");
        } else {
            LOG4CXX_TRACE(logger_, "Node type '" << type_name << "' is not available");
        }
        
        return available;
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to check node type availability: " 
                             << e.what());
        throw std::runtime_error(
            std::string("Failed to check node type '") + type_name + "': " + e.what());
    }
}

}  // namespace app
