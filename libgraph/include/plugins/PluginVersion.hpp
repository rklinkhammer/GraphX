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

#pragma once

namespace graph::plugins {

// ============================================================================
// Plugin API Version Management (Phase 4)
// ============================================================================

/**
 * @brief Current plugin API version supported by the loader
 * 
 * This version number is incremented when breaking changes are made to:
 * - NodeFacade struct layout (adding/removing/reordering fields)
 * - Plugin entry point signatures (plugin_create_node, plugin_get_facade, etc.)
 * - Required plugin exports
 * 
 * Backward compatibility:
 * - Plugins with API_VERSION <= CURRENT_API_VERSION are accepted
 * - Plugins with API_VERSION > CURRENT_API_VERSION are rejected with clear error
 * - Forward compatibility: Older loaders reject newer plugins automatically
 * 
 * Version History:
 * - Version 1: Initial release (Phases 1-3)
 *   - NodeFacade struct with 40+ function pointers
 *   - plugin_get_facade() for NodeFacade access
 *   - plugin_get_info() for metadata
 * 
 * - Version 2: Phase 4 addition
 *   - Added plugin_api_version() required export
 *   - Version checking in PluginLoader
 *   - Safe version negotiation at load time
 *   - Prepared for future NodeFacadeV3, V4, etc.
 * 
 * @note Plugins must export: int plugin_api_version(void) { return version; }
 */
constexpr int CURRENT_API_VERSION = 2;

/**
 * @brief Minimum API version supported by this loader
 * 
 * Plugins with API_VERSION < MINIMUM_API_VERSION are rejected.
 * This allows us to drop support for very old plugins in future versions.
 * 
 * GraphX does not support pre-versioned plugin APIs.
 */
constexpr int MINIMUM_API_VERSION = CURRENT_API_VERSION;

/**
 * @brief Check if a plugin API version is compatible with the loader
 * 
 * @param plugin_version API version exported by plugin via plugin_api_version()
 * @return true if plugin_version is compatible (between MINIMUM and CURRENT)
 * @return false if plugin_version is too old or newer than loader supports
 * 
 * @example
 * ```cpp
 * int plugin_version = plugin_get_api_version();
 * if (!IsVersionCompatible(plugin_version)) {
 *     if (plugin_version > CURRENT_API_VERSION) {
 *         error("Plugin requires newer loader");
 *     } else {
 *         error("Plugin too old, not supported");
 *     }
 * }
 * ```
 */
inline bool IsVersionCompatible(int plugin_version) {
    return plugin_version >= MINIMUM_API_VERSION && 
           plugin_version <= CURRENT_API_VERSION;
}

/**
 * @brief Get a human-readable version compatibility message
 * 
 * @param plugin_version API version from plugin
 * @return String describing the compatibility status
 * 
 * Examples:
 * - "Compatible (plugin v1, loader v2)"
 * - "Incompatible: plugin v3 requires loader v3 or newer"
 * - "Incompatible: plugin v1 is no longer supported (minimum is v2)"
 */
inline std::string GetVersionMessage(int plugin_version) {
    if (IsVersionCompatible(plugin_version)) {
        return "Compatible (plugin v" + std::to_string(plugin_version) + 
               ", loader v" + std::to_string(CURRENT_API_VERSION) + ")";
    } else if (plugin_version > CURRENT_API_VERSION) {
        return "Incompatible: plugin v" + std::to_string(plugin_version) + 
               " requires loader v" + std::to_string(plugin_version) + " or newer";
    } else {
        return "Incompatible: plugin v" + std::to_string(plugin_version) + 
               " is no longer supported (minimum is v" + std::to_string(MINIMUM_API_VERSION) + ")";
    }
}

}  // namespace graph::plugins
