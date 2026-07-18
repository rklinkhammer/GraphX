#include "dsp/fhss/FHSSBinaryIqFileSourceNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSBinaryIqFileSourceNodePolicy
    : PluginPolicy<dsp::fhss::FHSSBinaryIqFileSourceNode> {
  static constexpr const char *Description =
      "FHSS binary IQ file source GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSBinaryIqFileSourceNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSBinaryIqFileSourceNode,
                        FHSSBinaryIqFileSourceNodePolicy>;
static const NodeFacade fhss_binary_iq_file_source_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_binary_iq_file_source_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSBinaryIqFileSourceNode>();
    return new NodePluginInstance<dsp::fhss::FHSSBinaryIqFileSourceNode>(
        node, "FHSSBinaryIqFileSourceNode",
        "plugin.FHSSBinaryIqFileSourceNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSBinaryIqFileSourceNode|FHSS binary IQ file source GraphX node|"
         "1.0|plugin_create_fhss_binary_iq_file_source_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_binary_iq_file_source_node_facade);
}
int plugin_api_version() { return 2; }
}
