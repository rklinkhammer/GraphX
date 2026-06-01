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

#include "plugins/PluginRegistry.hpp"
#include <dlfcn.h>
#include <log4cxx/logger.h>

namespace graph {

log4cxx::LoggerPtr PluginRegistry::logger_ =
    log4cxx::Logger::getLogger("graph.PluginRegistry");

PluginRegistry::PluginRegistry() {
    LOG4CXX_TRACE(logger_, "PluginRegistry initialized");
}

std::expected<void, PluginRegistry::PluginRegistryError>
PluginRegistry::RegisterNodeTypeExpected(
    const std::string& type_name,
    const std::string& description,
    const std::string& plugin_path,
    const std::string& create_function,
    const std::string& abi_tag,
    const std::string& version,
    void* plugin_handle,
    const NodeFacade* facade) noexcept {
    
    LOG4CXX_TRACE(logger_, "Registering node type: " << type_name 
                 << " from plugin: " << plugin_path);
    
    if (!plugin_handle) {
        LOG4CXX_ERROR(logger_, "Invalid plugin handle for type: " << type_name);
        return std::unexpected(PluginRegistryError::InvalidPluginHandle);
    }
    
    if (!facade) {
        LOG4CXX_ERROR(logger_, "Null facade pointer for type: " << type_name);
        return std::unexpected(PluginRegistryError::NullFacade);
    }
    
    // Verify the create function can be found
    dlerror();  // Clear any previous error
    CreateNodeFunc create_func = (CreateNodeFunc)dlsym(plugin_handle, create_function.c_str());
    const char* error = dlerror();
    
    if (error || !create_func) {
        LOG4CXX_ERROR(logger_, "Cannot find create function '" << create_function 
                      << "' in plugin: " << (error ? error : "unknown error"));
        return std::unexpected(PluginRegistryError::MissingCreateFunction);
    }
    
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        
        if (registered_types_.find(type_name) != registered_types_.end()) {
            LOG4CXX_WARN(logger_, "Node type '" << type_name 
                         << "' already registered, overwriting");
        }
        
        registered_types_[type_name] = PluginNodeInfo{
            .type_name = type_name,
            .description = description,
            .plugin_path = plugin_path,
            .create_function = create_function,
            .abi_tag = abi_tag,
            .plugin_handle = plugin_handle,
            .create_func = create_func,
            .facade = facade,
            .version = version,
        };
        
        LOG4CXX_TRACE(logger_, "Successfully registered node type: " << type_name 
                     << " (version " << version << ", ABI: " << abi_tag << ")");
    }

    return {};
}

std::expected<std::pair<NodeHandle, const NodeFacade*>, PluginRegistry::PluginRegistryError>
PluginRegistry::CreateNodeExpected(const std::string& type_name) noexcept {
    
    LOG4CXX_TRACE(logger_, "Creating node of type: " << type_name);
    
    {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        
        auto it = registered_types_.find(type_name);
        if (it == registered_types_.end()) {
            LOG4CXX_ERROR(logger_, "Node type not found: " << type_name);
            return std::unexpected(PluginRegistryError::TypeNotRegistered);
        }
        
        const auto& info = it->second;
        
        try {
            // Call the plugin's creation function
            LOG4CXX_TRACE(logger_, "Calling create function: " << info.create_function);
            NodeHandle handle = info.create_func();
            
            if (!handle) {
                LOG4CXX_ERROR(logger_, "Plugin creation function failed for type: " << type_name);
                return std::unexpected(PluginRegistryError::CreationFailed);
            }
            
            LOG4CXX_TRACE(logger_, "Successfully created node instance for type: " << type_name);
            return std::pair<NodeHandle, const NodeFacade*>{handle, info.facade};
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(logger_, "Exception during node creation for type: " << type_name 
                          << " - " << e.what());
            return std::unexpected(PluginRegistryError::CreationThrew);
        } catch (...) {
            LOG4CXX_ERROR(logger_, "Unknown exception during node creation for type: " << type_name);
            return std::unexpected(PluginRegistryError::Unknown);
        }
    }
}

bool PluginRegistry::HasNodeType(const std::string& type_name) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    return registered_types_.find(type_name) != registered_types_.end();
}

std::vector<std::string> PluginRegistry::GetRegisteredNodeTypes() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    std::vector<std::string> types;
    for (const auto& [name, _] : registered_types_) {
        types.push_back(name);
    }
    LOG4CXX_TRACE(logger_, "Returning " << types.size() << " registered node types");
    return types;
}

std::optional<PluginRegistry::TypeInfo> PluginRegistry::GetNodeTypeInfo(
    const std::string& type_name) {
    
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = registered_types_.find(type_name);
    
    if (it == registered_types_.end()) {
        return std::nullopt;
    }
    
    const auto& info = it->second;
    return TypeInfo{
        .type_name = info.type_name,
        .description = info.description,
        .plugin_path = info.plugin_path,
        .version = info.version,
        .abi_tag = info.abi_tag,
    };
}

size_t PluginRegistry::GetRegisteredTypeCount() const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    return registered_types_.size();
}

bool PluginRegistry::UnregisterNodeType(const std::string& type_name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto it = registered_types_.find(type_name);
    if (it == registered_types_.end()) {
        LOG4CXX_WARN(logger_, "Cannot unregister unknown type: " << type_name);
        return false;
    }
    
    registered_types_.erase(it);
    LOG4CXX_TRACE(logger_, "Unregistered node type: " << type_name);
    return true;
}

size_t PluginRegistry::UnregisterNodeTypesForHandle(void* plugin_handle) {
    if (!plugin_handle) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(registry_mutex_);
    size_t removed = 0;
    for (auto it = registered_types_.begin(); it != registered_types_.end();) {
        if (it->second.plugin_handle == plugin_handle) {
            LOG4CXX_TRACE(logger_, "Unregistered node type for plugin handle: " << it->first);
            it = registered_types_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void PluginRegistry::Clear() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    LOG4CXX_TRACE(logger_, "Clearing registry (" << registered_types_.size() << " types)");
    registered_types_.clear();
}

}  // namespace graph
