// SPDX-License-Identifier: MIT

/**
 * @file sar_visualization_sink_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/SarVisualizationSinkNode.hpp"

using namespace graph;

namespace {

struct SarVisualizationSinkNodePolicy : PluginPolicy<sar::SarVisualizationSinkNode> {};

using Glue = PluginGlue<sar::SarVisualizationSinkNode, SarVisualizationSinkNodePolicy>;
static const NodeFacade sar_visualization_sink_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_sar_visualization_sink_node() {
    try {
        auto node = std::make_shared<sar::SarVisualizationSinkNode>();
        return new NodePluginInstance<sar::SarVisualizationSinkNode>(
            node,
            "SarVisualizationSinkNode",
            "plugin.SarVisualizationSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SarVisualizationSinkNode|SAR visualization sink node|1.0|"
           "plugin_create_sar_visualization_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sar_visualization_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
