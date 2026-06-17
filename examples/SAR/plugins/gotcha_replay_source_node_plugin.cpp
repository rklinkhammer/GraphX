// SPDX-License-Identifier: MIT

/**
 * @file gotcha_replay_source_node_plugin.cpp
 * @brief GraphX source file.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/GotchaReplaySourceNode.hpp"

using namespace graph;

namespace {

struct GotchaReplaySourceNodePolicy : PluginPolicy<sar::GotchaReplaySourceNode> {
    [[maybe_unused]] static constexpr const char* Description =
        "SAR Gotcha replay source node";
};

using Glue = PluginGlue<sar::GotchaReplaySourceNode, GotchaReplaySourceNodePolicy>;
static const NodeFacade gotcha_replay_source_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_gotcha_replay_source_node() {
    try {
        auto node = std::make_shared<sar::GotchaReplaySourceNode>();
        return new NodePluginInstance<sar::GotchaReplaySourceNode>(
            node,
            "GotchaReplaySourceNode",
            "plugin.GotchaReplaySourceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "GotchaReplaySourceNode|SAR Gotcha replay source node|1.0|"
           "plugin_create_gotcha_replay_source_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&gotcha_replay_source_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
