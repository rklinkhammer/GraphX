// MIT License
//
// Copyright (c) 2025 graphlib contributors

#include <gtest/gtest.h>

#include "graph/NodeFactoryRegistry.hpp"

namespace {

class StubFacadeDescriptorProvider final : public graph::INodeDescriptorProvider {
public:
    graph::NodeDescriptor BuildRuntimeDescriptor(
        graph::RuntimeNodeDescriptorRequest request) const override {
        auto descriptor = graph::BuildRuntimeNodeDescriptor(
            std::move(request.seed),
            request.parameterized,
            std::move(request.input_ports),
            std::move(request.output_ports));
        descriptor.name = "compat_descriptor_provider";
        return descriptor;
    }
};

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

bool FacadeTrue(graph::NodeHandle) {
    return true;
}

void FacadeNoop(graph::NodeHandle) {
}

graph::NodeFacadeAdapter MakeAdapter() {
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

    return graph::NodeFacadeAdapter(&handle_storage, &facade);
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

TEST(NodeFactoryRegistryExpectedTest, RegisterExpectedReportsInvalidArguments) {
    graph::config::NodeFactoryRegistry registry;

    auto empty_name = registry.RegisterExpected(
        "",
        MakeAdapter);
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.error(), graph::config::NodeFactoryRegistry::RegistryError::EmptyTypeName);

    auto null_factory = registry.RegisterExpected("TestNode", {});
    ASSERT_FALSE(null_factory);
    EXPECT_EQ(null_factory.error(), graph::config::NodeFactoryRegistry::RegistryError::NullFactory);
}

TEST(NodeFactoryRegistryExpectedTest, CreateExpectedReportsMissingType) {
    graph::config::NodeFactoryRegistry registry;

    auto result = registry.CreateExpected("MissingNode");

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::config::NodeFactoryRegistry::RegistryError::TypeNotRegistered);
}

TEST(NodeFactoryRegistryExpectedTest, CreateExpectedReturnsRegisteredAdapter) {
    graph::config::NodeFactoryRegistry registry;

    auto registered = registry.RegisterExpected(
        "TestNode",
        MakeAdapter);
    ASSERT_TRUE(registered);

    auto result = registry.CreateExpected("TestNode");

    EXPECT_TRUE(result);
}

TEST(NodeFactoryRegistryExpectedTest, NodeFacadeAdapterMetadataServiceCtorInjectsDescriptorProvider) {
    StubFacadeMetadataService metadata_service;
    auto adapter = MakeAdapterWithMetadataService(&metadata_service);

    const auto descriptor = adapter.GetDescriptor();
    EXPECT_EQ(descriptor.name, "compat_descriptor_provider");
}

}  // namespace
