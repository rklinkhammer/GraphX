/**
 * @file NodeFacadeInterop.hpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "graph/NodeFacadeAbi.hpp"

namespace graph {

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
