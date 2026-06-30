/**
 * @file RegisteredNodeProvider.cpp
 * @brief Registered Node Provider Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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
        case Error::Unsupported:
            return "Node operation is unsupported";
        case Error::BackendUnavailable:
            return "Node backend is unavailable";
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

    return plugin_registry_ && plugin_registry_->HasNodeType(node_type_name);
}

/**
 * @brief Get available node types.
 */
std::vector<std::string> RegisteredNodeProvider::GetAvailableNodeTypes() const {
    LOG4CXX_TRACE(logger_, "Getting list of available node types");
    
    std::vector<std::string> available_types;

    if (plugin_registry_) {
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
        if (plugin_registry_) {
            LOG4CXX_TRACE(logger_, "Using authoritative plugin registry");
        } else {
            LOG4CXX_TRACE(logger_, "No plugin registry configured, skipping plugin registration");
        }
        
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

    if (initialized_ && plugin_registry_) {
        auto created = plugin_registry_->CreateNodeExpected(node_type_name);
        if (!created) {
            return std::unexpected(
                created.error() == PluginRegistry::PluginRegistryError::TypeNotRegistered
                    ? NodeCreationError::TypeNotFound
                    : NodeCreationError::CreationFailed);
        }
        auto [handle, facade] = *created;
        return NodeFacadeAdapter(handle, facade);
    }

    LOG4CXX_ERROR(logger_, "Unified provider is not initialized");
    return std::unexpected(NodeCreationError::NotInitialized);
}

}  // namespace graph
