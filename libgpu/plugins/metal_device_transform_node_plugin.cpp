// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/DeviceTransformNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalDeviceTransformNodePolicy
    : PluginPolicy<graph::gpu::metal::nodes::DeviceTransformNodeMetal> {
    static constexpr const char* Description =
        "Metal device transform node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::DeviceTransformNodeMetal,
                        MetalDeviceTransformNodePolicy>;
static const NodeFacade metal_device_transform_node_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_device_transform_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::DeviceTransformNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::DeviceTransformNodeMetal>(
            node,
            "DeviceTransformNodeMetal",
            "plugin.gpu.metal.DeviceTransformNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "DeviceTransformNodeMetal|Metal device transform node|1.0|"
           "plugin_create_metal_device_transform_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_device_transform_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"