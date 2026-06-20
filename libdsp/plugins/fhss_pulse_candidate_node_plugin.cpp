#include "dsp/fhss/FHSSPulseCandidateNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSPulseCandidateNodePolicy
    : PluginPolicy<dsp::fhss::FHSSPulseCandidateNode> {
  static constexpr const char *Description =
      "FHSS pulse candidate boundary GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSPulseCandidateNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSPulseCandidateNode,
                        FHSSPulseCandidateNodePolicy>;
static const NodeFacade fhss_pulse_candidate_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_pulse_candidate_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSPulseCandidateNode>();
    return new NodePluginInstance<dsp::fhss::FHSSPulseCandidateNode>(
        node, "FHSSPulseCandidateNode", "plugin.FHSSPulseCandidateNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSPulseCandidateNode|FHSS pulse candidate boundary GraphX node|1.0|"
         "plugin_create_fhss_pulse_candidate_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_pulse_candidate_node_facade);
}
int plugin_api_version() { return 2; }
}
