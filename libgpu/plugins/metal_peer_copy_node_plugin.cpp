/**
 * @file metal_peer_copy_node_plugin.cpp
 * @brief Metal Peer Copy Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/PeerCopyNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalPeerCopyNodePolicy : PluginPolicy<graph::gpu::metal::nodes::PeerCopyNodeMetal> {
    static constexpr const char* Description =
        "Metal peer copy node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::PeerCopyNodeMetal,
                        MetalPeerCopyNodePolicy>;
static const NodeFacade metal_peer_copy_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create metal peer copy node.
 */
void* plugin_create_metal_peer_copy_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::PeerCopyNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::PeerCopyNodeMetal>(
            node,
            "PeerCopyNodeMetal",
            "plugin.gpu.metal.PeerCopyNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "PeerCopyNodeMetal|Metal peer copy node|1.0|"
           "plugin_create_metal_peer_copy_node|"
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
    return const_cast<NodeFacade*>(&metal_peer_copy_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
