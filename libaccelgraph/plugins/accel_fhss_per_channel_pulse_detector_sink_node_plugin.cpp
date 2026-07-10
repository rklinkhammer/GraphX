// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct AccelFhssPerChannelPulseDetectorSinkNodePolicy
    : PluginPolicy<accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode> {
    static constexpr const char* Description =
        "Accelgraph FHSS per-channel pulse evidence sink node";
};

using Glue = PluginGlue<accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode,
                        AccelFhssPerChannelPulseDetectorSinkNodePolicy>;
static const NodeFacade accel_fhss_per_channel_pulse_detector_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_accel_fhss_per_channel_pulse_detector_sink_node() {
    try {
        auto node = std::make_shared<accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode>();
        return new NodePluginInstance<accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode>(
            node,
            "AccelFhssPerChannelPulseDetectorSinkNode",
            "plugin.accelgraph.fhss.AccelFhssPerChannelPulseDetectorSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AccelFhssPerChannelPulseDetectorSinkNode|Accelgraph FHSS per-channel pulse evidence sink node|1.0|"
           "plugin_create_accel_fhss_per_channel_pulse_detector_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&accel_fhss_per_channel_pulse_detector_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
