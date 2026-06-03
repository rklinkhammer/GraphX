// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/cuda/nodes/LeaseReleaseNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct CudaLeaseReleaseNodePolicy
    : PluginPolicy<graph::gpu::cuda::nodes::LeaseReleaseNode> {
    static constexpr const char* Description =
        "CUDA stub lease release node plugin";
};

using Glue = PluginGlue<graph::gpu::cuda::nodes::LeaseReleaseNode,
                        CudaLeaseReleaseNodePolicy>;
static const NodeFacade cuda_lease_release_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_cuda_lease_release_node() {
    try {
        auto node = std::make_shared<graph::gpu::cuda::nodes::LeaseReleaseNode>();
        return new NodePluginInstance<graph::gpu::cuda::nodes::LeaseReleaseNode>(
            node,
            "LeaseReleaseNode",
            "plugin.gpu.cuda.LeaseReleaseNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "LeaseReleaseNode|CUDA stub lease release node|1.0|"
           "plugin_create_cuda_lease_release_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&cuda_lease_release_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"