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

inline bool IsVersionCompatible(int plugin_version) {
    return plugin_version == CURRENT_API_VERSION;
}

}  // namespace graph::plugins
