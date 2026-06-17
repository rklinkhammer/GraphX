/**
 * @file cuda_host_ingress_pinned_source_node_plugin.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/cuda/nodes/HostIngressPinnedSourceNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct CudaHostIngressPinnedSourceNodePolicy
    : PluginPolicy<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode> {
    static constexpr const char* Description =
        "CUDA host ingress pinned source node plugin";
};

using Glue = PluginGlue<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode,
                        CudaHostIngressPinnedSourceNodePolicy>;
static const NodeFacade cuda_host_ingress_pinned_source_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create cuda host ingress pinned source node.
 */
void* plugin_create_cuda_host_ingress_pinned_source_node() {
    try {
        auto node = std::make_shared<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode>();

        return new NodePluginInstance<graph::gpu::cuda::nodes::HostIngressPinnedSourceNode>(
            node,
            "HostIngressPinnedSourceNode",
            "plugin.gpu.cuda.HostIngressPinnedSourceNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "HostIngressPinnedSourceNode|CUDA host ingress pinned source node|1.0|"
           "plugin_create_cuda_host_ingress_pinned_source_node|"
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
    return const_cast<NodeFacade*>(&cuda_host_ingress_pinned_source_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
