/**
 * @file PluginInterop.cpp
 * @brief Plugin Interop Graph runtime support.
 *
 * @details Provides plugin loading, reflection, and dynamic node registration support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include "plugins/PluginInterop.hpp"

#include <dlfcn.h>

namespace graph {

namespace {

template <typename FuncT>
std::expected<FuncT, PluginInteropError>
ResolveSymbol(void* plugin_handle, const char* symbol_name) noexcept {
    if (!plugin_handle) {
        return std::unexpected(PluginInteropError::InvalidPluginHandle);
    }

    dlerror();
    auto symbol = reinterpret_cast<FuncT>(dlsym(plugin_handle, symbol_name));
    const char* error = dlerror();

    if (error || !symbol) {
        return std::unexpected(PluginInteropError::MissingSymbol);
    }

    return symbol;
}

}  // namespace

std::expected<CreateNodeFunc, PluginInteropError>
ResolveCreateNodeFunction(void* plugin_handle, const std::string& symbol_name) noexcept {
    return ResolveSymbol<CreateNodeFunc>(plugin_handle, symbol_name.c_str());
}

std::expected<NodeHandle, PluginInteropError>
CreateNodeFromPlugin(CreateNodeFunc create_func) noexcept {
    if (!create_func) {
        return std::unexpected(PluginInteropError::MissingSymbol);
    }

    try {
        NodeHandle handle = create_func();
        if (!handle) {
            return std::unexpected(PluginInteropError::CreationFailed);
        }
        return handle;
    } catch (...) {
        return std::unexpected(PluginInteropError::CreationThrew);
    }
}

std::expected<GetPluginInfoFunc, PluginInteropError>
ResolveGetPluginInfoFunction(void* plugin_handle) noexcept {
    return ResolveSymbol<GetPluginInfoFunc>(plugin_handle, "plugin_get_info");
}

std::expected<GetPluginApiVersionFunc, PluginInteropError>
ResolveGetPluginApiVersionFunction(void* plugin_handle) noexcept {
    return ResolveSymbol<GetPluginApiVersionFunc>(plugin_handle, "plugin_api_version");
}

std::expected<GetPluginFacadeFunc, PluginInteropError>
ResolveGetPluginFacadeFunction(void* plugin_handle) noexcept {
    return ResolveSymbol<GetPluginFacadeFunc>(plugin_handle, "plugin_get_facade");
}

}  // namespace graph
