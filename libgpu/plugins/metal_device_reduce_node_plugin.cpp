/**
 * @file metal_device_reduce_node_plugin.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/DeviceReduceNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalDeviceReduceNodePolicy : PluginPolicy<graph::gpu::metal::nodes::DeviceReduceNodeMetal> {
    static constexpr const char* Description =
        "Metal device reduce node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::DeviceReduceNodeMetal,
                        MetalDeviceReduceNodePolicy>;
static const NodeFacade metal_device_reduce_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create metal device reduce node.
 */
void* plugin_create_metal_device_reduce_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::DeviceReduceNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::DeviceReduceNodeMetal>(
            node,
            "DeviceReduceNodeMetal",
            "plugin.gpu.metal.DeviceReduceNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "DeviceReduceNodeMetal|Metal device reduce node|1.0|"
           "plugin_create_metal_device_reduce_node|"
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
    return const_cast<NodeFacade*>(&metal_device_reduce_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
