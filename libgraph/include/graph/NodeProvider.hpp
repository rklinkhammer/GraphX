// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include <expected>
#include <string>
#include <vector>

#include "graph/NodeFacade.hpp"

namespace graph {

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
 * static registry, loaders, and factory helpers behind the creation path.
 */
class INodeProvider {
public:
    virtual ~INodeProvider() = default;

    [[nodiscard]] virtual std::expected<NodeFacadeAdapter, NodeCreationError>
    CreateNodeExpected(const std::string& node_type_name) noexcept = 0;

    [[nodiscard]] virtual bool
    IsNodeTypeAvailable(const std::string& node_type_name) const = 0;

    [[nodiscard]] virtual std::vector<std::string>
    GetAvailableNodeTypes() const = 0;
};

}  // namespace graph
