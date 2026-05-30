/**
 * @file sine_iq_source_plugin.cpp
 * @brief SineIQSourceNode as a dynamically loadable plugin.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "test/SDRTestNodes.hpp"

using namespace graph;
using test::sdr::SineIQSourceNode;

struct SineIQSourceNodePolicy : PluginPolicy<SineIQSourceNode> {
    static constexpr const char* Description = "SDR sine wave IQ packet generator";
};

using Glue = PluginGlue<SineIQSourceNode, SineIQSourceNodePolicy>;
static const NodeFacade sine_iq_source_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_sine_iq_source() {
    try {
        auto node = std::make_shared<SineIQSourceNode>();
        return new NodePluginInstance<SineIQSourceNode>(node, "SineIQSourceNode", "plugin.SineIQSourceNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SineIQSourceNode|SDR sine wave IQ packet generator|1.0|"
           "plugin_create_sine_iq_source|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sine_iq_source_facade);
}

int plugin_api_version() {
    return 2;
}

}
