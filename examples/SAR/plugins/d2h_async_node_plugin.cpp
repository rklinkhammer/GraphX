#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/D2HAsyncAccelNode.hpp"

using namespace graph;

namespace {

struct D2HAsyncAccelNodePolicy : PluginPolicy<sar::D2HAsyncAccelNode> {};

using Glue = PluginGlue<sar::D2HAsyncAccelNode, D2HAsyncAccelNodePolicy>;
static const NodeFacade d2h_async_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_d2h_async_node() {
    try {
        auto node = std::make_shared<sar::D2HAsyncAccelNode>();
        return new NodePluginInstance<sar::D2HAsyncAccelNode>(
            node,
            "D2HAsyncAccelNode",
            "plugin.D2HAsyncAccelNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "D2HAsyncAccelNode|SAR device-to-host async transfer stage|1.0|"
           "plugin_create_d2h_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&d2h_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
