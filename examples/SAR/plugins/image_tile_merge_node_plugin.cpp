#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/ImageTileMergeNode.hpp"

using namespace graph;

namespace {

struct ImageTileMergeNodePolicy : PluginPolicy<sar::ImageTileMergeNode> {};

using Glue = PluginGlue<sar::ImageTileMergeNode, ImageTileMergeNodePolicy>;
static const NodeFacade image_tile_merge_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_image_tile_merge_node() {
    try {
        auto node = std::make_shared<sar::ImageTileMergeNode>();
        return new NodePluginInstance<sar::ImageTileMergeNode>(
            node,
            "ImageTileMergeNode",
            "plugin.ImageTileMergeNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "ImageTileMergeNode|SAR image tile merge node|1.0|"
           "plugin_create_image_tile_merge_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&image_tile_merge_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
