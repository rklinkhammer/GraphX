// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssChannelizerSinkNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssChannelizerSinkNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS channelized IQ sink node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssChannelizerSinkNode, AccelFhssChannelizerSinkNodePolicy>;
static const NodeFacade accel_fhss_channelizer_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_channelizer_sink_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssChannelizerSinkNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssChannelizerSinkNode>(
            node,
            "AccelFhssChannelizerSinkNode",
            "plugin.accelgraph.fhss.AccelFhssChannelizerSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssChannelizerSinkNode|Accelgraph FHSS channelized IQ sink node|1.0|"
           "plugin_create_accel_fhss_channelizer_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_channelizer_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
