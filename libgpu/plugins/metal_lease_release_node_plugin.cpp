// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/LeaseReleaseNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalLeaseReleaseNodePolicy : PluginPolicy<graph::gpu::metal::nodes::LeaseReleaseNodeMetal> {
    static constexpr const char* Description =
        "Metal lease release node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::LeaseReleaseNodeMetal,
                        MetalLeaseReleaseNodePolicy>;
static const NodeFacade metal_lease_release_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_lease_release_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::LeaseReleaseNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::LeaseReleaseNodeMetal>(
            node,
            "LeaseReleaseNodeMetal",
            "plugin.gpu.metal.LeaseReleaseNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "LeaseReleaseNodeMetal|Metal lease release node|1.0|"
           "plugin_create_metal_lease_release_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_lease_release_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"