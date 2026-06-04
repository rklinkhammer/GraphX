// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/HostIngressPinnedSourceNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalHostIngressPinnedSourceNodePolicy
    : PluginPolicy<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal> {
    static constexpr const char* Description =
        "Metal host ingress pinned source node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal,
                        MetalHostIngressPinnedSourceNodePolicy>;
static const NodeFacade metal_host_ingress_pinned_source_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_host_ingress_pinned_source_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::HostIngressPinnedSourceNodeMetal>(
            node,
            "HostIngressPinnedSourceNodeMetal",
            "plugin.gpu.metal.HostIngressPinnedSourceNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "HostIngressPinnedSourceNodeMetal|Metal host ingress pinned source node|1.0|"
           "plugin_create_metal_host_ingress_pinned_source_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_host_ingress_pinned_source_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"