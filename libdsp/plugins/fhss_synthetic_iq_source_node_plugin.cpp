#include "dsp/fhss/FHSSSyntheticIqSourceNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSSyntheticIqSourceNodePolicy
    : PluginPolicy<dsp::fhss::FHSSSyntheticIqSourceNode> {
  static constexpr const char *Description =
      "FHSS synthetic IQ source GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSSyntheticIqSourceNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSSyntheticIqSourceNode,
                        FHSSSyntheticIqSourceNodePolicy>;
static const NodeFacade fhss_synthetic_iq_source_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_synthetic_iq_source_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSSyntheticIqSourceNode>();
    return new NodePluginInstance<dsp::fhss::FHSSSyntheticIqSourceNode>(
        node, "FHSSSyntheticIqSourceNode",
        "plugin.FHSSSyntheticIqSourceNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSSyntheticIqSourceNode|FHSS synthetic IQ source GraphX node|1.0|"
         "plugin_create_fhss_synthetic_iq_source_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_synthetic_iq_source_node_facade);
}
int plugin_api_version() { return 2; }
}
