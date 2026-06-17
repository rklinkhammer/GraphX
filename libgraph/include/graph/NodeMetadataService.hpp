/**
 * @file NodeMetadataService.hpp
 * @brief GraphX source file.
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
 * @brief INodeMetadataService class.
 */
/**
 * @class INodeMetadataService
 * @brief I node metadata service implementation for GraphX.
 */
class INodeMetadataService {
public:
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
class DefaultNodeMetadataService final : public INodeMetadataService {
public:
    const INodeDescriptorProvider& DescriptorProvider() const override {
        return GetDefaultNodeDescriptorProvider();
    }

    const INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return GetDefaultNodeDescriptorSchemaProvider();
    }
};

inline const INodeMetadataService& GetDefaultNodeMetadataService() {
    static const DefaultNodeMetadataService service;
    return service;
}

}  // namespace graph
