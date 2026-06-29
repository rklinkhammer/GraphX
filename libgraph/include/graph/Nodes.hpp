// SPDX-License-Identifier: MIT
#pragma once

// PR10 decomposition: keep Nodes.hpp as a thin umbrella while concrete
// node-shape implementations live in focused headers.

#include "graph/IFnBase.hpp"
#include "graph/InputFunction.hpp"
#include "graph/Lifecycle.hpp"
#include "graph/MergeFunction.hpp"
#include "graph/NamedType.hpp"
#include "graph/OutputFunction.hpp"
#include "graph/PortSpec.hpp"
#include "graph/PortTypes.hpp"
#include "graph/ThreadMetrics.hpp"
#include "graph/TransferFunction.hpp"

#include "graph/NodeShapes.hpp"
#include "graph/SplitNode.hpp"
#include "graph/NamedNodes.hpp"
