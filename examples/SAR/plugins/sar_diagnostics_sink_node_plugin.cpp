#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"

using namespace graph;

namespace {

struct SarDiagnosticsSinkNodePolicy : PluginPolicy<sar::SarDiagnosticsSinkNode> {};

using Glue = PluginGlue<sar::SarDiagnosticsSinkNode, SarDiagnosticsSinkNodePolicy>;
static const NodeFacade sar_diagnostics_sink_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_sar_diagnostics_sink_node() {
    try {
        auto node = std::make_shared<sar::SarDiagnosticsSinkNode>();
        return new NodePluginInstance<sar::SarDiagnosticsSinkNode>(
            node,
            "SarDiagnosticsSinkNode",
            "plugin.SarDiagnosticsSinkNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "SarDiagnosticsSinkNode|SAR diagnostics sink node|1.0|"
           "plugin_create_sar_diagnostics_sink_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&sar_diagnostics_sink_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
