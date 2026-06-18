/**
 * @file PluginInterop.hpp
 * @brief Plugin Interop Graph runtime support.
 *
 * @details Provides plugin loading, reflection, and dynamic node registration support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <expected>
#include <string>

#include "graph/NodeFacadeAbi.hpp"

namespace graph {

/**

 * @struct NodeFacade

 * @brief Node Facade data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct NodeFacade;

using GetPluginInfoFunc = const char* (*)();
using GetPluginApiVersionFunc = int (*)();
using GetPluginFacadeFunc = const NodeFacade* (*)();

/**

 * @enum PluginInteropError

 * @brief Plugin Interop Error values.

 *

 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

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
