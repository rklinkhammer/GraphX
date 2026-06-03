// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/cuda/nodes/H2DAsyncNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct CudaH2DAsyncNodePolicy : PluginPolicy<graph::gpu::cuda::nodes::H2DAsyncNode> {
    static constexpr const char* Description =
        "CUDA async host-to-device transfer node plugin";
};

using Glue =
    PluginGlue<graph::gpu::cuda::nodes::H2DAsyncNode, CudaH2DAsyncNodePolicy>;
static const NodeFacade cuda_h2d_async_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_cuda_h2d_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::cuda::nodes::H2DAsyncNode>();

        return new NodePluginInstance<graph::gpu::cuda::nodes::H2DAsyncNode>(
            node,
            "H2DAsyncNode",
            "plugin.gpu.cuda.H2DAsyncNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "H2DAsyncNode|CUDA async host-to-device transfer node|1.0|"
           "plugin_create_cuda_h2d_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&cuda_h2d_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
