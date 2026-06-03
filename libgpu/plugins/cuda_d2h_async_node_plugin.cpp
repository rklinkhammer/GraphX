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

const char* plugin_get_info() {
    return "D2HAsyncNode|CUDA async device-to-host transfer node|1.0|"
           "plugin_create_cuda_d2h_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&cuda_d2h_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
