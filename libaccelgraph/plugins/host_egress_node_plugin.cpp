// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/TransferGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct HostEgressNodePolicy : PluginPolicy<accelgraph::HostEgressNode> {
    static constexpr const char* Description =
        "Accelgraph host egress transfer node";
};

using Glue = PluginGlue<accelgraph::HostEgressNode, HostEgressNodePolicy>;
static const NodeFacade host_egress_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_host_egress_node() {
    try {
        auto node = std::make_shared<accelgraph::HostEgressNode>();
        return new NodePluginInstance<accelgraph::HostEgressNode>(
            node,
            "HostEgressNode",
            "plugin.accelgraph.HostEgressNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "HostEgressNode|Accelgraph host egress transfer node|1.0|"
           "plugin_create_host_egress_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&host_egress_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
