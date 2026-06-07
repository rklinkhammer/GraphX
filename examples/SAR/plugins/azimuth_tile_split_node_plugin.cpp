#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/AzimuthTileSplitNode.hpp"

using namespace graph;

namespace {

struct AzimuthTileSplitNodePolicy : PluginPolicy<sar::AzimuthTileSplitNode> {
    static constexpr const char* Description = "SAR azimuth tile split node";
};

using Glue = PluginGlue<sar::AzimuthTileSplitNode, AzimuthTileSplitNodePolicy>;
static const NodeFacade azimuth_tile_split_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_azimuth_tile_split_node() {
    try {
        auto node = std::make_shared<sar::AzimuthTileSplitNode>();
        return new NodePluginInstance<sar::AzimuthTileSplitNode>(
            node,
            "AzimuthTileSplitNode",
            "plugin.AzimuthTileSplitNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AzimuthTileSplitNode|SAR azimuth tile split node|1.0|"
           "plugin_create_azimuth_tile_split_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&azimuth_tile_split_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
