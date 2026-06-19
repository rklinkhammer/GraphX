// SPDX-License-Identifier: MIT

/**
 * @file main.cpp
 * @brief GraphX source file.
 */

#include <graph/NodeFactory.hpp>
#include <core/VariantRouter.hpp>
#include <config/DataTypes.hpp>
#include <dsp/CpuSpectrumDftNode.hpp>

int main() {
    graph::VariantRouter<sensors::SensorPayload> router;
    (void)router;
    return 0;
}
