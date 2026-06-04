// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/H2DAsyncNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalH2DAsyncNodePolicy : PluginPolicy<graph::gpu::metal::nodes::H2DAsyncNodeMetal> {
    static constexpr const char* Description =
        "Metal async host-to-device transfer node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::H2DAsyncNodeMetal,
                        MetalH2DAsyncNodePolicy>;
static const NodeFacade metal_h2d_async_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_h2d_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::H2DAsyncNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::H2DAsyncNodeMetal>(
            node,
            "H2DAsyncNodeMetal",
            "plugin.gpu.metal.H2DAsyncNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "H2DAsyncNodeMetal|Metal async host-to-device transfer node|1.0|"
           "plugin_create_metal_h2d_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_h2d_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"