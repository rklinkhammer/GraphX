// SPDX-License-Identifier: MIT

/**
 * @file test_topology_debug.cpp
 * @brief Test Topology Debug Graph runtime support.
 *
 * @details Provides Graph runtime test coverage and test support nodes. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
 */
#include <gtest/gtest.h>
#include "graph/RegisteredNodeProvider.hpp"
#include "graph/NodeProvider.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include "test/AdvancedTestNodes.hpp"
#include "test/PluginInfrastructure.hpp"

TEST(TopologyDebug, BuildsAndInitializesTopology3) {
    // Load plugins manually
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto loader = std::make_unique<graph::PluginLoader>(
        PLUGIN_OUTPUT_DIRECTORY, registry);
    
    auto loaded = loader->LoadAllPluginsSafe();
    ASSERT_TRUE(loaded);
    
    // Create provider
    std::shared_ptr<graph::INodeProvider> provider = std::make_shared<graph::RegisteredNodeProvider>(registry);
    
    // Build Topology3 exactly as the test does
    EXPECT_NO_THROW({
        auto graph = std::make_shared<graph::GraphManager>();
        
        auto source_adapter = std::make_shared<graph::NodeFacadeAdapter>(
            test::PluginInfrastructure::CreateNodeOrThrow(provider, "SourceTestNode"));
        auto interior_adapter = std::make_shared<graph::NodeFacadeAdapter>(
            test::PluginInfrastructure::CreateNodeOrThrow(provider, "InteriorTestNode"));
        auto sink_adapter = std::make_shared<graph::NodeFacadeAdapter>(
            test::PluginInfrastructure::CreateNodeOrThrow(provider, "SinkTestNode"));
        
        auto sourcex = std::make_shared<graph::NodeFacadeAdapterWrapper>(source_adapter);
        auto interiorx = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior_adapter);
        auto sinkx = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink_adapter);
        
        graph->AddNode(sourcex);
        graph->AddNode(interiorx);
        graph->AddNode(sinkx);
        
        // Now try to create executor and init it (might crash here)
        auto executor = graph::GraphExecutorBuilder()
            .WithGraphManager(graph)
            .WithExecutorTimeout(std::chrono::seconds(30))
            .Build();
        
        EXPECT_NE(nullptr, executor);
        
        auto init_result = executor->Init();
        EXPECT_TRUE(init_result.success);
        
        auto start_result = executor->Start();
        EXPECT_TRUE(start_result.success);
        const auto stop_result = executor->Stop();
        EXPECT_TRUE(stop_result.success);
        const auto join_result = executor->Join();
        EXPECT_TRUE(join_result.success);
    });
}
