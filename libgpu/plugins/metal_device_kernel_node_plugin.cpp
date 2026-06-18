/**
 * @file metal_device_kernel_node_plugin.cpp
 * @brief Metal Device Kernel Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/DeviceKernelNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalDeviceKernelNodePolicy
    : PluginPolicy<graph::gpu::metal::nodes::DeviceKernelNodeMetal> {
    static constexpr const char* Description =
        "Metal descriptor-driven device kernel node plugin";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::DeviceKernelNodeMetal,
                        MetalDeviceKernelNodePolicy>;
static const NodeFacade metal_device_kernel_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create metal device kernel node.
 */
void* plugin_create_metal_device_kernel_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::DeviceKernelNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::DeviceKernelNodeMetal>(
            node,
            "DeviceKernelNodeMetal",
            "plugin.gpu.metal.DeviceKernelNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "DeviceKernelNodeMetal|Metal descriptor-driven device kernel node|1.0|"
           "plugin_create_metal_device_kernel_node|"
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
    return const_cast<NodeFacade*>(&metal_device_kernel_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
