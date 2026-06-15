#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/OrderedCrsdSetInputSourceNode.hpp"

using namespace graph;

namespace {

struct OrderedCrsdSetInputSourceNodePolicy : PluginPolicy<sar::OrderedCrsdSetInputSourceNode> {
    [[maybe_unused]] static constexpr const char* Description =
        "SAR ordered CRSD set input source node";
};

using Glue = PluginGlue<sar::OrderedCrsdSetInputSourceNode, OrderedCrsdSetInputSourceNodePolicy>;
static const NodeFacade ordered_crsd_set_input_source_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_ordered_crsd_set_input_source_node() {
    try {
        auto node = std::make_shared<sar::OrderedCrsdSetInputSourceNode>();
        return new NodePluginInstance<sar::OrderedCrsdSetInputSourceNode>(
            node,
            "OrderedCrsdSetInputSourceNode",
            "plugin.OrderedCrsdSetInputSourceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "OrderedCrsdSetInputSourceNode|SAR ordered CRSD set input source node|1.0|"
           "plugin_create_ordered_crsd_set_input_source_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&ordered_crsd_set_input_source_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
