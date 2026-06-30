// SPDX-License-Identifier: MIT

/**
 * @file main.cpp
 * @brief GraphX source file.
 */

#include <graph/Message.hpp>
#include <dsp/CpuSpectrumDftNode.hpp>

int main() {
    using MessageType = graph::message::Message;
    using SpectrumNodeType = dsp::CpuSpectrumDftNode<float, 256>;
    static_assert(sizeof(MessageType) > 0);
    static_assert(sizeof(SpectrumNodeType) > 0);
    return 0;
}
