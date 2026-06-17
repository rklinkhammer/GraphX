/**
 * @file sycl_d2h_async_node_plugin.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/sycl/nodes/D2HAsyncNodeSycl.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SyclD2HAsyncNodePolicy : PluginPolicy<graph::gpu::sycl::nodes::D2HAsyncNodeSycl> {
    static constexpr const char* Description =
        "SYCL async device-to-host transfer node plugin";
};

using Glue =
    PluginGlue<graph::gpu::sycl::nodes::D2HAsyncNodeSycl, SyclD2HAsyncNodePolicy>;
static const NodeFacade sycl_d2h_async_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create sycl d 2 h async node.
 */
void* plugin_create_sycl_d2h_async_node() {
    try {
        auto node = std::make_shared<graph::gpu::sycl::nodes::D2HAsyncNodeSycl>();

        return new NodePluginInstance<graph::gpu::sycl::nodes::D2HAsyncNodeSycl>(
            node,
            "D2HAsyncNodeSycl",
            "plugin.gpu.sycl.D2HAsyncNodeSycl");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "D2HAsyncNodeSycl|SYCL async device-to-host transfer node|1.0|"
           "plugin_create_sycl_d2h_async_node|"
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
    return const_cast<NodeFacade*>(&sycl_d2h_async_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
