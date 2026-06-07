#include <gtest/gtest.h>
#include "graph/RegisteredNodeProvider.hpp"
#include "graph/NodeProvider.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/GraphManager.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include "test/AdvancedTestNodes.hpp"
#include "test/PluginInfrastructure.hpp"

TEST(TopologyDebug, DISABLED_BuildsAndInitializesTopology3) {
    // Load plugins manually
    auto registry = std::make_shared<graph::PluginRegistry>();
    auto loader = std::make_unique<graph::PluginLoader>(
        "/Users/rklinkhammer/workspace/GraphX/build/plugins", registry);
    
    auto loaded = loader->LoadAllPluginsSafe();
    if (!loaded) {
        // Plugins may not be fully available, but registration may have worked
    }
    
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
        
        // Try to start execution (might crash here)
        try {
            auto start_result = executor->Start();
            EXPECT_TRUE(start_result.success);
            
            // Wait a bit for execution
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            FAIL() << "Exception during Start(): " << e.what();
        } catch (...) {
            FAIL() << "Unknown exception during Start()";
        }
    });
}
