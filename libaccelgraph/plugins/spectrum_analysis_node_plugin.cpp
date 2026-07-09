// SPDX-License-Identifier: MIT

#include <memory>

#include "accelgraph/SpectrumGraphNodes.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SpectrumAnalysisNodePolicy : PluginPolicy<accelgraph::SpectrumAnalysisNode> {
    static constexpr const char* Description =
        "Accelgraph backend-neutral spectrum analysis node";
};

using Glue = PluginGlue<accelgraph::SpectrumAnalysisNode, SpectrumAnalysisNodePolicy>;
static const NodeFacade spectrum_analysis_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_spectrum_analysis_node() {
    try {
        auto node = std::make_shared<accelgraph::SpectrumAnalysisNode>();
        return new NodePluginInstance<accelgraph::SpectrumAnalysisNode>(
            node,
            "SpectrumAnalysisNode",
            "plugin.accelgraph.SpectrumAnalysisNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SpectrumAnalysisNode|Accelgraph backend-neutral spectrum analysis node|1.0|"
           "plugin_create_spectrum_analysis_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&spectrum_analysis_node_facade);
}

int plugin_api_version() {
    return 2;
}

}  // extern "C"
