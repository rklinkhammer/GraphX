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

const char* plugin_get_info() {
    return "PeerCopyNodeMetal|Metal peer copy node|1.0|"
           "plugin_create_metal_peer_copy_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_peer_copy_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"