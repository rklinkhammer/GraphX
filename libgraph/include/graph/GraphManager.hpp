// SPDX-License-Identifier: MIT
#pragma once

// PR10 decomposition: GraphManager.hpp is now a focused umbrella that pulls
// in ownership/lifecycle/edge/metrics components and the full core definition.

#include "graph/GraphManagerOwnership.hpp"
#include "graph/GraphManagerLifecycle.hpp"
#include "graph/GraphManagerEdgeOwnership.hpp"
#include "graph/GraphManagerMetricsSnapshot.hpp"
#include "graph/GraphManagerCore.hpp"
