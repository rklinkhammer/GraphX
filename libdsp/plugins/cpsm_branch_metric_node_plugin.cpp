#include "dsp/fhss/CPSMBranchMetricNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct CPSMBranchMetricNodePolicy
    : PluginPolicy<dsp::fhss::CPSMBranchMetricNode> {
  static constexpr const char *Description =
      "CPSM branch metric GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::CPSMBranchMetricNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue =
    PluginGlue<dsp::fhss::CPSMBranchMetricNode, CPSMBranchMetricNodePolicy>;
static const NodeFacade cpsm_branch_metric_node_facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_cpsm_branch_metric_node() {
  try {
    auto node = std::make_shared<dsp::fhss::CPSMBranchMetricNode>();
    return new NodePluginInstance<dsp::fhss::CPSMBranchMetricNode>(
        node, "CPSMBranchMetricNode", "plugin.CPSMBranchMetricNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "CPSMBranchMetricNode|CPSM branch metric GraphX node|1.0|"
         "plugin_create_cpsm_branch_metric_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&cpsm_branch_metric_node_facade);
}
int plugin_api_version() { return 2; }
}
