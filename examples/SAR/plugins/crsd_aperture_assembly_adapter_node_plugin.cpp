#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "../include/sar/CrsdApertureAssemblyAdapterNode.hpp"

using namespace graph;

namespace {

struct CrsdApertureAssemblyAdapterNodePolicy : PluginPolicy<sar::CrsdApertureAssemblyAdapterNode> {
    [[maybe_unused]] static constexpr const char* Description =
        "CRSD aperture assembly adapter node";
};

using Glue = PluginGlue<sar::CrsdApertureAssemblyAdapterNode, CrsdApertureAssemblyAdapterNodePolicy>;
static const NodeFacade crsd_aperture_assembly_adapter_node_facade = Glue::MakeFacade();

} // namespace

extern "C" {

void* plugin_create_crsd_aperture_assembly_adapter_node() {
    try {
        auto node = std::make_shared<sar::CrsdApertureAssemblyAdapterNode>();
        return new NodePluginInstance<sar::CrsdApertureAssemblyAdapterNode>(
            node,
            "CrsdApertureAssemblyAdapterNode",
            "plugin.CrsdApertureAssemblyAdapterNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "CrsdApertureAssemblyAdapterNode|CRSD aperture assembly adapter node|1.0|"
           "plugin_create_crsd_aperture_assembly_adapter_node|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&crsd_aperture_assembly_adapter_node_facade);
}

int plugin_api_version() {
    return 2;
}

} // extern "C"
