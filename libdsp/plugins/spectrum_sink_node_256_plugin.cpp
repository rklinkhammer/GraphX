/**
 * @file spectrum_sink_node_256_plugin.cpp
 * @brief Spectrum Sink Node 256 Plugin DSP support.
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

#include "dsp/SpectrumSinkNode.hpp"
#include "plugins/NodePluginTemplate.hpp"

using namespace graph;

struct SpectrumSinkNode256Policy : PluginPolicy<dsp::SpectrumSinkNode<float, 256>> {
    static constexpr const char* Description =
        "Spectrum sink DSP analyzer (256 samples/packet, standard latency)";

    static bool SetProperty(NodePluginInstance<dsp::SpectrumSinkNode<float, 256>>* inst,
                            const char*, const char*) {
        LOG4CXX_TRACE(inst->logger, "No properties supported");
        return true;
    }
};

using Glue = PluginGlue<dsp::SpectrumSinkNode<float, 256>, SpectrumSinkNode256Policy>;
static const NodeFacade spectrum_sink_node_256_facade = Glue::MakeFacade();

extern "C" {

/**
 * @brief Plugin create spectrum sink node 256.
 */
void* plugin_create_spectrum_sink_node_256() {
    try {
        auto node = std::make_shared<dsp::SpectrumSinkNode<float, 256>>();
        return new NodePluginInstance<dsp::SpectrumSinkNode<float, 256>>(
            node, "SpectrumSinkNode", "plugin.SpectrumSinkNode.256");
    } catch (...) {
        return nullptr;
    }
}

/**
 * @brief Plugin get info.
 */
const char* plugin_get_info() {
    return "SpectrumSinkNode<256>|Spectrum sink DSP analyzer (256 samples/packet)|1.0|"
           "plugin_create_spectrum_sink_node_256|"
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
    return const_cast<NodeFacade*>(&spectrum_sink_node_256_facade);
}

/**
 * @brief Plugin api version.
 */
int plugin_api_version() {
    return 2;
}

}
