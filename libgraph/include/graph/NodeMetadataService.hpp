/**
 * @file NodeMetadataService.hpp
 * @brief Node Metadata Service Graph runtime support.
 *
 * @details Provides graph construction, node execution, ports, messages, and runtime orchestration. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "config/SchemaGenerator.hpp"
#include "graph/NodeDescriptor.hpp"

namespace graph {

/**
 * @class INodeMetadataService
 * @brief Inode Metadata Service graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class INodeMetadataService {
public:
    /**
     * @brief Releases resources owned by Inode Metadata Service.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     */
    virtual ~INodeMetadataService() = default;

/**
 * @brief Descriptor provider.
 * @return Result of the operation.
 */
    virtual const INodeDescriptorProvider& DescriptorProvider() const = 0;
/**
 * @brief Descriptor schema provider.
 * @return Result of the operation.
 */
    virtual const INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const = 0;
};

/**
 * @class DefaultNodeMetadataService
 * @brief Default node metadata service implementation for GraphX.
 */
/**
 * @class DefaultNodeMetadataService
 * @brief Default Node Metadata Service graph node.
 *
 * @details Implements a GraphX node boundary with typed inputs, outputs, configuration, and lifecycle hooks. The node participates in graph execution through the standard port and message contracts.
 */
class DefaultNodeMetadataService final : public INodeMetadataService {
public:
    /**
     * @brief Executes the Descriptor Provider operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    const INodeDescriptorProvider& DescriptorProvider() const override {
        return GetDefaultNodeDescriptorProvider();
    }

    /**
     * @brief Executes the Descriptor Schema Provider operation.
     *
     * @details Documents the method contract for Doxygen readers. Callers should preserve the surrounding GraphX lifecycle, ownership, and typed-message invariants when invoking or overriding this method.
     * @return Method-specific result, status, or produced value when the signature provides one.
     */
    const INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return GetDefaultNodeDescriptorSchemaProvider();
    }
};

inline const INodeMetadataService& GetDefaultNodeMetadataService() {
    static const DefaultNodeMetadataService service;
    return service;
}

}  // namespace graph
