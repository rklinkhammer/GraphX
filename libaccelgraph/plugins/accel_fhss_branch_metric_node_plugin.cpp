// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssBranchMetricNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssBranchMetricNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS backend-aware branch metric node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssBranchMetricNode, AccelFhssBranchMetricNodePolicy>;
static const NodeFacade accel_fhss_branch_metric_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_branch_metric_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssBranchMetricNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssBranchMetricNode>(
            node,
            "AccelFhssBranchMetricNode",
            "plugin.accelgraph.fhss.AccelFhssBranchMetricNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssBranchMetricNode|Accelgraph FHSS backend-aware branch metric node|1.0|"
           "plugin_create_accel_fhss_branch_metric_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_branch_metric_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
