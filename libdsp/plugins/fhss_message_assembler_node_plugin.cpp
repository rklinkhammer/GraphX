#include "dsp/fhss/FHSSMessageAssemblerNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSMessageAssemblerNodePolicy
    : PluginPolicy<dsp::fhss::FHSSMessageAssemblerNode> {
  static constexpr const char *Description =
      "FHSS message assembler GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSMessageAssemblerNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSMessageAssemblerNode,
                        FHSSMessageAssemblerNodePolicy>;
static const NodeFacade fhss_message_assembler_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_message_assembler_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSMessageAssemblerNode>();
    return new NodePluginInstance<dsp::fhss::FHSSMessageAssemblerNode>(
        node, "FHSSMessageAssemblerNode", "plugin.FHSSMessageAssemblerNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSMessageAssemblerNode|FHSS message assembler GraphX node|1.0|"
         "plugin_create_fhss_message_assembler_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_message_assembler_node_facade);
}
int plugin_api_version() { return 2; }
}
