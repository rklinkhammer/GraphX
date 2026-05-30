/**
 * @file fft_power_spectrum_plugin.cpp
 * @brief FFTPowerSpectrumNode as a dynamically loadable plugin.
 */

#include <memory>

#include "plugins/NodePluginTemplate.hpp"
#include "test/SDRTestNodes.hpp"

using namespace graph;
using test::sdr::FFTPowerSpectrumNode;

struct FFTPowerSpectrumNodePolicy : PluginPolicy<FFTPowerSpectrumNode> {
    static constexpr const char* Description = "SDR FFT processor producing a power spectrum";
};

using Glue = PluginGlue<FFTPowerSpectrumNode, FFTPowerSpectrumNodePolicy>;
static const NodeFacade fft_power_spectrum_facade = Glue::MakeFacade();

extern "C" {

void* plugin_create_fft_power_spectrum() {
    try {
        auto node = std::make_shared<FFTPowerSpectrumNode>();
        return new NodePluginInstance<FFTPowerSpectrumNode>(
            node, "FFTPowerSpectrumNode", "plugin.FFTPowerSpectrumNode");
    } catch (...) {
        return nullptr;
    }
}

const char* plugin_get_info() {
    return "FFTPowerSpectrumNode|SDR FFT processor producing a power spectrum|1.0|"
           "plugin_create_fft_power_spectrum|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&fft_power_spectrum_facade);
}

int plugin_api_version() {
    return 2;
}

}
