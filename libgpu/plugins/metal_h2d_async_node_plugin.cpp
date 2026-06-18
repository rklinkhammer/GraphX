/**
 * @file metal_h2d_async_node_plugin.cpp
 * @brief Metal H2D Async Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
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

/**
 * @brief Plugin create metal h 2 d async node.
 */
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

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "H2DAsyncNodeMetal|Metal async host-to-device transfer node|1.0|"
           "plugin_create_metal_h2d_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

/**
 * @brief Plugin get facade.
 */
NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_h2d_async_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
