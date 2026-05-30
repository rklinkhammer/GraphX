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
 * @file double_sink_plugin.cpp
 * @brief TestDoubleSinkNode as a dynamically-loadable plugin
 *
 * Exports TestDoubleSinkNode (double sink consumer) as a dynamically-loadable plugin
 * for producer-based topology testing.
 */

#include <memory>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "test/ProducerTestNodes.hpp"

using namespace graph;
using namespace test;

// ============================================================================
// Policy specialization
// ============================================================================

struct TestDoubleSinkNodePolicy : PluginPolicy<TestDoubleSinkNode> {
    static constexpr const char* Description =
        "Double sink test node - consumes Message and unpacks double values";
};

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<TestDoubleSinkNode, TestDoubleSinkNodePolicy>;
static const NodeFacade double_sink_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

void* plugin_create_double_sink() {
    try {
        auto node = std::make_shared<TestDoubleSinkNode>();
        return new NodePluginInstance<TestDoubleSinkNode>(
            node, "TestDoubleSinkNode", "plugin.TestDoubleSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "TestDoubleSinkNode|Double sink test node|1.0|"
           "plugin_create_double_sink|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&double_sink_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
