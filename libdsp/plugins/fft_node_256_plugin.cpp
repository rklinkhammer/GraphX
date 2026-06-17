/**
 * @file fft_node_256_plugin.cpp
 * @brief GraphX source file.
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

/**
 * @file fft_node_256_plugin.cpp
 * @brief FFTNode<float, 256> as a dynamically-loadable plugin
 *
 * This file exposes FFTNode (a DSP FFT processor) with 256 samples per packet
 * as a dynamically-loadable plugin using the NodeFacade interface.
 *
 * Compilation (from workspace root):
 *   mkdir -p build/plugins
 *   cd build
 *   cmake ..
 *   make fft_node_256
 *
 * This produces: build/plugins/libfft_node_256.so
 */

#include <memory>
#include <string>
#include <log4cxx/logger.h>
#include "plugins/NodePluginTemplate.hpp"
#include "dsp/FFTNode.hpp"

using namespace graph;
using dsp::FFTNode;

// ============================================================================
// Policy specialization
// ============================================================================

struct FFTNode256Policy : PluginPolicy<dsp::FFTNode<float, 256>> {
    static constexpr const char* Description =
        "FFT DSP processor (256 samples/packet, standard latency)";

    static bool SetProperty(NodePluginInstance<dsp::FFTNode<float, 256>>* inst,
                            const char*, const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

// ============================================================================
// Facade
// ============================================================================

using Glue = PluginGlue<dsp::FFTNode<float, 256>, FFTNode256Policy>;
static const NodeFacade fft_node_256_facade = Glue::MakeFacade();

// ============================================================================
// C exports
// ============================================================================

extern "C" {

/**
 * @brief Plugin create fft node 256.
 */
void* plugin_create_fft_node_256() {
    try {
        auto node = std::make_shared<dsp::FFTNode<float, 256>>();
        return new NodePluginInstance<dsp::FFTNode<float, 256>>(
            node, "FFTNode", "plugin.FFTNode.256");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "FFTNode<256>|FFT DSP processor (256 samples/packet)|1.0|"
           "plugin_create_fft_node_256|"
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
    return const_cast<NodeFacade*>(&fft_node_256_facade);
}

// Phase 4: Plugin API version negotiation
/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;  // Supports PluginLoader v2+ (version negotiation)
}

}
