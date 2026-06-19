/**
 * @file dsp_iq_h2d_node_256_plugin.cpp
 * @brief DSP IQ H2D node 256 plugin.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/DspIqH2DNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct DspIqH2DNode256Policy : PluginPolicy<dsp::DspIqH2DNode<256>> {
    static constexpr const char* Description =
        "DSP IQ host-to-device transfer node (256 samples/packet)";

    static bool SetProperty(NodePluginInstance<dsp::DspIqH2DNode<256>>* inst,
                            const char*,
                            const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

using Glue = PluginGlue<dsp::DspIqH2DNode<256>, DspIqH2DNode256Policy>;
static const NodeFacade dsp_iq_h2d_node_256_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_dsp_iq_h2d_node_256() {
    try {
        auto node = std::make_shared<dsp::DspIqH2DNode<256>>();
        return new NodePluginInstance<dsp::DspIqH2DNode<256>>(
            node, "DspIqH2DNode", "plugin.DspIqH2DNode.256");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "DspIqH2DNode<256>|DSP IQ host-to-device transfer node (256 samples/packet)|1.0|"
           "plugin_create_dsp_iq_h2d_node_256|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&dsp_iq_h2d_node_256_facade);
}

int plugin_api_version() {
    return 2;
}

}
