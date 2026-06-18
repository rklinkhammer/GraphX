/**
 * @file test_capability_context.cpp
 * @brief Test Capability Context Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include <gtest/gtest.h>

#include "capabilities/GraphCapability.hpp"
#include "graph/CapabilityContext.hpp"
#include "metrics/IMetricsCallback.hpp"

namespace {

/**
 * @class MetricsNode
 * @brief Metrics node implementation for GraphX.
 */
class MetricsNode final : public graph::INode, public graph::IMetricsCallbackProvider {
public:
    graph::LifecycleState GetLifecycleState() const override {
        return graph::LifecycleState::Initialized;
    }

    bool Init() override {
        return true;
    }

    bool Start() override {
        return true;
    }

    void Join() override {}

    bool JoinWithTimeout(std::chrono::milliseconds) override {
        return true;
    }

    void Stop() override {}

    bool SetMetricsCallback(graph::IMetricsCallback* callback) noexcept override {
        callback_ = callback;
        return callback_ != nullptr;
    }

    bool HasMetricsCallback() const noexcept override {
        return callback_ != nullptr;
    }

    graph::IMetricsCallback* GetMetricsCallback() const noexcept override {
        return callback_;
    }

    app::metrics::NodeMetricsSchema GetNodeMetricsSchema() const noexcept override {
        return {};
    }

private:
    graph::IMetricsCallback* callback_{nullptr};
};

struct TestBusCapability {
    int value{42};
};

/**
 * @class StubDescriptorProvider
 * @brief Stub descriptor provider implementation for GraphX.
 */
class StubDescriptorProvider final : public graph::INodeDescriptorProvider {
public:
    graph::NodeDescriptor BuildRuntimeDescriptor(
        graph::RuntimeNodeDescriptorRequest request) const override {
        graph::NodeDescriptor descriptor;
        descriptor.name = "stub-provider-descriptor";
        descriptor.type = std::move(request.seed.type);
        descriptor.description = std::move(request.seed.description);
        descriptor.lifecycle_state = request.seed.lifecycle_state;
        descriptor.supports_configuration = request.seed.supports_configuration;
        descriptor.input_ports = std::move(request.input_ports);
        descriptor.output_ports = std::move(request.output_ports);
        return descriptor;
    }
};

/**
 * @class StubCapabilityContextMetadataService
 * @brief Stub capability context metadata service implementation for GraphX.
 */
class StubCapabilityContextMetadataService final : public graph::INodeMetadataService {
public:
    const graph::INodeDescriptorProvider& DescriptorProvider() const override {
        return descriptor_provider_;
    }

    const graph::INodeDescriptorSchemaProvider& DescriptorSchemaProvider() const override {
        return graph::GetDefaultNodeDescriptorSchemaProvider();
    }

private:
    StubDescriptorProvider descriptor_provider_;
};

}  // namespace

TEST(CapabilityContextTest, ReportsMissingGraphManager) {
    capabilities::GraphCapability graph_capability;
    graph::CapabilityContext context{graph_capability};

    auto nodes = context.Nodes();

    ASSERT_FALSE(nodes);
    EXPECT_EQ(nodes.error(), graph::CapabilityContextError::MissingGraphManager);
}

TEST(CapabilityContextTest, DiscoversDirectNodeCapability) {
    capabilities::GraphCapability graph_capability;
    graph::CapabilityContext context{graph_capability};
    auto node = std::make_shared<MetricsNode>();

    auto capability = context.NodeCapability<graph::IMetricsCallbackProvider>(node);

    ASSERT_TRUE(capability);
    EXPECT_EQ(capability->get(), node.get());
}

TEST(CapabilityContextTest, ReportsMissingDirectNodeCapability) {
    capabilities::GraphCapability graph_capability;
    graph::CapabilityContext context{graph_capability};
    auto node = std::make_shared<MetricsNode>();

    auto capability = context.NodeCapability<graph::CompletionCallbackProvider>(node);

    ASSERT_FALSE(capability);
    EXPECT_EQ(capability.error(), graph::CapabilityContextError::MissingNodeCapability);
}

TEST(CapabilityContextTest, RetrievesBusCapabilityWithExpectedContract) {
    capabilities::GraphCapability graph_capability;
    auto bus_capability = std::make_shared<TestBusCapability>();
    graph_capability.GetCapabilityBus().Register<TestBusCapability>(bus_capability);
    graph::CapabilityContext context{graph_capability};

    auto capability = context.BusCapability<TestBusCapability>();

    ASSERT_TRUE(capability);
    EXPECT_EQ(*capability, bus_capability);
    EXPECT_EQ((*capability)->value, 42);
}

TEST(CapabilityContextTest, DescribeNodeUsesInjectedDescriptorProvider) {
    capabilities::GraphCapability graph_capability;
    StubCapabilityContextMetadataService metadata_service;
    graph::CapabilityContext context{graph_capability, &metadata_service};
    auto node = std::make_shared<MetricsNode>();

    auto descriptor = context.DescribeNode(node);

    EXPECT_EQ(descriptor.name, "stub-provider-descriptor");
}

TEST(CapabilityContextTest, DescribeNodeUsesInjectedMetadataService) {
    capabilities::GraphCapability graph_capability;
    StubCapabilityContextMetadataService metadata_service;
    graph::CapabilityContext context{graph_capability, &metadata_service};
    auto node = std::make_shared<MetricsNode>();

    auto descriptor = context.DescribeNode(node);

    EXPECT_EQ(descriptor.name, "stub-provider-descriptor");
}
