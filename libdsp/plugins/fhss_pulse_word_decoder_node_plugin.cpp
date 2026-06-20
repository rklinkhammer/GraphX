#include "dsp/fhss/FHSSPulseWordDecoderNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSPulseWordDecoderNodePolicy
    : PluginPolicy<dsp::fhss::FHSSPulseWordDecoderNode> {
  static constexpr const char *Description =
      "FHSS pulse word decoder GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSPulseWordDecoderNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::FHSSPulseWordDecoderNode,
                        FHSSPulseWordDecoderNodePolicy>;
static const NodeFacade fhss_pulse_word_decoder_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_pulse_word_decoder_node() {
  try {
    auto node = std::make_shared<dsp::fhss::FHSSPulseWordDecoderNode>();
    return new NodePluginInstance<dsp::fhss::FHSSPulseWordDecoderNode>(
        node, "FHSSPulseWordDecoderNode", "plugin.FHSSPulseWordDecoderNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSPulseWordDecoderNode|FHSS pulse word decoder GraphX node|1.0|"
         "plugin_create_fhss_pulse_word_decoder_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&fhss_pulse_word_decoder_node_facade);
}
int plugin_api_version() { return 2; }
}
