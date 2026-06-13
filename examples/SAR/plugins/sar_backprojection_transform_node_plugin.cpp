#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/SarBackprojectionTransformAccelNode.hpp"

using namespace graph;

namespace {

struct SarBackprojectionTransformAccelNodePolicy : PluginPolicy<sar::SarBackprojectionTransformAccelNode> {
    [[maybe_unused]] static constexpr const char* Description =
        "SAR backprojection transform node";
};

using Glue = PluginGlue<sar::SarBackprojectionTransformAccelNode, SarBackprojectionTransformAccelNodePolicy>;
static const NodeFacade sar_backprojection_transform_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_sar_backprojection_transform_node() {
    try {
        auto node = std::make_shared<sar::SarBackprojectionTransformAccelNode>();
        return new NodePluginInstance<sar::SarBackprojectionTransformAccelNode>(
            node,
            "SarBackprojectionTransformAccelNode",
            "plugin.SarBackprojectionTransformAccelNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SarBackprojectionTransformAccelNode|SAR backprojection transform node|1.0|"
           "plugin_create_sar_backprojection_transform_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sar_backprojection_transform_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
