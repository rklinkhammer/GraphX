/**
 * @file NodeFacadeInterop.hpp
 * @brief Node Facade Interop Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "graph/NodeFacadeAbi.hpp"

namespace graph {

/**

 * @struct ExtractedNodeInterfaces

 * @brief Extracted Node Interfaces data record.

 *

 * @details Groups related fields passed through GraphX runtime, DSP, or GPU boundaries. The type is intentionally documented as a value object so callers understand ownership, lifetime, and validation expectations.

 */

struct ExtractedNodeInterfaces {
    void* data_injection_node_config{nullptr};
    void* configurable{nullptr};
    void* diagnosable{nullptr};
    void* parameterized{nullptr};
    void* metrics_callback_provider{nullptr};
    void* completion_callback_provider{nullptr};
    void* gpu_capability_binding{nullptr};
};

// Centralized ABI callback probing for optional node interfaces.
// Core graph code should use this typed result rather than invoking
// GetAs* callbacks directly across many call sites.
ExtractedNodeInterfaces ExtractNodeInterfaces(
    NodeHandle handle,
    const NodeFacade* facade) noexcept;

}  // namespace graph
