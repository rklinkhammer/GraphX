// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/sycl/nodes/HostEgressSinkNodeSycl.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SyclHostEgressSinkNodePolicy
    : PluginPolicy<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl> {
    static constexpr const char* Description =
        "SYCL host egress sink node plugin";
};

using Glue = PluginGlue<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl,
                        SyclHostEgressSinkNodePolicy>;
static const NodeFacade sycl_host_egress_sink_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_sycl_host_egress_sink_node() {
    try {
        auto node = std::make_shared<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl>();
        return new NodePluginInstance<graph::gpu::sycl::nodes::HostEgressSinkNodeSycl>(
            node,
            "HostEgressSinkNodeSycl",
            "plugin.gpu.sycl.HostEgressSinkNodeSycl");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "HostEgressSinkNodeSycl|SYCL host egress sink node|1.0|"
           "plugin_create_sycl_host_egress_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sycl_host_egress_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
