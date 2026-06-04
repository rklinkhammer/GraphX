// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/QueueSyncNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalQueueSyncNodePolicy : PluginPolicy<graph::gpu::metal::nodes::QueueSyncNodeMetal> {
    static constexpr const char* Description =
        "Metal queue sync node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::QueueSyncNodeMetal,
                        MetalQueueSyncNodePolicy>;
static const NodeFacade metal_queue_sync_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_queue_sync_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::QueueSyncNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::QueueSyncNodeMetal>(
            node,
            "QueueSyncNodeMetal",
            "plugin.gpu.metal.QueueSyncNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "QueueSyncNodeMetal|Metal queue sync node|1.0|"
           "plugin_create_metal_queue_sync_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_queue_sync_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"