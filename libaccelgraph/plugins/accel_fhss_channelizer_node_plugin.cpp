// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssChannelizerNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssChannelizerNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS backend-aware fixture channelizer node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssChannelizerNode, AccelFhssChannelizerNodePolicy>;
static const NodeFacade accel_fhss_channelizer_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_channelizer_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssChannelizerNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssChannelizerNode>(
            node,
            "AccelFhssChannelizerNode",
            "plugin.accelgraph.fhss.AccelFhssChannelizerNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssChannelizerNode|Accelgraph FHSS backend-aware fixture channelizer node|1.0|"
           "plugin_create_accel_fhss_channelizer_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_channelizer_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
