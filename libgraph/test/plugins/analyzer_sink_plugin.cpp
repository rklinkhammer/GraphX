/**
 * @file analyzer_sink_plugin.cpp
 * @brief AnalyzerSinkNode as a dynamically loadable plugin.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "test/SDRTestNodes.hpp"

using namespace graph;
using test::sdr::AnalyzerSinkNode;

struct AnalyzerSinkNodePolicy : PluginPolicy<AnalyzerSinkNode> {
    static constexpr const char* Description = "SDR analyzer sink for power spectra";
};

using Glue = PluginGlue<AnalyzerSinkNode, AnalyzerSinkNodePolicy>;
static const NodeFacade analyzer_sink_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_analyzer_sink() {
    try {
        auto node = std::make_shared<AnalyzerSinkNode>();
        return new NodePluginInstance<AnalyzerSinkNode>(node, "AnalyzerSinkNode", "plugin.AnalyzerSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "AnalyzerSinkNode|SDR analyzer sink for power spectra|1.0|"
           "plugin_create_analyzer_sink|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&analyzer_sink_facade);
}

int plugin_api_version() {
    return 2;
}

}
