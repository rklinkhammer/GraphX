#include "dsp/fhss/PerChannelPulseDetectorNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct PerChannelPulseDetectorNodePolicy
    : PluginPolicy<dsp::fhss::PerChannelPulseDetectorNode> {
  static constexpr const char *Description =
      "FHSS per-channel pulse detector GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::PerChannelPulseDetectorNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::PerChannelPulseDetectorNode,
                        PerChannelPulseDetectorNodePolicy>;
static const NodeFacade per_channel_pulse_detector_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_per_channel_pulse_detector_node() {
  try {
    auto node = std::make_shared<dsp::fhss::PerChannelPulseDetectorNode>();
    return new NodePluginInstance<dsp::fhss::PerChannelPulseDetectorNode>(
        node, "PerChannelPulseDetectorNode",
        "plugin.PerChannelPulseDetectorNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "PerChannelPulseDetectorNode|FHSS per-channel pulse detector GraphX "
         "node|1.0|plugin_create_per_channel_pulse_detector_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&per_channel_pulse_detector_node_facade);
}
int plugin_api_version() { return 2; }
}
