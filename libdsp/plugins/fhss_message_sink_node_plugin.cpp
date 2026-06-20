#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSMessageSinkNodePolicy
    : PluginPolicy<dsp::fhss::FHSSMessageSinkNode> {
  static constexpr const char *Description =
      "FHSS message sink GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSMessageSinkNode> *inst, const char *,
      const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue =
    PluginGlue<dsp::fhss::FHSSMessageSinkNode, FHSSMessageSinkNodePolicy>;
static const NodeFacade fhss_message_sink_node_facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_message_sink_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSMessageSinkNode>();
    return new NodePluginInstance<dsp::fhss::FHSSMessageSinkNode>(
        node, "FHSSMessageSinkNode", "plugin.FHSSMessageSinkNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSMessageSinkNode|FHSS message sink GraphX node|1.0|"
         "plugin_create_fhss_message_sink_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_message_sink_node_facade);
}
int plugin_api_version() { return 2; }
}
