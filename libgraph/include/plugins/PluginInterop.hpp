// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <expected>
#include <string>

#include "graph/NodeFacadeAbi.hpp"

namespace graph {

struct NodeFacade;

using GetPluginInfoFunc = const char* (*)();
using GetPluginApiVersionFunc = int (*)();
using GetPluginFacadeFunc = const NodeFacade* (*)();

enum class PluginInteropError {
    InvalidPluginHandle = 1,
    MissingSymbol = 2,
    CreationFailed = 3,
    CreationThrew = 4,
};

// Resolve a plugin node creation symbol from a loaded plugin handle.
[[nodiscard]] std::expected<CreateNodeFunc, PluginInteropError>
ResolveCreateNodeFunction(void* plugin_handle, const std::string& symbol_name) noexcept;

// Invoke a plugin node creation function and normalize failure handling.
[[nodiscard]] std::expected<NodeHandle, PluginInteropError>
CreateNodeFromPlugin(CreateNodeFunc create_func) noexcept;

// Resolve plugin_get_info symbol.
[[nodiscard]] std::expected<GetPluginInfoFunc, PluginInteropError>
ResolveGetPluginInfoFunction(void* plugin_handle) noexcept;

// Resolve plugin_api_version symbol (optional in some plugins).
[[nodiscard]] std::expected<GetPluginApiVersionFunc, PluginInteropError>
ResolveGetPluginApiVersionFunction(void* plugin_handle) noexcept;

// Resolve plugin_get_facade symbol.
[[nodiscard]] std::expected<GetPluginFacadeFunc, PluginInteropError>
ResolveGetPluginFacadeFunction(void* plugin_handle) noexcept;

}  // namespace graph
