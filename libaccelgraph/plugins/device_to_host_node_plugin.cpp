// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/TransferGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct DeviceToHostNodePolicy : PluginPolicy<accelgraph::DeviceToHostNode> {
    static constexpr const char* Description =
        "Accelgraph device-to-host transfer node";
};

using Glue = PluginGlue<accelgraph::DeviceToHostNode, DeviceToHostNodePolicy>;
static const NodeFacade device_to_host_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_device_to_host_node() {
    try {
        auto node = std::make_shared<accelgraph::DeviceToHostNode>();
        return new NodePluginInstance<accelgraph::DeviceToHostNode>(
            node,
            "DeviceToHostNode",
            "plugin.accelgraph.DeviceToHostNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "DeviceToHostNode|Accelgraph device-to-host transfer node|1.0|"
           "plugin_create_device_to_host_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&device_to_host_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
