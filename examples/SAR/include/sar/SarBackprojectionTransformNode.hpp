#pragma once

#include "sar/SarBackprojectionTransformAccelNode.hpp"

namespace sar {

// Compatibility alias for config-facing SAR graphs. Keep until maintained presets
// and downstream users migrate to SarBackprojectionTransformAccelNode explicitly.
using SarBackprojectionTransformConfig = SarBackprojectionTransformAccelConfig;
using SarBackprojectionTransformNode = SarBackprojectionTransformAccelNode;
using SarBackprojectionInputToken = SarAccelControlToken;
using SarBackprojectionOutputToken = SarAccelControlToken;

} // namespace sar
