// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/sycl/nodes/H2DAsyncNodeSycl.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SyclH2DAsyncNodePolicy : PluginPolicy<graph::gpu::sycl::nodes::H2DAsyncNodeSycl> {
    static constexpr const char* Description =
        "SYCL async host-to-device transfer node plugin";
};

using Glue =
    PluginGlue<graph::gpu::sycl::nodes::H2DAsyncNodeSycl, SyclH2DAsyncNodePolicy>;
static const NodeFacade sycl_h2d_async_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_sycl_h2d_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::sycl::nodes::H2DAsyncNodeSycl>();

        return new NodePluginInstance<graph::gpu::sycl::nodes::H2DAsyncNodeSycl>(
            node,
            "H2DAsyncNodeSycl",
            "plugin.gpu.sycl.H2DAsyncNodeSycl");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "H2DAsyncNodeSycl|SYCL async host-to-device transfer node|1.0|"
           "plugin_create_sycl_h2d_async_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sycl_h2d_async_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
