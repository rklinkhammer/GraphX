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
 * @file source_test_node_plugin.cpp
 * @brief SourceTestNode as a dynamically-loadable plugin
 *
 * Exports SourceTestNode (data producer) as a dynamically-loadable plugin
 * for Stage 5.5a completion testing.
 */

#include <memory>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "test/AdvancedTestNodes.hpp"

using namespace graph;
using namespace test;

// ============================================================================
// Policy specialization
// ============================================================================

struct SourceTestNodePolicy : PluginPolicy<SourceTestNode> {
    static constexpr const char* Description =
        "Source test node - data producer with ICompletionCallback support";
};

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<SourceTestNode, SourceTestNodePolicy>;
static const NodeFacade source_test_node_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

void* plugin_create_source_test_node() {
    try {
        auto node = std::make_shared<SourceTestNode>();
        return new NodePluginInstance<SourceTestNode>(
            node, "SourceTestNode", "plugin.SourceTestNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SourceTestNode|Source test node - data producer|1.0|"
           "plugin_create_source_test_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&source_test_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
