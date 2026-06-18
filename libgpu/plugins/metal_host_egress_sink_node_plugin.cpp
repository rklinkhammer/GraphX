/**
 * @file metal_host_egress_sink_node_plugin.cpp
 * @brief Metal Host Egress Sink Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/HostEgressSinkNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalHostEgressSinkNodePolicy : PluginPolicy<graph::gpu::metal::nodes::HostEgressSinkNodeMetal> {
    static constexpr const char* Description =
        "Metal host egress sink node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::HostEgressSinkNodeMetal,
                        MetalHostEgressSinkNodePolicy>;
static const NodeFacade metal_host_egress_sink_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create metal host egress sink node.
 */
void* plugin_create_metal_host_egress_sink_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::HostEgressSinkNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::HostEgressSinkNodeMetal>(
            node,
            "HostEgressSinkNodeMetal",
            "plugin.gpu.metal.HostEgressSinkNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "HostEgressSinkNodeMetal|Metal host egress sink node|1.0|"
           "plugin_create_metal_host_egress_sink_node|"
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
    return const_cast<NodeFacade*>(&metal_host_egress_sink_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
