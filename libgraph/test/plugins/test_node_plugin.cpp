/**
 * @file test_node_plugin.cpp
 * @brief GraphX source file.
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

/**
 * @file flight_monitor_node_plugin.cpp
 * @brief FlightMonitorNode as a dynamically-loadable plugin
 *
 * This file demonstrates how to expose FlightMonitorNode (flight phase determination)
 * as a dynamically-loadable plugin using the NodeFacade interface.
 *
 * Converts state vector data into discrete flight phase enumerations.
 *
 * Compilation (from workspace root):
 *   mkdir -p build/plugins
 *   cd build
 *   cmake ..
 *   make flight_monitor_node
 *
 * This produces: build/plugins/libflight_monitor_node.so
 */

#include <memory>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "test/TestNode.hpp"

using namespace graph;
using namespace test;

// ============================================================================
// Policy specialization
// ============================================================================

struct TestNodePolicy : PluginPolicy<TestNode> {
    static constexpr const char* Description =
        "Test node for plugin dynamic loading verification";

 };

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<TestNode, TestNodePolicy>;
static const NodeFacade test_node_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

/**
 * @brief Plugin create test node.
 */
void* plugin_create_test_node() {
    try {
        auto node = std::make_shared<TestNode>();
        return new NodePluginInstance<TestNode>(
            node, "TestNode", "plugin.TestNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "TestNode|Test node for plugin dynamic loading|1.0|"
           "plugin_create_test_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

/**
 * @brief Plugin get facade.
 */
NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&test_node_facade);
}

// Phase 4: Plugin API version negotiation
/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;  // Supports PluginLoader v2+ (version negotiation)
}

}

