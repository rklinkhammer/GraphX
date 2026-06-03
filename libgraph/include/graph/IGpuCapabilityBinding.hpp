// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "graph/CapabilityBus.hpp"

namespace graph {

class IGpuCapabilityBinding {
public:
    virtual ~IGpuCapabilityBinding() = default;

    // Called during GpuPolicy::OnInit to bind capability dependencies
    // from the graph-owned capability bus into the node.
    virtual bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) = 0;
};

} // namespace graph
