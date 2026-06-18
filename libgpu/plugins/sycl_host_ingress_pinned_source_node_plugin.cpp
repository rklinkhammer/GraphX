/**
 * @file sycl_host_ingress_pinned_source_node_plugin.cpp
 * @brief SYCL Host Ingress Pinned Source Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/sycl/nodes/HostIngressPinnedSourceNodeSycl.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SyclHostIngressPinnedSourceNodePolicy
    : PluginPolicy<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl> {
    static constexpr const char* Description =
        "SYCL host ingress pinned source node plugin";
};

using Glue = PluginGlue<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl,
                        SyclHostIngressPinnedSourceNodePolicy>;
static const NodeFacade sycl_host_ingress_pinned_source_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create sycl host ingress pinned source node.
 */
void* plugin_create_sycl_host_ingress_pinned_source_node() {
    try {
        auto node = std::make_shared<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl>();

        return new NodePluginInstance<graph::gpu::sycl::nodes::HostIngressPinnedSourceNodeSycl>(
            node,
            "HostIngressPinnedSourceNodeSycl",
            "plugin.gpu.sycl.HostIngressPinnedSourceNodeSycl");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "HostIngressPinnedSourceNodeSycl|SYCL host ingress pinned source node|1.0|"
           "plugin_create_sycl_host_ingress_pinned_source_node|"
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
    return const_cast<NodeFacade*>(&sycl_host_ingress_pinned_source_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
