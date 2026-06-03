// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/sycl/nodes/LeaseReleaseNodeSycl.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SyclLeaseReleaseNodePolicy
    : PluginPolicy<graph::gpu::sycl::nodes::LeaseReleaseNodeSycl> {
    static constexpr const char* Description =
        "SYCL stub lease release node plugin";
};

using Glue = PluginGlue<graph::gpu::sycl::nodes::LeaseReleaseNodeSycl,
                        SyclLeaseReleaseNodePolicy>;
static const NodeFacade sycl_lease_release_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_sycl_lease_release_node() {
    try {
        auto node = std::make_shared<graph::gpu::sycl::nodes::LeaseReleaseNodeSycl>();
        return new NodePluginInstance<graph::gpu::sycl::nodes::LeaseReleaseNodeSycl>(
            node,
            "LeaseReleaseNodeSycl",
            "plugin.gpu.sycl.LeaseReleaseNodeSycl");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "LeaseReleaseNodeSycl|SYCL stub lease release node|1.0|"
           "plugin_create_sycl_lease_release_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sycl_lease_release_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"