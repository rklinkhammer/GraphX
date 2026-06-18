/**
 * @file cuda_d2h_async_node_plugin.cpp
 * @brief CUDA D2H Async Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/cuda/nodes/D2HAsyncNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct CudaD2HAsyncNodePolicy : PluginPolicy<graph::gpu::cuda::nodes::D2HAsyncNode> {
    static constexpr const char* Description =
        "CUDA async device-to-host transfer node plugin";
};

using Glue =
    PluginGlue<graph::gpu::cuda::nodes::D2HAsyncNode, CudaD2HAsyncNodePolicy>;
static const NodeFacade cuda_d2h_async_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create cuda d 2 h async node.
 */
void* plugin_create_cuda_d2h_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::cuda::nodes::D2HAsyncNode>();

        return new NodePluginInstance<graph::gpu::cuda::nodes::D2HAsyncNode>(
            node,
            "D2HAsyncNode",
            "plugin.gpu.cuda.D2HAsyncNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "D2HAsyncNode|CUDA async device-to-host transfer node|1.0|"
           "plugin_create_cuda_d2h_async_node|"
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
    return const_cast<NodeFacade*>(&cuda_d2h_async_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
