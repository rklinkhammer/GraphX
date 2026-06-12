#pragma once

#include "sar/D2HAsyncAccelNode.hpp"

namespace sar {

// Compatibility alias for config-facing SAR graphs. Keep until maintained presets
// and downstream users migrate to D2HAsyncAccelNode explicitly.
using D2HAsyncConfig = D2HAsyncAccelConfig;
using D2HAsyncNode = D2HAsyncAccelNode;
using D2HAsyncInputToken = SarAccelControlToken;
using D2HAsyncOutputToken = SarAccelControlToken;

} // namespace sar
