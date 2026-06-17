// SPDX-License-Identifier: MIT

/**
 * @file range_compression_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/RangeCompressionNode.hpp"

using namespace graph;

namespace {

struct RangeCompressionNodePolicy : PluginPolicy<sar::RangeCompressionNode> {};

using Glue = PluginGlue<sar::RangeCompressionNode, RangeCompressionNodePolicy>;
static const NodeFacade range_compression_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_range_compression_node() {
    try {
        auto node = std::make_shared<sar::RangeCompressionNode>();
        return new NodePluginInstance<sar::RangeCompressionNode>(
            node,
            "RangeCompressionNode",
            "plugin.RangeCompressionNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "RangeCompressionNode|SAR FFT-backed range compression node|1.0|"
           "plugin_create_range_compression_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&range_compression_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
