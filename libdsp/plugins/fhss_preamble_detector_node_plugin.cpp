#include "dsp/fhss/FHSSPreambleDetectorNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSPreambleDetectorNodePolicy
    : PluginPolicy<dsp::fhss::FHSSPreambleDetectorNode> {
  static constexpr const char *Description =
      "FHSS preamble detector GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSPreambleDetectorNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSPreambleDetectorNode,
                        FHSSPreambleDetectorNodePolicy>;
static const NodeFacade fhss_preamble_detector_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_preamble_detector_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSPreambleDetectorNode>();
    return new NodePluginInstance<dsp::fhss::FHSSPreambleDetectorNode>(
        node, "FHSSPreambleDetectorNode", "plugin.FHSSPreambleDetectorNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSPreambleDetectorNode|FHSS preamble detector GraphX node|1.0|"
         "plugin_create_fhss_preamble_detector_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_preamble_detector_node_facade);
}
int plugin_api_version() { return 2; }
}
