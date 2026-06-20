#include "dsp/fhss/CPSMViterbiDecoderNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct CPSMViterbiDecoderNodePolicy
    : PluginPolicy<dsp::fhss::CPSMViterbiDecoderNode> {
  static constexpr const char *Description =
      "CPSM Viterbi decoder GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::CPSMViterbiDecoderNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::CPSMViterbiDecoderNode,
                        CPSMViterbiDecoderNodePolicy>;
static const NodeFacade cpsm_viterbi_decoder_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_cpsm_viterbi_decoder_node() {
  try {
    auto node = std::make_shared<dsp::fhss::CPSMViterbiDecoderNode>();
    return new NodePluginInstance<dsp::fhss::CPSMViterbiDecoderNode>(
        node, "CPSMViterbiDecoderNode", "plugin.CPSMViterbiDecoderNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "CPSMViterbiDecoderNode|CPSM Viterbi decoder GraphX node|1.0|"
         "plugin_create_cpsm_viterbi_decoder_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&cpsm_viterbi_decoder_node_facade);
}
int plugin_api_version() { return 2; }
}
