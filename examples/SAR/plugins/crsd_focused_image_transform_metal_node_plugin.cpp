// SPDX-License-Identifier: MIT

/**
 * @file crsd_focused_image_transform_metal_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/CrsdFocusedImageTransformMetal.hpp"

using namespace graph;

namespace {

struct CrsdFocusedImageTransformMetalNodePolicy
    : PluginPolicy<sar::CrsdFocusedImageTransformMetalNode> {
    [[maybe_unused]] static constexpr const char* Description =
    "CRSD focused-image Metal execution transform node (experimental, algorithm incomplete)";
};

using Glue = PluginGlue<sar::CrsdFocusedImageTransformMetalNode,
                        CrsdFocusedImageTransformMetalNodePolicy>;
static const NodeFacade crsd_focused_image_transform_metal_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_crsd_focused_image_transform_metal_node() {
    try {
        auto node = std::make_shared<sar::CrsdFocusedImageTransformMetalNode>();
        return new NodePluginInstance<sar::CrsdFocusedImageTransformMetalNode>(
            node,
            "CrsdFocusedImageTransformMetalNode",
            "plugin.CrsdFocusedImageTransformMetalNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "CrsdFocusedImageTransformMetalNode|CRSD focused-image Metal execution transform node (experimental, algorithm incomplete)|1.0|"
           "plugin_create_crsd_focused_image_transform_metal_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&crsd_focused_image_transform_metal_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
