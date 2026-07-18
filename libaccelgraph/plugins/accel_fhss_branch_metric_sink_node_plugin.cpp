// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssBranchMetricSinkNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssBranchMetricSinkNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS branch metric sink node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssBranchMetricSinkNode, AccelFhssBranchMetricSinkNodePolicy>;
static const NodeFacade accel_fhss_branch_metric_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_branch_metric_sink_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssBranchMetricSinkNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssBranchMetricSinkNode>(
            node,
            "AccelFhssBranchMetricSinkNode",
            "plugin.accelgraph.fhss.AccelFhssBranchMetricSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssBranchMetricSinkNode|Accelgraph FHSS branch metric sink node|1.0|"
           "plugin_create_accel_fhss_branch_metric_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_branch_metric_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
