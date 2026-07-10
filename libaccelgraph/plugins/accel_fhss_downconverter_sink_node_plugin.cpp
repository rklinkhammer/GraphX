// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssDownconverterSinkNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssDownconverterSinkNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS downconverted IQ sink node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssDownconverterSinkNode, AccelFhssDownconverterSinkNodePolicy>;
static const NodeFacade accel_fhss_downconverter_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_downconverter_sink_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssDownconverterSinkNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssDownconverterSinkNode>(
            node,
            "AccelFhssDownconverterSinkNode",
            "plugin.accelgraph.fhss.AccelFhssDownconverterSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssDownconverterSinkNode|Accelgraph FHSS downconverted IQ sink node|1.0|"
           "plugin_create_accel_fhss_downconverter_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_downconverter_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
