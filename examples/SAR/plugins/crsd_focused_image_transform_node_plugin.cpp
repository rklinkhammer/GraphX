// SPDX-License-Identifier: MIT

/**
 * @file crsd_focused_image_transform_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/CrsdFocusedImageTransformNode.hpp"

using namespace graph;

namespace {

struct CrsdFocusedImageTransformNodePolicy
    : PluginPolicy<sar::CrsdFocusedImageTransformNode> {
    [[maybe_unused]] static constexpr const char* Description =
        "CRSD focused-image CPU backprojection transform node";
};

using Glue = PluginGlue<sar::CrsdFocusedImageTransformNode,
                        CrsdFocusedImageTransformNodePolicy>;
static const NodeFacade crsd_focused_image_transform_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_crsd_focused_image_transform_node() {
    try {
        auto node = std::make_shared<sar::CrsdFocusedImageTransformNode>();
        return new NodePluginInstance<sar::CrsdFocusedImageTransformNode>(
            node,
            "CrsdFocusedImageTransformNode",
            "plugin.CrsdFocusedImageTransformNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "CrsdFocusedImageTransformNode|CRSD focused-image CPU backprojection transform node|1.0|"
           "plugin_create_crsd_focused_image_transform_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&crsd_focused_image_transform_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
