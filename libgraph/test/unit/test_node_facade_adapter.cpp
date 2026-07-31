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

int facade_stop_calls = 0;
int facade_join_calls = 0;
int facade_destroy_calls = 0;

void FacadeStop(graph::NodeHandle) {
    ++facade_stop_calls;
}

void FacadeDestroy(graph::NodeHandle) {
    ++facade_destroy_calls;
}

bool FacadeJoin(graph::NodeHandle) {
    ++facade_join_calls;
    return true;
}

graph::NodeFacadeAdapter MakeAdapterWithMetadataService(
    const graph::INodeMetadataService* metadata_service) {
    static int handle_storage = 0;
    static const graph::NodeFacade facade{
        .GetLifecycleState = nullptr,
        .Init = FacadeTrue,
        .Start = FacadeTrue,
        .Stop = FacadeStop,
        .Join = FacadeJoin,
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
        .Destroy = FacadeDestroy,
    };

    return graph::NodeFacadeAdapter(&handle_storage, &facade, metadata_service);
}

TEST(NodeFacadeAdapterTest, MetadataServiceCtorInjectsDescriptorProvider) {
    StubFacadeMetadataService metadata_service;
    auto adapter = MakeAdapterWithMetadataService(&metadata_service);

    const auto descriptor = adapter.GetDescriptor();
    EXPECT_EQ(descriptor.name, "metadata_service_descriptor_provider");
}

TEST(NodeFacadeAdapterTest, CleanupDestroysOwnedHandleExactlyOnce) {
    facade_destroy_calls = 0;
    {
        auto adapter = MakeAdapterWithMetadataService(nullptr);
        adapter.Cleanup();
        adapter.Cleanup();
        EXPECT_EQ(facade_destroy_calls, 1);
    }
    EXPECT_EQ(facade_destroy_calls, 1);
}

TEST(NodeFacadeAdapterTest, DestructorDestroysOwnedHandle) {
    facade_destroy_calls = 0;
    { auto adapter = MakeAdapterWithMetadataService(nullptr); }
    EXPECT_EQ(facade_destroy_calls, 1);
}

TEST(NodeFacadeAdapterTest, StopRetainsJoinRequiredStateUntilJoinCompletes) {
    facade_stop_calls = 0;
    facade_join_calls = 0;
    auto adapter = MakeAdapterWithMetadataService(nullptr);

    ASSERT_TRUE(adapter.Init());
    ASSERT_TRUE(adapter.Start());
    adapter.Stop();
    EXPECT_EQ(facade_stop_calls, 1);
    EXPECT_TRUE(adapter.Join());
    EXPECT_EQ(facade_join_calls, 1);
    EXPECT_TRUE(adapter.Join());
    EXPECT_EQ(facade_join_calls, 1);
}

}  // namespace
