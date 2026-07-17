#include "dsp/fhss/FHSSAcquisitionPulseDetectorNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSAcquisitionPulseDetectorNodePolicy
    : PluginPolicy<dsp::fhss::FHSSAcquisitionPulseDetectorNode> {
  static constexpr const char *Description =
      "FHSS Phase 2 bounded-capture evidence-driven acquisition detector";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSAcquisitionPulseDetectorNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSAcquisitionPulseDetectorNode,
                        FHSSAcquisitionPulseDetectorNodePolicy>;
static const NodeFacade facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_acquisition_pulse_detector_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSAcquisitionPulseDetectorNode>();
    return new NodePluginInstance<dsp::fhss::FHSSAcquisitionPulseDetectorNode>(
        node, "FHSSAcquisitionPulseDetectorNode",
        "plugin.FHSSAcquisitionPulseDetectorNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSAcquisitionPulseDetectorNode|FHSS Phase 2 bounded-capture "
         "evidence-driven acquisition detector|1.0|"
         "plugin_create_fhss_acquisition_pulse_detector_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() { return const_cast<NodeFacade *>(&facade); }
int plugin_api_version() { return 2; }
}
