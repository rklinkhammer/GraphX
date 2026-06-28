#include "dsp/fhss/FHSSFixtureFrequencyChannelizerNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct FHSSFixtureFrequencyChannelizerNodePolicy
    : PluginPolicy<dsp::fhss::FHSSFixtureFrequencyChannelizerNode> {
  static constexpr const char *Description =
      "FHSS fixture-only frequency mixer/decimator GraphX node";
  static bool SetProperty(
      NodePluginInstance<dsp::fhss::FHSSFixtureFrequencyChannelizerNode> *inst,
      const char *, const char *) {
    LOG4CXX_TRACE(inst->logger, "No properties supported");
    return true;
  }
};

using Glue =
    PluginGlue<dsp::fhss::FHSSFixtureFrequencyChannelizerNode,
               FHSSFixtureFrequencyChannelizerNodePolicy>;
static const NodeFacade fhss_fixture_frequency_channelizer_node_facade =
    Glue::MakeFacade();

extern "C" {
void *plugin_create_fhss_fixture_frequency_channelizer_node() {
  try {
    auto node =
        std::make_shared<dsp::fhss::FHSSFixtureFrequencyChannelizerNode>();
    return new NodePluginInstance<
        dsp::fhss::FHSSFixtureFrequencyChannelizerNode>(
        node, "FHSSFixtureFrequencyChannelizerNode",
        "plugin.FHSSFixtureFrequencyChannelizerNode");
  } catch (...) {
    return nullptr;
  }
}
const char *plugin_get_info() {
  return "FHSSFixtureFrequencyChannelizerNode|FHSS fixture-only frequency "
         "mixer/decimator GraphX node|1.0|"
         "plugin_create_fhss_fixture_frequency_channelizer_node|"
#ifdef _LIBCPP_VERSION
         "libc++_v1";
#else
         "libstdc++_v1";
#endif
}
NodeFacade *plugin_get_facade() {
  return const_cast<NodeFacade *>(
      &fhss_fixture_frequency_channelizer_node_facade);
}
int plugin_api_version() { return 2; }
}
