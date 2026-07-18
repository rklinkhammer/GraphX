// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/TransferGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct HostIngressNodePolicy : PluginPolicy<accelgraph::HostIngressNode> {
    static constexpr const char* Description =
        "Accelgraph host ingress transfer node";
};

using Glue = PluginGlue<accelgraph::HostIngressNode, HostIngressNodePolicy>;
static const NodeFacade host_ingress_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_host_ingress_node() {
    try {
        auto node = std::make_shared<accelgraph::HostIngressNode>();
        return new NodePluginInstance<accelgraph::HostIngressNode>(
            node,
            "HostIngressNode",
            "plugin.accelgraph.HostIngressNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "HostIngressNode|Accelgraph host ingress transfer node|1.0|"
           "plugin_create_host_ingress_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&host_ingress_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
