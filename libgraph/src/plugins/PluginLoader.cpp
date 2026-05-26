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

#include "plugins/PluginLoader.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <log4cxx/logger.h>

namespace fs = std::filesystem;

namespace graph {

log4cxx::LoggerPtr PluginLoader::logger_ =
    log4cxx::Logger::getLogger("graph.PluginLoader");

PluginLoader::PluginLoader(const std::string& plugin_directory,
                           std::shared_ptr<PluginRegistry> registry)
    : plugin_directory_(plugin_directory), registry_(registry) {
    
    LOG4CXX_TRACE(logger_, "PluginLoader initialized for directory: " << plugin_directory);
    
    // Verify directory exists
    if (!fs::exists(plugin_directory)) {
        LOG4CXX_WARN(logger_, "Plugin directory does not exist: " << plugin_directory);
        // Don't throw - might be created later or plugins loaded manually
    }
}

std::vector<std::string> PluginLoader::ParsePluginInfo(
    const std::string& info_string) {
    
    std::vector<std::string> parts;
    std::stringstream ss(info_string);
    std::string part;
    
    while (std::getline(ss, part, '|')) {
        // Trim whitespace
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        parts.push_back(part);
    }
    
    return parts;
}

std::string PluginLoader::GetCurrentABITag() const {
    // Determine which C++ standard library we're using
    // This is a compile-time check, but we report at runtime
    
    #ifdef _GLIBCXX_USE_CXX11_ABI
        #if _GLIBCXX_USE_CXX11_ABI
            return "libstdc++_v1";
        #else
            return "libstdc++_v0";
        #endif
    #else
        // Likely libc++ (Clang/macOS)
        return "libc++_v1";
    #endif
}

void PluginLoader::LoadPlugin(const std::string& plugin_filename) {
    
    LOG4CXX_TRACE(logger_, "Loading plugin: " << plugin_filename);
    
    // Construct full path
    fs::path full_path = fs::path(plugin_directory_) / plugin_filename;
    std::string full_path_str = full_path.string();
    
    LOG4CXX_TRACE(logger_, "Full plugin path: " << full_path_str);
    
    // dlopen the plugin
    // RTLD_LAZY: Resolve symbols as needed
    // RTLD_GLOBAL: Make symbols available to other plugins
    dlerror();  // Clear any previous error
    void* handle = dlopen(full_path_str.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    
    if (!handle) {
        const char* error = dlerror();
        LOG4CXX_ERROR(logger_, "dlopen failed for " << plugin_filename 
                      << ": " << (error ? error : "unknown error"));
        throw std::runtime_error(std::string("Failed to load plugin: ") + plugin_filename);
    }
    
    LOG4CXX_TRACE(logger_, "dlopen successful, handle: " << handle);
    
    // Find the plugin_get_info function
    dlerror();
    typedef const char* (*GetInfoFunc)(void);
    GetInfoFunc get_info = (GetInfoFunc)dlsym(handle, "plugin_get_info");
    const char* error = dlerror();
    
    if (!get_info || error) {
        LOG4CXX_ERROR(logger_, "Plugin missing plugin_get_info: " << plugin_filename);
        dlclose(handle);
        throw std::runtime_error("Plugin missing plugin_get_info function");
    }
    
    LOG4CXX_TRACE(logger_, "Found plugin_get_info function");
    
    // Get plugin metadata
    const char* info_string = get_info();
    if (!info_string) {
        LOG4CXX_ERROR(logger_, "plugin_get_info returned null");
        dlclose(handle);
        throw std::runtime_error("plugin_get_info returned null");
    }
    
    LOG4CXX_TRACE(logger_, "Plugin info: " << info_string);
    
    // Parse info: NodeType|Description|Version|CreateFunctionName|ABITag
    auto parts = ParsePluginInfo(info_string);
    
    if (parts.size() < 5) {
        LOG4CXX_ERROR(logger_, "Plugin info has insufficient fields: " << info_string);
        dlclose(handle);
        throw std::runtime_error("Invalid plugin info format");
    }
    
    std::string type_name = parts[0];
    std::string description = parts[1];
    std::string version = parts[2];
    std::string create_function = parts[3];
    std::string abi_tag = parts[4];
    
    LOG4CXX_TRACE(logger_, "Plugin metadata: type=" << type_name 
                  << ", version=" << version << ", ABI=" << abi_tag);
    
    // Validate ABI compatibility
    std::string current_abi = GetCurrentABITag();
    if (abi_tag != current_abi) {
        LOG4CXX_ERROR(logger_, "ABI mismatch for " << type_name 
                      << ": plugin uses " << abi_tag 
                      << " but application uses " << current_abi);
        dlclose(handle);
        throw std::runtime_error("Plugin ABI incompatible");
    }
    
    LOG4CXX_TRACE(logger_, "ABI validation passed");
    
    // ========================================================================
    // Phase 4: Version Negotiation
    // ========================================================================
    
    // Try to get plugin API version
    dlerror();
    typedef int (*GetApiVersionFunc)(void);
    GetApiVersionFunc get_api_version = (GetApiVersionFunc)dlsym(handle, "plugin_api_version");
    
    int plugin_api_version = 1;  // Default to version 1 for backward compatibility
    
    if (get_api_version) {
        // Plugin explicitly exports version
        plugin_api_version = get_api_version();
        LOG4CXX_TRACE(logger_, "Plugin API version: " << plugin_api_version);
    } else {
        // Plugin doesn't export version - assume version 1
        LOG4CXX_TRACE(logger_, "Plugin missing plugin_api_version, assuming v1");
        dlerror();  // Clear the dlsym error
    }
    
    // Validate version compatibility
    using namespace graph::plugins;
    if (!IsVersionCompatible(plugin_api_version)) {
        std::string version_msg = GetVersionMessage(plugin_api_version);
        LOG4CXX_ERROR(logger_, "Version mismatch for " << type_name 
                      << ": " << version_msg);
        dlclose(handle);
        throw std::runtime_error("Plugin API version incompatible: " + version_msg);
    }
    
    LOG4CXX_TRACE(logger_, "Version validation passed");
    
    // Now we need to get the NodeFacade from the plugin
    // For now, plugins will export a function that returns it
    dlerror();
    typedef const NodeFacade* (*GetFacadeFunc)(void);
    GetFacadeFunc get_facade = (GetFacadeFunc)dlsym(handle, "plugin_get_facade");
    
    if (!get_facade) {
        LOG4CXX_ERROR(logger_, "Plugin missing plugin_get_facade: " << type_name);
        dlclose(handle);
        throw std::runtime_error("Plugin missing plugin_get_facade function");
    }
    
    const NodeFacade* facade = get_facade();
    if (!facade) {
        LOG4CXX_ERROR(logger_, "plugin_get_facade returned null");
        dlclose(handle);
        throw std::runtime_error("plugin_get_facade returned null");
    }
    
    LOG4CXX_TRACE(logger_, "Retrieved NodeFacade from plugin");
    
    // Register with the registry
    try {
        registry_->RegisterNodeType(
            type_name,
            description,
            full_path_str,
            create_function,
            abi_tag,
            version,
            handle,
            facade
        );
    } catch (const std::exception& e) {
        LOG4CXX_ERROR(logger_, "Failed to register node type: " << e.what());
        dlclose(handle);
        throw;
    }
    
    // Remember this plugin
    loaded_plugins_.push_back(plugin_filename);
    plugin_handles_.push_back(handle);
    
    LOG4CXX_TRACE(logger_, "Successfully loaded plugin: " << plugin_filename 
                 << " with node type: " << type_name);
}

void PluginLoader::LoadAllPlugins() {
    
    LOG4CXX_TRACE(logger_, "Loading all plugins from: " << plugin_directory_);
    
    if (!fs::exists(plugin_directory_)) {
        LOG4CXX_ERROR(logger_, "Plugin directory does not exist: " << plugin_directory_);
        throw std::runtime_error("Plugin directory not found: " + plugin_directory_);
    }
    
    size_t loaded_count = 0;
    size_t failed_count = 0;
    
    for (const auto& entry : fs::directory_iterator(plugin_directory_)) {
        // Check for .so (Linux) or .dylib (macOS) files
        if (entry.path().extension() == ".so" ||
            entry.path().extension() == ".dylib") {
            
            try {
                LoadPlugin(entry.path().filename().string());
                loaded_count++;
            } catch (const std::exception& e) {
                LOG4CXX_WARN(logger_, "Failed to load plugin " 
                             << entry.path().filename() << ": " << e.what());
                failed_count++;
            }
        }
    }
    
    LOG4CXX_TRACE(logger_, "Plugin loading complete: " << loaded_count 
                 << " loaded, " << failed_count << " failed");
}

bool PluginLoader::UnloadPlugin(const std::string& plugin_filename) {
    
    auto it = std::find(loaded_plugins_.begin(), loaded_plugins_.end(), plugin_filename);
    
    if (it == loaded_plugins_.end()) {
        LOG4CXX_WARN(logger_, "Cannot unload plugin (not found): " << plugin_filename);
        return false;
    }
    
    size_t index = std::distance(loaded_plugins_.begin(), it);
    void* handle = plugin_handles_[index];
    
    if (dlclose(handle) != 0) {
        LOG4CXX_WARN(logger_, "dlclose failed: " << dlerror());
        return false;
    }
    
    loaded_plugins_.erase(it);
    plugin_handles_.erase(plugin_handles_.begin() + index);
    
    LOG4CXX_TRACE(logger_, "Unloaded plugin: " << plugin_filename);
    return true;
}

// ============================================================================
// Phase 5e: Safe Error Handling Implementations
// ============================================================================

std::expected<void, app::error::PluginLoadError>
PluginLoader::LoadPluginSafe(const std::string& plugin_filename) noexcept {
    try {
        LoadPlugin(plugin_filename);
        return {};  // Return void (success)
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        
        // Map exception message to PluginLoadError
        if (error_msg.find("not found") != std::string::npos ||
            error_msg.find("No such file") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: FileNotFound - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::FileNotFound);
        } else if (error_msg.find("dlopen failed") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: SystemError - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::SystemError);
        } else if (error_msg.find("missing") != std::string::npos ||
                   error_msg.find("plugin_get_") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: MissingSymbol - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::MissingSymbol);
        } else if (error_msg.find("Invalid plugin info") != std::string::npos ||
                   error_msg.find("invalid") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: InvalidFormat - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::InvalidFormat);
        } else if (error_msg.find("ABI") != std::string::npos ||
                   error_msg.find("incompatible") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: VersionMismatch - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::VersionMismatch);
        } else if (error_msg.find("register") != std::string::npos ||
                   error_msg.find("Failed to register") != std::string::npos) {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: InitializationFailed - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::InitializationFailed);
        } else {
            LOG4CXX_ERROR(logger_, "LoadPluginSafe: Unknown - " << error_msg);
            return std::unexpected(app::error::PluginLoadError::Unknown);
        }
    }
}

std::expected<size_t, app::error::PluginLoadError>
PluginLoader::LoadAllPluginsSafe() noexcept {
    try {
        // Check if directory exists first
        if (!fs::exists(plugin_directory_)) {
            LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe: Directory not found - " 
                          << plugin_directory_);
            return std::unexpected(app::error::PluginLoadError::FileNotFound);
        }
        
        LOG4CXX_TRACE(logger_, "LoadAllPluginsSafe: Loading plugins from " 
                      << plugin_directory_);
        
        size_t loaded_count = 0;
        size_t failed_count = 0;
        
        // Scan directory for .so and .dylib files
        for (const auto& entry : fs::directory_iterator(plugin_directory_)) {
            if (entry.path().extension() == ".so" ||
                entry.path().extension() == ".dylib") {
                
                // Try to load each plugin, but don't fail overall if one fails
                auto result = LoadPluginSafe(entry.path().filename().string());
                if (result) {
                    loaded_count++;
                } else {
                    LOG4CXX_WARN(logger_, "LoadAllPluginsSafe: Failed to load plugin "
                                 << entry.path().filename() << " - "
                                 << app::error::ErrorMessage(result.error()));
                    failed_count++;
                }
            }
        }
        
        LOG4CXX_TRACE(logger_, "LoadAllPluginsSafe: Complete - "
                      << loaded_count << " loaded, " << failed_count << " failed");
        
        return loaded_count;
        
    } catch (const std::exception& e) {
        std::string error_msg = e.what();
        LOG4CXX_ERROR(logger_, "LoadAllPluginsSafe: Exception - " << error_msg);
        
        // Map exception to appropriate error
        if (error_msg.find("not found") != std::string::npos) {
            return std::unexpected(app::error::PluginLoadError::FileNotFound);
        } else {
            return std::unexpected(app::error::PluginLoadError::SystemError);
        }
    }
}

PluginLoader::~PluginLoader() {
    LOG4CXX_TRACE(logger_, "PluginLoader destructor: releasing bookkeeping for "
                  << plugin_handles_.size() << " plugin handles");

    // Do not dlclose() here. PluginRegistry/NodeFactory can retain function and
    // facade pointers into these libraries through shutdown, and plugin-level
    // static destructors can also depend on process-wide logging state. Explicit
    // unloading remains available through UnloadPlugin().
    plugin_handles_.clear();
    loaded_plugins_.clear();
}

}  // namespace graph
