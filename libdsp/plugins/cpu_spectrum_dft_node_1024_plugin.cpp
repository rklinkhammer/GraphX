/**
 * @file cpu_spectrum_dft_node_1024_plugin.cpp
 * @brief FFT Node 1024 Plugin DSP support.
 *
 * @details Provides plugin registration unit for dynamically loading DSP graph nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 GraphX Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include <memory>
#include <string>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "dsp/CpuSpectrumDftNode.hpp"

using namespace graph;
using dsp::CpuSpectrumDftNode;

// ============================================================================
// Policy specialization
// ============================================================================

struct CpuSpectrumDftNode1024Policy : PluginPolicy<dsp::CpuSpectrumDftNode<float, 1024>> {
    static constexpr const char* Description =
        "FFT DSP processor (1024 samples/packet, batch processing)";

    static bool SetProperty(NodePluginInstance<dsp::CpuSpectrumDftNode<float, 1024>>* inst,
                            const char*, const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<dsp::CpuSpectrumDftNode<float, 1024>, CpuSpectrumDftNode1024Policy>;
static const NodeFacade cpu_spectrum_dft_node_1024_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

/**
 * @brief Plugin create fft node 1024.
 */
void* plugin_create_cpu_spectrum_dft_node_1024() {
    try {
        auto node = std::make_shared<dsp::CpuSpectrumDftNode<float, 1024>>();
        return new NodePluginInstance<dsp::CpuSpectrumDftNode<float, 1024>>(
            node, "CpuSpectrumDftNode", "plugin.CpuSpectrumDftNode.1024");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "CpuSpectrumDftNode<1024>|FFT DSP processor (1024 samples/packet)|1.0|"
           "plugin_create_cpu_spectrum_dft_node_1024|"
#ifdef _LIBCPP_VERSION
           "libc++_v1";
#else
           "libstdc++_v1";
#endif
}

/**
 * @brief Plugin get facade.
 */
NodeFacade* plugin_get_facade() {
    return const_cast<NodeFacade*>(&cpu_spectrum_dft_node_1024_facade);
}

// Phase 4: Plugin API version negotiation
/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;  // Supports PluginLoader v2+ (version negotiation)
}

}
