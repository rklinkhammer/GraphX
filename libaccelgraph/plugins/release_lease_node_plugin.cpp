// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/TransferGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct ReleaseLeaseNodePolicy : PluginPolicy<accelgraph::ReleaseLeaseNode> {
    static constexpr const char* Description =
        "Accelgraph transfer lease release node";
};

using Glue = PluginGlue<accelgraph::ReleaseLeaseNode, ReleaseLeaseNodePolicy>;
static const NodeFacade release_lease_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_release_lease_node() {
    try {
        auto node = std::make_shared<accelgraph::ReleaseLeaseNode>();
        return new NodePluginInstance<accelgraph::ReleaseLeaseNode>(
            node,
            "ReleaseLeaseNode",
            "plugin.accelgraph.ReleaseLeaseNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "ReleaseLeaseNode|Accelgraph transfer lease release node|1.0|"
           "plugin_create_release_lease_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&release_lease_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
