// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/TransferGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct HostToDeviceNodePolicy : PluginPolicy<accelgraph::HostToDeviceNode> {
    static constexpr const char* Description =
        "Accelgraph host-to-device transfer node";
};

using Glue = PluginGlue<accelgraph::HostToDeviceNode, HostToDeviceNodePolicy>;
static const NodeFacade host_to_device_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_host_to_device_node() {
    try {
        auto node = std::make_shared<accelgraph::HostToDeviceNode>();
        return new NodePluginInstance<accelgraph::HostToDeviceNode>(
            node,
            "HostToDeviceNode",
            "plugin.accelgraph.HostToDeviceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "HostToDeviceNode|Accelgraph host-to-device transfer node|1.0|"
           "plugin_create_host_to_device_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&host_to_device_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
