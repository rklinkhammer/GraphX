/**
 * @file int_sink_plugin.cpp
 * @brief Int Sink Plugin Graph runtime support.
 *
 * @details Provides plugin loading, reflection, and dynamic node registration support. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include <memory>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "test/ProducerTestNodes.hpp"

using namespace graph;
using namespace test;

// ============================================================================
// Policy specialization
// ============================================================================

struct TestIntSinkNodePolicy : PluginPolicy<TestIntSinkNode> {
    static constexpr const char* Description =
        "Integer sink test node - consumes Message and unpacks int values";
};

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<TestIntSinkNode, TestIntSinkNodePolicy>;
static const NodeFacade int_sink_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

/**
 * @brief Plugin create int sink.
 */
void* plugin_create_int_sink() {
    try {
        auto node = std::make_shared<TestIntSinkNode>();
        return new NodePluginInstance<TestIntSinkNode>(
            node, "TestIntSinkNode", "plugin.TestIntSinkNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "TestIntSinkNode|Integer sink test node|1.0|"
           "plugin_create_int_sink|"
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
    return const_cast<NodeFacade*>(&int_sink_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

}  // extern "C"
