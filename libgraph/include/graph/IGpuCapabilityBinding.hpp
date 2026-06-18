/**
 * @file IGpuCapabilityBinding.hpp
 * @brief Igpu Capability Binding Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "graph/CapabilityBus.hpp"

namespace graph {

/**
 * @class IGpuCapabilityBinding
 * @brief Igpu Capability Binding capability contract.
 *
 * @details Describes a runtime service obtained through the capability bus. Implementations provide backend or policy services without coupling graph nodes to concrete subsystems.
 */
class IGpuCapabilityBinding {
public:
    /**
     * @brief Releases resources owned by Igpu Capability Binding.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
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
