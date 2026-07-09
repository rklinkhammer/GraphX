// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SpectrumSinkNodePolicy : PluginPolicy<accelgraph::SpectrumSinkNode> {
    static constexpr const char* Description =
        "Accelgraph spectrum sink node";
};

using Glue = PluginGlue<accelgraph::SpectrumSinkNode, SpectrumSinkNodePolicy>;
static const NodeFacade spectrum_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_spectrum_sink_node() {
    try {
        auto node = std::make_shared<accelgraph::SpectrumSinkNode>();
        return new NodePluginInstance<accelgraph::SpectrumSinkNode>(
            node,
            "SpectrumSinkNode",
            "plugin.accelgraph.SpectrumSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SpectrumSinkNode|Accelgraph spectrum sink node|1.0|"
           "plugin_create_spectrum_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&spectrum_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
