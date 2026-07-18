// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssPerChannelPulseDetectorNodePolicy : PluginPolicy<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS backend-aware per-channel pulse detector node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode,
                        AccelFhssPerChannelPulseDetectorNodePolicy>;
static const NodeFacade accel_fhss_per_channel_pulse_detector_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_per_channel_pulse_detector_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode>(
            node,
            "AccelFhssPerChannelPulseDetectorNode",
            "plugin.accelgraph.fhss.AccelFhssPerChannelPulseDetectorNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssPerChannelPulseDetectorNode|Accelgraph FHSS backend-aware per-channel pulse detector node|1.0|"
           "plugin_create_accel_fhss_per_channel_pulse_detector_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_per_channel_pulse_detector_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
