// MIT License
//
// Copyright (c) 2025 graphlib contributors

/**
 * @file optional_config_test_node_plugin.cpp
 * @brief OptionalConfigTestNode as a dynamically-loadable plugin
 */

#include <memory>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "test/AdvancedTestNodes.hpp"

using namespace graph;
using namespace test;

struct OptionalConfigTestNodePolicy : PluginPolicy<OptionalConfigTestNode> {
    static constexpr const char* Description =
        "Configurable test node with no required config fields";
};

using Glue = PluginGlue<OptionalConfigTestNode, OptionalConfigTestNodePolicy>;
static const NodeFacade optional_config_test_node_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create optional config test node.
 */
void* plugin_create_optional_config_test_node() {
    try {
        auto node = std::make_shared<OptionalConfigTestNode>();
        return new NodePluginInstance<OptionalConfigTestNode>(
            node, "OptionalConfigTestNode", "plugin.OptionalConfigTestNode");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "OptionalConfigTestNode|Optional configurable test node|1.0|"
           "plugin_create_optional_config_test_node|"
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
    return const_cast<NodeFacade*>(&optional_config_test_node_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

}  // extern "C"
