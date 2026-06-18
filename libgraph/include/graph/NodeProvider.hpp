/**
 * @file NodeProvider.hpp
 * @brief Node Provider Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <expected>
#include <string>
#include <vector>

#include "graph/NodeFacade.hpp"

namespace graph {

/**

 * @enum NodeCreationError

 * @brief Node Creation Error values.

 *

 * @details Enumerates stable options or status values used by the libgraph API. Keep additions explicit so configuration, diagnostics, and generated documentation remain readable.

 */

enum class NodeCreationError {
    TypeNotFound = 2,
    NotInitialized = 3,
    CreationFailed = 4,
    InvalidArgument = 5,
    Unknown = 99,
};

/**
 * Single node-creation/query contract for graph construction.
 *
 * This separates graph-building code from the concrete mix of plugin registry,
 * static registry, loaders, and provider helpers behind the creation path.
 */
/**
 * @class INodeProvider
 * @brief Inode Provider graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class INodeProvider {
public:
    /**
     * @brief Releases resources owned by Inode Provider.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~INodeProvider() = default;

    [[nodiscard]] virtual std::expected<NodeFacadeAdapter, NodeCreationError>
    /**
     * @brief Creates or builds the object described by Create Node Expected.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    CreateNodeExpected(const std::string& node_type_name) noexcept = 0;

    [[nodiscard]] virtual bool
    /**
     * @brief Reports whether Is Node Type Available is true.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @param node_type_name Input or configuration value consumed by the method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    IsNodeTypeAvailable(const std::string& node_type_name) const = 0;

    [[nodiscard]] virtual std::vector<std::string>
    /**
     * @brief Returns the Available Node Types.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    GetAvailableNodeTypes() const = 0;
};

}  // namespace graph
