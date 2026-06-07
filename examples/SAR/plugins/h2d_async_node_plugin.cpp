#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/H2DAsyncNode.hpp"

using namespace graph;

namespace {

struct H2DAsyncNodePolicy : PluginPolicy<sar::H2DAsyncNode> {};

using Glue = PluginGlue<sar::H2DAsyncNode, H2DAsyncNodePolicy>;
static const NodeFacade h2d_async_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_h2d_async_node() {
    try {
        auto node = std::make_shared<sar::H2DAsyncNode>();
        return new NodePluginInstance<sar::H2DAsyncNode>(
            node,
            "H2DAsyncNode",
            "plugin.H2DAsyncNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "H2DAsyncNode|SAR host-to-device async transfer stage|1.0|"
           "plugin_create_h2d_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&h2d_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
