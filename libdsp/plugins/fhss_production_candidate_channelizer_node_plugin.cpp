#include "dsp/fhss/FHSSProductionCandidateChannelizerNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSProductionCandidateChannelizerNodePolicy
    : PluginPolicy<dsp::fhss::FHSSProductionCandidateChannelizerNode> {
  static constexpr const char *Description =
      "FHSS Phase 2 deterministic CPU FIR production-candidate channelizer";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSProductionCandidateChannelizerNode>
          *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSProductionCandidateChannelizerNode,
                        FHSSProductionCandidateChannelizerNodePolicy>;
static const NodeFacade facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_production_candidate_channelizer_node() {
  try {
    auto node =
        std::make_shared<dsp::fhss::FHSSProductionCandidateChannelizerNode>();
    return new NodePluginInstance<
        dsp::fhss::FHSSProductionCandidateChannelizerNode>(
        node, "FHSSProductionCandidateChannelizerNode",
        "plugin.FHSSProductionCandidateChannelizerNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSProductionCandidateChannelizerNode|FHSS Phase 2 deterministic "
         "CPU FIR production-candidate channelizer|1.0|"
         "plugin_create_fhss_production_candidate_channelizer_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() { return const_cast<NodeFacade *>(&facade); }
int plugin_api_version() { return 2; }
}
