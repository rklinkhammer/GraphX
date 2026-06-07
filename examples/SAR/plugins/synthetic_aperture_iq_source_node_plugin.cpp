#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/SyntheticApertureIqSourceNode.hpp"

using namespace graph;

namespace {

struct SyntheticApertureIqSourceNodePolicy : PluginPolicy<sar::SyntheticApertureIqSourceNode> {};

using Glue = PluginGlue<sar::SyntheticApertureIqSourceNode, SyntheticApertureIqSourceNodePolicy>;
static const NodeFacade synthetic_aperture_iq_source_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_synthetic_aperture_iq_source_node() {
    try {
        auto node = std::make_shared<sar::SyntheticApertureIqSourceNode>();
        return new NodePluginInstance<sar::SyntheticApertureIqSourceNode>(
            node,
            "SyntheticApertureIqSourceNode",
            "plugin.SyntheticApertureIqSourceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SyntheticApertureIqSourceNode|SAR synthetic aperture IQ source node|1.0|"
           "plugin_create_synthetic_aperture_iq_source_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&synthetic_aperture_iq_source_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
