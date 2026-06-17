// SPDX-License-Identifier: MIT

/**
 * @file range_window_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/RangeWindowNode.hpp"

using namespace graph;

namespace {

struct RangeWindowNodePolicy : PluginPolicy<sar::RangeWindowNode> {};

using Glue = PluginGlue<sar::RangeWindowNode, RangeWindowNodePolicy>;
static const NodeFacade range_window_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_range_window_node() {
    try {
        auto node = std::make_shared<sar::RangeWindowNode>();
        return new NodePluginInstance<sar::RangeWindowNode>(
            node,
            "RangeWindowNode",
            "plugin.RangeWindowNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "RangeWindowNode|SAR deterministic range window node|1.0|"
           "plugin_create_range_window_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&range_window_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
