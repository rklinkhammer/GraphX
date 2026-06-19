/**
 * @file dsp_magnitude_d2h_node_256_plugin.cpp
 * @brief DSP magnitude D2H node 256 plugin.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/DspMagnitudeD2HNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct DspMagnitudeD2HNode256Policy : PluginPolicy<dsp::DspMagnitudeD2HNode<256>> {
    static constexpr const char* Description =
        "DSP magnitude device-to-host transfer node (256 samples/packet)";

    static bool SetProperty(NodePluginInstance<dsp::DspMagnitudeD2HNode<256>>* inst,
                            const char*,
                            const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

using Glue = PluginGlue<dsp::DspMagnitudeD2HNode<256>, DspMagnitudeD2HNode256Policy>;
static const NodeFacade dsp_magnitude_d2h_node_256_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_dsp_magnitude_d2h_node_256() {
    try {
        auto node = std::make_shared<dsp::DspMagnitudeD2HNode<256>>();
        return new NodePluginInstance<dsp::DspMagnitudeD2HNode<256>>(
            node, "DspMagnitudeD2HNode", "plugin.DspMagnitudeD2HNode.256");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "DspMagnitudeD2HNode<256>|DSP magnitude device-to-host transfer node (256 samples/packet)|1.0|"
           "plugin_create_dsp_magnitude_d2h_node_256|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&dsp_magnitude_d2h_node_256_facade);
}

int plugin_api_version() {
    return 2;
}

}
