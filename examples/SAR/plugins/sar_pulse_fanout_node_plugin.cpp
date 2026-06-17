// SPDX-License-Identifier: MIT

/**
 * @file sar_pulse_fanout_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/SarPulseFanoutNode.hpp"

using namespace graph;

namespace {

struct SarPulseFanoutNodePolicy : PluginPolicy<sar::SarPulseFanoutNode> {};

using Glue = PluginGlue<sar::SarPulseFanoutNode, SarPulseFanoutNodePolicy>;
static const NodeFacade sar_pulse_fanout_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_sar_pulse_fanout_node() {
    try {
        auto node = std::make_shared<sar::SarPulseFanoutNode>();
        return new NodePluginInstance<sar::SarPulseFanoutNode>(
            node,
            "SarPulseFanoutNode",
            "plugin.SarPulseFanoutNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SarPulseFanoutNode|SAR pulse fan-out node|1.0|"
           "plugin_create_sar_pulse_fanout_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sar_pulse_fanout_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
