#pragma once

#include "sar/H2DAsyncAccelNode.hpp"

namespace sar {

// Compatibility alias for config-facing SAR graphs. Keep until maintained presets
// and downstream users migrate to H2DAsyncAccelNode explicitly.
using H2DAsyncConfig = H2DAsyncAccelConfig;
using H2DAsyncNode = H2DAsyncAccelNode;
using H2DAsyncInputToken = SarAccelControlToken;
using H2DAsyncOutputToken = SarAccelControlToken;

} // namespace sar
