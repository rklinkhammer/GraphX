// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssDownconverterNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssDownconverterNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS backend-aware downconverter node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssDownconverterNode, AccelFhssDownconverterNodePolicy>;
static const NodeFacade accel_fhss_downconverter_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_downconverter_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssDownconverterNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssDownconverterNode>(
            node,
            "AccelFhssDownconverterNode",
            "plugin.accelgraph.fhss.AccelFhssDownconverterNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssDownconverterNode|Accelgraph FHSS backend-aware downconverter node|1.0|"
           "plugin_create_accel_fhss_downconverter_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_downconverter_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
