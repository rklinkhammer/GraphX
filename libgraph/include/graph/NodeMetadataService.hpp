// MIT License
//
// Copyright (c) 2026 GraphX contributors

#pragma once

#include "config/SchemaGenerator.hpp"
#include "graph/NodeDescriptor.hpp"

namespace graph {

class INodeMetadataService {
public:
    virtual ~INodeMetadataService() = default;

    virtual const INodeDescriptorProvider& DescriptorProvider() const = 0;
    virtual const INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const = 0;
};

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
