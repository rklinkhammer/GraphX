/**
 * @file metal_collective_reduce_node_plugin.cpp
 * @brief Metal Collective Reduce Node Plugin GPU acceleration support.
 *
 * @details Provides GPU plugin registration unit for dynamic graph-node loading. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include <memory>

#include "gpu/metal/nodes/CollectiveReduceNodeMetal.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct MetalCollectiveReduceNodePolicy
    : PluginPolicy<graph::gpu::metal::nodes::CollectiveReduceNodeMetal> {
    static constexpr const char* Description =
    "Metal collective reduce node plugin (runtime unsupported)";
};

using Glue = PluginGlue<graph::gpu::metal::nodes::CollectiveReduceNodeMetal,
                        MetalCollectiveReduceNodePolicy>;
static const NodeFacade metal_collective_reduce_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create metal collective reduce node.
 */
void* plugin_create_metal_collective_reduce_node() {
    try {
        auto node = std::make_shared<graph::gpu::metal::nodes::CollectiveReduceNodeMetal>();

        return new NodePluginInstance<graph::gpu::metal::nodes::CollectiveReduceNodeMetal>(
            node,
            "CollectiveReduceNodeMetal",
            "plugin.gpu.metal.CollectiveReduceNodeMetal");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "CollectiveReduceNodeMetal|Metal collective reduce node (runtime unsupported)|1.0|"
           "plugin_create_metal_collective_reduce_node|"
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
    return const_cast<NodeFacade*>(&metal_collective_reduce_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

} // extern "C"
