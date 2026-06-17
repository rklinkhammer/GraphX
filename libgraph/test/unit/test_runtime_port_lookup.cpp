/**
 * @file test_runtime_port_lookup.cpp
 * @brief GraphX source file.
 */

// MIT License
//
// Copyright (c) 2026 GraphX contributors

#include <gtest/gtest.h>

#include "graph/IPortFunction.hpp"
#include "graph/RuntimePort.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

TEST(RuntimePortLookupTest, OutputPortLookupByIdUsesAdapterMetadata) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);

    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateNodeOrThrow(provider, "SourceTestNode"));

    auto result = adapter->GetOutputPortHandle("0", 7);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->node_index, 7u);
    EXPECT_EQ(result->descriptor.id, 0u);
    EXPECT_EQ(result->descriptor.direction, graph::PortDirection::Output);
    EXPECT_FALSE(result->descriptor.name.empty());
    EXPECT_FALSE(result->descriptor.payload_type.empty());
    EXPECT_FALSE(result->descriptor.transport_type.empty());
    EXPECT_NE(result->owned_port, nullptr);
    ASSERT_NE(result->port, nullptr);
    EXPECT_EQ(result->port->GetDirection(), graph::PortDirection::Output);
    EXPECT_EQ(result->port->GetTypeName(), result->descriptor.payload_type);
}

TEST(RuntimePortLookupTest, OutputPortLookupByNameUsesAdapterMetadata) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);

    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateNodeOrThrow(provider, "SourceTestNode"));
    const auto metadata = adapter->GetOutputPortMetadata();
    ASSERT_FALSE(metadata.empty());

    auto result = adapter->GetOutputPortHandle(metadata.front().port_name, 3);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->node_index, 3u);
    EXPECT_EQ(result->descriptor.id, metadata.front().index);
    EXPECT_EQ(result->descriptor.name, metadata.front().port_name);
    EXPECT_EQ(result->descriptor.direction, graph::PortDirection::Output);
    ASSERT_NE(result->port, nullptr);
    EXPECT_FALSE(std::string(result->port->GetTransportTypeName()).empty());
}

TEST(RuntimePortLookupTest, InputPortLookupByNameUsesAdapterMetadata) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);

    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateNodeOrThrow(provider, "SinkTestNode"));
    const auto metadata = adapter->GetInputPortMetadata();
    ASSERT_FALSE(metadata.empty());

    auto result = adapter->GetInputPortHandle(metadata.front().port_name, 11);

    ASSERT_TRUE(result);
    EXPECT_EQ(result->node_index, 11u);
    EXPECT_EQ(result->descriptor.id, metadata.front().index);
    EXPECT_EQ(result->descriptor.name, metadata.front().port_name);
    EXPECT_EQ(result->descriptor.direction, graph::PortDirection::Input);
    ASSERT_NE(result->port, nullptr);
    EXPECT_FALSE(std::string(result->port->GetTransportTypeName()).empty());
}

TEST(RuntimePortLookupTest, MissingOutputPortReturnsPortNotFound) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);

    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateNodeOrThrow(provider, "SourceTestNode"));

    auto result = adapter->GetOutputPortHandle("missing_port", 0);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::RuntimePortLookupError::PortNotFound);
}

TEST(RuntimePortLookupTest, MissingInputPortByIdReturnsPortNotFound) {
    auto provider = test::PluginInfrastructure::GetProvider();
    ASSERT_NE(provider, nullptr);

    auto adapter = std::make_shared<graph::NodeFacadeAdapter>(
        test::PluginInfrastructure::CreateNodeOrThrow(provider, "SinkTestNode"));

    auto result = adapter->GetInputPortHandle("99", 0);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), graph::RuntimePortLookupError::PortNotFound);
}

}  // namespace