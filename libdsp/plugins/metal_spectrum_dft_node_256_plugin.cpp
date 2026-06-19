/**
 * @file metal_spectrum_dft_node_256_plugin.cpp
 * @brief Metal direct DFT spectrum node 256 plugin.
 */
// MIT License
//
// Copyright (c) 2026 GraphX Contributors

#include "dsp/MetalSpectrumDftNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

#include <log4cxx/logger.h>
#include <memory>

using namespace graph;

struct MetalSpectrumDftNode256Policy : PluginPolicy<dsp::MetalSpectrumDftNode<256>> {
    static constexpr const char* Description =
        "Metal direct DFT spectrum transform (256 samples/packet)";

    static bool SetProperty(NodePluginInstance<dsp::MetalSpectrumDftNode<256>>* inst,
                            const char*,
                            const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

using Glue = PluginGlue<dsp::MetalSpectrumDftNode<256>, MetalSpectrumDftNode256Policy>;
static const NodeFacade metal_spectrum_dft_node_256_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_metal_spectrum_dft_node_256() {
    try {
        auto node = std::make_shared<dsp::MetalSpectrumDftNode<256>>();
        return new NodePluginInstance<dsp::MetalSpectrumDftNode<256>>(
            node, "MetalSpectrumDftNode", "plugin.MetalSpectrumDftNode.256");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "MetalSpectrumDftNode<256>|Metal direct DFT spectrum transform (256 samples/packet)|1.0|"
           "plugin_create_metal_spectrum_dft_node_256|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&metal_spectrum_dft_node_256_facade);
}

int plugin_api_version() {
    return 2;
}

}
