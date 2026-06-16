#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/CrsdFocusedImageSinkNode.hpp"

using namespace graph;

namespace {

struct CrsdFocusedImageSinkNodePolicy : PluginPolicy<sar::CrsdFocusedImageSinkNode> {};

using Glue = PluginGlue<sar::CrsdFocusedImageSinkNode, CrsdFocusedImageSinkNodePolicy>;
static const NodeFacade crsd_focused_image_sink_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_crsd_focused_image_sink_node() {
    try {
        auto node = std::make_shared<sar::CrsdFocusedImageSinkNode>();
        return new NodePluginInstance<sar::CrsdFocusedImageSinkNode>(
            node,
            "CrsdFocusedImageSinkNode",
            "plugin.CrsdFocusedImageSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "CrsdFocusedImageSinkNode|CRSD focused-image deterministic output sink node|1.0|"
           "plugin_create_crsd_focused_image_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&crsd_focused_image_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
