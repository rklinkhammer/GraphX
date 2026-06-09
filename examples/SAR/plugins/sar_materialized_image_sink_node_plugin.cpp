#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

using namespace graph;

namespace {

struct SarMaterializedImageSinkNodePolicy : PluginPolicy<sar::SarMaterializedImageSinkNode> {};

using Glue = PluginGlue<sar::SarMaterializedImageSinkNode, SarMaterializedImageSinkNodePolicy>;
static const NodeFacade sar_materialized_image_sink_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_sar_materialized_image_sink_node() {
    try {
        auto node = std::make_shared<sar::SarMaterializedImageSinkNode>();
        return new NodePluginInstance<sar::SarMaterializedImageSinkNode>(
            node,
            "SarMaterializedImageSinkNode",
            "plugin.SarMaterializedImageSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SarMaterializedImageSinkNode|SAR in-memory materialized image sink node|1.0|"
           "plugin_create_sar_materialized_image_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sar_materialized_image_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"