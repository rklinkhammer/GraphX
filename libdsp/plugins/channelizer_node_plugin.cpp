#include "dsp/fhss/ChannelizerNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct ChannelizerNodePolicy : PluginPolicy<dsp::fhss::ChannelizerNode> {
  static constexpr const char *Description =
      "FHSS frequency-parallel CPU channelizer GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::ChannelizerNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue = PluginGlue<dsp::fhss::ChannelizerNode, ChannelizerNodePolicy>;
static const NodeFacade channelizer_node_facade = Glue::MakeFacade();

extern "C" {
void *plugin_create_channelizer_node() {
  try {
    auto node = std::make_shared<dsp::fhss::ChannelizerNode>();
    return new NodePluginInstance<dsp::fhss::ChannelizerNode>(
        node, "ChannelizerNode", "plugin.ChannelizerNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "ChannelizerNode|FHSS frequency-parallel CPU channelizer GraphX "
         "node|1.0|plugin_create_channelizer_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(&channelizer_node_facade);
}
int plugin_api_version() { return 2; }
}

