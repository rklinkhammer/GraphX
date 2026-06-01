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

#include "graph/NodeFactoryRegistry.hpp"

#include <algorithm>
#include <format>

#include "log4cxx/logger.h"

namespace graph::config {

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("graph.config.NodeFactoryRegistry"));

std::expected<void, NodeFactoryRegistry::RegistryError>
NodeFactoryRegistry::RegisterExpected(
    const std::string& type_name,
    NodeFactoryFunction factory) noexcept {
    
    if (type_name.empty()) {
        LOG4CXX_ERROR(logger, "Cannot register factory with empty type name");
        return std::unexpected(RegistryError::EmptyTypeName);
    }
    
    if (!factory) {
        LOG4CXX_ERROR(logger, "Factory function is null for type: " << type_name);
        return std::unexpected(RegistryError::NullFactory);
    }
    
    // Check if already registered
    bool already_registered = factories_.count(type_name) > 0;
    
    // Register or replace
    factories_[type_name] = factory;
    
    if (already_registered) {
        LOG4CXX_WARN(logger, "Replacing existing factory for type: " << type_name);
    } else {
        LOG4CXX_TRACE(logger, "Registered factory for node type: " << type_name);
    }

    return {};
}

std::expected<NodeFacadeAdapter, NodeFactoryRegistry::RegistryError>
NodeFactoryRegistry::CreateExpected(const std::string& type_name) noexcept {
    auto it = factories_.find(type_name);
    
    if (it == factories_.end()) {
        std::string error_msg = std::format("Node type not registered: {}", type_name);
        LOG4CXX_ERROR(logger, error_msg);
        return std::unexpected(RegistryError::TypeNotRegistered);
    }
    
    try {
        LOG4CXX_TRACE(logger, "Creating node of type: " << type_name);
        auto adapter = it->second();  // Call factory function
        LOG4CXX_TRACE(logger, "Successfully created node of type: " << type_name);
        return adapter;
    } catch (const std::exception& e) {
        std::string error_msg = std::format("Failed to create node of type '{}': {}",
                                           type_name, e.what());
        LOG4CXX_ERROR(logger, error_msg);
        return std::unexpected(RegistryError::FactoryFailed);
    } catch (...) {
        std::string error_msg = std::format("Failed to create node of type '{}': unknown exception",
                                           type_name);
        LOG4CXX_ERROR(logger, error_msg);
        return std::unexpected(RegistryError::Unknown);
    }
}

bool NodeFactoryRegistry::IsAvailable(const std::string& type_name) const {
    return factories_.count(type_name) > 0;
}

std::vector<std::string> NodeFactoryRegistry::GetAvailableTypes() const {
    std::vector<std::string> types;
    types.reserve(factories_.size());
    
    for (const auto& pair : factories_) {
        types.push_back(pair.first);
    }
    
    // Sort for deterministic ordering
    std::sort(types.begin(), types.end());
    
    return types;
}

size_t NodeFactoryRegistry::GetRegistrySize() const {
    return factories_.size();
}

void NodeFactoryRegistry::Clear() {
    factories_.clear();
    LOG4CXX_TRACE(logger, "Cleared all registered factories from registry");
}

}  // namespace graph::config
