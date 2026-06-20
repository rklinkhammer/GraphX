#include "dsp/fhss/FHSSPulseMergeNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSPulseMergeNodePolicy
    : PluginPolicy<dsp::fhss::FHSSPulseMergeNode> {
  static constexpr const char *Description =
      "FHSS pulse merge GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSPulseMergeNode> *inst, const char *,
      const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue =
    PluginGlue<dsp::fhss::FHSSPulseMergeNode, FHSSPulseMergeNodePolicy>;
static const NodeFacade fhss_pulse_merge_node_facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_pulse_merge_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSPulseMergeNode>();
    return new NodePluginInstance<dsp::fhss::FHSSPulseMergeNode>(
        node, "FHSSPulseMergeNode", "plugin.FHSSPulseMergeNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSPulseMergeNode|FHSS pulse merge GraphX node|1.0|"
         "plugin_create_fhss_pulse_merge_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_pulse_merge_node_facade);
}
int plugin_api_version() { return 2; }
}
