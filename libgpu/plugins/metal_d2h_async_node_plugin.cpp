// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/D2HAsyncNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalD2HAsyncNodePolicy : PluginPolicy<graph::gpu::metal::nodes::D2HAsyncNodeMetal> {
    static constexpr const char* Description =
        "Metal async device-to-host transfer node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::D2HAsyncNodeMetal,
                        MetalD2HAsyncNodePolicy>;
static const NodeFacade metal_d2h_async_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_d2h_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::D2HAsyncNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::D2HAsyncNodeMetal>(
            node,
            "D2HAsyncNodeMetal",
            "plugin.gpu.metal.D2HAsyncNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "D2HAsyncNodeMetal|Metal async device-to-host transfer node|1.0|"
           "plugin_create_metal_d2h_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_d2h_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"