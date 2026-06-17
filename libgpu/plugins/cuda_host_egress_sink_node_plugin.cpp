/**
 * @file cuda_host_egress_sink_node_plugin.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/cuda/nodes/HostEgressSinkNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct CudaHostEgressSinkNodePolicy
    : PluginPolicy<graph::gpu::cuda::nodes::HostEgressSinkNode> {
    static constexpr const char* Description =
        "CUDA host egress sink node plugin";
};

using Glue = PluginGlue<graph::gpu::cuda::nodes::HostEgressSinkNode,
                        CudaHostEgressSinkNodePolicy>;
static const NodeFacade cuda_host_egress_sink_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create cuda host egress sink node.
 */
void* plugin_create_cuda_host_egress_sink_node() {
    try {
        auto node = std::make_shared<graph::gpu::cuda::nodes::HostEgressSinkNode>();
        return new NodePluginInstance<graph::gpu::cuda::nodes::HostEgressSinkNode>(
            node,
            "HostEgressSinkNode",
            "plugin.gpu.cuda.HostEgressSinkNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "HostEgressSinkNode|CUDA host egress sink node|1.0|"
           "plugin_create_cuda_host_egress_sink_node|"
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
    return const_cast<NodeFacade*>(&cuda_host_egress_sink_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
