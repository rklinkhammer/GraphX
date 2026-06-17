/**
 * @file IGpuCapabilityBinding.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "graph/CapabilityBus.hpp"

namespace graph {

/**
 * @class IGpuCapabilityBinding
 * @brief IGpuCapabilityBinding class.
 */
/**
 * @class IGpuCapabilityBinding
 * @brief I gpu capability binding implementation for GraphX.
 */
class IGpuCapabilityBinding {
public:
    virtual ~IGpuCapabilityBinding() = default;

    // Called during GpuPolicy::OnInit to bind capability dependencies
    // from the graph-owned capability bus into the node.
/**
 * @brief Bind gpu capabilities.
 * @param capability_bus Parameter for bind gpu capabilities.
 * @return Result of the operation.
 */
    virtual bool BindGpuCapabilities(graph::CapabilityBus& capability_bus) = 0;
};

} // namespace graph
