/**
 * @file test_node_facade_adapter.cpp
 * @brief Test Node Facade Adapter Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2025 graphlib contributors

#include <gtest/gtest.h>

#include <utility>

#include "graph/NodeFacade.hpp"

namespace {

/**
 * @class StubFacadeDescriptorProvider
 * @brief Stub facade descriptor provider implementation for GraphX.
 */
class StubFacadeDescriptorProvider final : public graph::INodeDescriptorProvider {
public:
    graph::NodeDescriptor BuildRuntimeDescriptor(
        graph::RuntimeNodeDescriptorRequest request) const override {
        auto descriptor = graph::BuildRuntimeNodeDescriptor(
            std::move(request.seed),
            request.parameterized,
            std::move(request.input_ports),
            std::move(request.output_ports));
        descriptor.name = "metadata_service_descriptor_provider";
        return descriptor;
    }
};

/**
 * @class StubFacadeMetadataService
 * @brief Stub facade metadata service implementation for GraphX.
 */
class StubFacadeMetadataService final : public graph::INodeMetadataService {
public:
    const graph::INodeDescriptorProvider& DescriptorProvider() const override {
        return descriptor_provider_;
    }

    const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return graph::GetDefaultNodeDescriptorSchemaProvider();
    }

private:
    StubFacadeDescriptorProvider descriptor_provider_;
};

/**
 * @brief Facade true.
 * @param graph::NodeHandle Parameter for facade true.
 */
bool FacadeTrue(graph::NodeHandle) {
    return true;
}

/**
 * @brief Facade noop.
 * @param graph::NodeHandle Parameter for facade noop.
 */
void FacadeNoop(graph::NodeHandle) {
}

graph::NodeFacadeAdapter MakeAdapterWithMetadataService(
    const graph::INodeMetadataService* metadata_service) {
    static int handle_storage = 0;
    static const graph::NodeFacade facade{
        .GetLifecycleState = nullptr,
        .Init = FacadeTrue,
        .Start = FacadeTrue,
        .Stop = FacadeNoop,
        .Join = nullptr,
        .JoinWithTimeout = nullptr,
        .Execute = nullptr,
        .GetName = nullptr,
        .SetName = nullptr,
        .GetType = nullptr,
        .GetInputPortCount = nullptr,
        .GetOutputPortCount = nullptr,
        .GetInputPortName = nullptr,
        .GetOutputPortName = nullptr,
        .GetInputPortMetadata = nullptr,
        .GetOutputPortMetadata = nullptr,
        .FreePortMetadata = nullptr,
        .GetThreadMetrics = nullptr,
        .GetThreadUtilizationPercent = nullptr,
        .GetAsDataInjectionNodeConfig = nullptr,
        .GetAsIConfigurable = nullptr,
        .GetAsIDiagnosable = nullptr,
        .GetAsIParameterized = nullptr,
        .GetAsIMetricsCallbackProvider = nullptr,
        .GetAsICompletionCallback = nullptr,
        .Destroy = FacadeNoop,
    };

    return graph::NodeFacadeAdapter(&handle_storage, &facade, metadata_service);
}

TEST(NodeFacadeAdapterTest, MetadataServiceCtorInjectsDescriptorProvider) {
    StubFacadeMetadataService metadata_service;
    auto adapter = MakeAdapterWithMetadataService(&metadata_service);

    const auto descriptor = adapter.GetDescriptor();
    EXPECT_EQ(descriptor.name, "metadata_service_descriptor_provider");
}

}  // namespace
