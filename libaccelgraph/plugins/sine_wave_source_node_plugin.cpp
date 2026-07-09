// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SineWaveSourceNodePolicy : PluginPolicy<accelgraph::SineWaveSourceNode> {
    static constexpr const char* Description =
        "Accelgraph deterministic sine-wave IQ source node";
};

using Glue = PluginGlue<accelgraph::SineWaveSourceNode, SineWaveSourceNodePolicy>;
static const NodeFacade sine_wave_source_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_sine_wave_source_node() {
    try {
        auto node = std::make_shared<accelgraph::SineWaveSourceNode>();
        return new NodePluginInstance<accelgraph::SineWaveSourceNode>(
            node,
            "SineWaveSourceNode",
            "plugin.accelgraph.SineWaveSourceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SineWaveSourceNode|Accelgraph deterministic sine-wave IQ source node|1.0|"
           "plugin_create_sine_wave_source_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sine_wave_source_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
