/**
 * @file TestGraphTopologies.cpp
 * @brief Implementation of graph topologies for GraphManager testing
 *
 * Provides a collection of predefined graph topologies for comprehensive
 * testing of graph construction, execution, and validation scenarios.
 *
 * Uses static templated node creation with AddNode<NodeType>() and
 * templated AddEdge for type-safe edge creation between nodes.
 *
 * @author Test Suite
 * @date 2026-05-14
 */

#include "test/TestGraphTopologies.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeFactory.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "plugins/PluginRegistry.hpp"
#include "plugins/PluginLoader.hpp"
#include "test/AdvancedTestNodes.hpp"
#include <stdexcept>
#include <memory>
#include <vector>
#include <string>
#include <log4cxx/logger.h>

// Ensure PLUGIN_OUTPUT_DIRECTORY is defined
#ifndef PLUGIN_OUTPUT_DIRECTORY
#define PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace test {

// ============================================================================
// Plugin Infrastructure Helper
// ============================================================================

class PluginInfrastructure {
public:
    static std::shared_ptr<graph::NodeFactory> GetFactory() {
        static auto* factory = new std::shared_ptr<graph::NodeFactory>();
        static auto* loader = new std::shared_ptr<graph::PluginLoader>();
        static bool initialized = false;
        
        if (!initialized) {
            auto registry = std::make_shared<graph::PluginRegistry>();
            // Load plugins from build directory
            *loader = std::make_shared<graph::PluginLoader>(PLUGIN_OUTPUT_DIRECTORY, registry);
            
            try {
                (*loader)->LoadAllPlugins();
            } catch (...) {
                // Plugins may not be available in test environment
            }
            
            *factory = std::make_shared<graph::NodeFactory>(registry);
            (*factory)->SetPluginLoader(*loader);
            initialized = true;
        }
        
        return *factory;
    }

    template <typename SrcNode, std::size_t SrcPort, typename DstNode, std::size_t DstPort>
    static bool AddEdge(std::shared_ptr<graph::GraphManager> g,
                        std::shared_ptr<graph::NodeFacadeAdapterWrapper> src_wrapper,
                        std::shared_ptr<graph::NodeFacadeAdapterWrapper> dst_wrapper,
                        size_t buffer_size = 10)
    {
        auto src = src_wrapper->GetNode<SrcNode>();
        auto dst = dst_wrapper->GetNode<DstNode>();
        if (!src)
        {
            return false;
        }
        if (!dst)
        {
            return false;
        }
        g->AddEdge<SrcNode, SrcPort, DstNode, DstPort>(src, dst, buffer_size);
        return true;
    }
};

// // Helper to convert NodeFacadeAdapter to INode for use with templated AddEd
// ============================================================================
// Topology 1: Minimal Graph (Source -> Sink)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildMinimalGraph() {
    /**
     * Graph Structure:
     *
     *   ┌────────────────────────────┐
     *   │    Minimal Graph           │
     *   └────────────────────────────┘
     *
     *   [Source] --> [Sink]
     *      |          |
     *     Out(0)   In(0)
     *
     * Data Flow:
     *   Source produces Message -> Sink consumes
     *
     * Purpose:
     *   Baseline test for simplest valid graph
     *   Tests fundamental source-to-sink connectivity
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));

    auto source_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);
    auto sink_wrapper = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source_wrapper);
    graph->AddNode(sink_wrapper);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, SinkTestNode, 0>(graph, source_wrapper, sink_wrapper);
    std::cout << "Graph Structure:" << std::endl;
    std::cout << graph->DisplayGraph();
    return graph;
}

// ============================================================================
// Topology 2: Linear Sequential (Source -> Interior -> Sink)
// ============================================================================


std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildLinearSequential() {
    /**
     * Graph Structure:
     *
     *   ┌─────────────────────────────────────┐
     *   │     Linear Sequential Topology      │
     *   └─────────────────────────────────────┘
     *
     *   [Source] ---> [Interior] ---> [Sink]
     *      |              |             |
     *     Out(0)     In(0)/Out(0)   In(0)
     *
     * Data Flow:
     *   Source produces Message -> Interior passes through -> Sink consumes
     *
     * Purpose:
     *   Tests basic sequential graph execution with transformation node
     */
    

    auto graph = std::make_shared<graph::GraphManager>();

    auto factory = PluginInfrastructure::GetFactory();
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto xxx = factory->CreateDynamicNode("InteriorTestNode");
    //auto interior = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    //auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));


    // auto sourcex = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);
    // auto interiorx = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior);
    // auto sinkx = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);
    
    // graph->AddNode(sourcex);
    // graph->AddNode(interiorx);
    // graph->AddNode(sinkx);
    
    // PluginInfrastructure::AddEdge<SourceTestNode, 0, InteriorTestNode, 0>(graph, sourcex, interiorx);
    // PluginInfrastructure::AddEdge<InteriorTestNode, 0, SinkTestNode, 0>(graph, interiorx, sinkx);
        
    return graph;
}

// ============================================================================
// Topology 3: Simple Merge (Source + Source -> Merge -> Sink)
// ============================================================================


std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildMergeSimple() {
    /**
     * Graph Structure:
     *
     *   ┌──────────────────────────────────────┐
     *   │    Simple Merge Topology             │
     *   └──────────────────────────────────────┘
     *
     *   [Source1]
     *      |
     *     Out(0)
     *      |
     *      +---> [Merge] ---> [Sink]
     *      |      In(0)/In(1)/Out(0)
     *     In(0)
     *      |
     *   [Source2]
     *      |
     *     Out(0)
     *
     * Data Flow:
     *   Source1 & Source2 produce Messages -> Merge combines them -> Sink consumes
     *
     * Purpose:
     *   Tests multi-input merge node with parallel sources
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto source2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto merge = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source1);
    auto source2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source2);
    auto merge_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge);
    auto sink_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source1_typed);
    graph->AddNode(source2_typed);
    graph->AddNode(merge_typed);
    graph->AddNode(sink_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 0>(graph, source1_typed, merge_typed);
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 1>(graph, source2_typed, merge_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SinkTestNode, 0>(graph, merge_typed, sink_typed);
        
    return graph;
}

// ============================================================================
// Topology 4: Simple Split (Source -> Split -> Sink + Sink)
// ============================================================================


std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildSplitSimple() {
    /**
     * Graph Structure:
     *
     *   ┌──────────────────────────────────────┐
     *   │    Simple Split Topology             │
     *   └──────────────────────────────────────┘
     *
     *                  [Sink1]
     *                   ^
     *                   |
     *                In(0)
     *                   |
     *   [Source] ---> [Split]
     *      |          Out(0)/Out(1)
     *     Out(0)      |
     *     In(0)       |
     *                In(0)
     *                   |
     *                   v
     *                  [Sink2]
     *
     * Data Flow:
     *   Source produces Message -> Split replicates to both outputs -> Sinks consume
     *
     * Purpose:
     *   Tests single-output to multiple-input replication
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto split = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SplitTestNode"));
    auto sink1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    auto sink2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);
    auto split_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(split);
    auto sink1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink1);
    auto sink2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink2);

    graph->AddNode(source_typed);
    graph->AddNode(split_typed);
    graph->AddNode(sink1_typed);
    graph->AddNode(sink2_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, SplitTestNode, 0>(graph, source_typed, split_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 0, SinkTestNode, 0>(graph, split_typed, sink1_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 1, SinkTestNode, 0>(graph, split_typed, sink2_typed);
  
    return graph;
}

// ============================================================================
// Topology 5: Diamond Complex (Source -> Split -> Interior/Interior -> Merge -> Sink)
// ============================================================================


std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildDiamondComplex() {
    /**
     * Graph Structure:
     *
     *   ┌───────────────────────────────────────────────┐
     *   │    Diamond Complex Topology                   │
     *   └───────────────────────────────────────────────┘
     *
     *                 [Interior1]
     *                  ^        v
     *                 In(0)   Out(0)
     *                 |          |
     *   [Source] -> [Split]      +---> [Merge] --> [Sink]
     *       |       Out(0)/Out(1)         ^In(0)/In(1)
     *      Out(0)    |                    |
     *       In(0)    |                 In(1)
     *              In(0)               |
     *                |             [Interior2]
     *                v
     *             [Interior2]
     *                |
     *              Out(0)
     *                |
     *                v
     *
     * Data Flow:
     *   Source -> Split into 2 paths -> Interior processes each -> Merge combines -> Sink
     *
     * Purpose:
     *   Tests complex graph with both split and merge operations
     *   Validates data flow through multiple parallel paths
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto split = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SplitTestNode"));
    auto interior1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto interior2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto merge = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);
    auto split_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(split);
    auto interior1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior1);
    auto interior2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior2);
    auto merge_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge);
    auto sink_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source_typed);
    graph->AddNode(split_typed);
    graph->AddNode(interior1_typed);
    graph->AddNode(interior2_typed);
    graph->AddNode(merge_typed);
    graph->AddNode(sink_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, SplitTestNode, 0>(graph, source_typed, split_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 0, InteriorTestNode, 0>(graph, split_typed, interior1_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 1, InteriorTestNode, 0>(graph, split_typed, interior2_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 0>(graph, interior1_typed, merge_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 1>(graph, interior2_typed, merge_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SinkTestNode, 0>(graph, merge_typed, sink_typed);
    
    return graph;
}

// ============================================================================
// Topology 6: Multi-Path Sequential (Source -> Interior -> Interior -> Interior -> Sink)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildMultiPathSequential() {
    /**
     * Graph Structure:
     *
     *   ┌──────────────────────────────────────────────────────────┐
     *   │    Multi-Path Sequential Topology                        │
     *   └──────────────────────────────────────────────────────────┘
     *
     *   [Source] -> [Int1] -> [Int2] -> [Int3] -> [Sink]
     *      |         |        |        |        |
     *     Out(0)  In(0)/Out(0) In(0)/Out(0) In(0)/Out(0) In(0)
     *
     * Data Flow:
     *   Source -> Interior1 -> Interior2 -> Interior3 -> Sink
     *   Each Interior transforms the message sequentially
     *
     * Purpose:
     *   Tests long sequential processing chains
     *   Validates message integrity through multiple transformations
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto interior1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto interior2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto interior3 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);
    auto interior1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior1);
    auto interior2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior2);
    auto interior3_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior3);
    auto sink_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source_typed);
    graph->AddNode(interior1_typed);
    graph->AddNode(interior2_typed);
    graph->AddNode(interior3_typed);
    graph->AddNode(sink_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, InteriorTestNode, 0>(graph, source_typed, interior1_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, InteriorTestNode, 0>(graph, interior1_typed, interior2_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, InteriorTestNode, 0>(graph, interior2_typed, interior3_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, SinkTestNode, 0>(graph, interior3_typed, sink_typed);
        
    return graph;
}

// ============================================================================
// Topology 7: Interior to Merge (Source -> Interior -> Merge -> Sink)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildInteriorToMerge() {
    /**
     * Graph Structure:
     *
     *   ┌──────────────────────────────────────────┐
     *   │    Interior to Merge Topology            │
     *   └──────────────────────────────────────────┘
     *
     *                        [Source2]
     *                           |
     *                          Out(0)
     *                           |
     *   [Source1] -> [Interior] +---> [Merge] --> [Sink]
     *       |           |       In(0)    ^In(0)/In(1)
     *      Out(0)    In(0)/Out(0) |
     *                    |      In(1)
     *                    v
     *
     * Data Flow:
     *   Source1 -> Interior -> Merge (receives from Interior and Source2) -> Sink
     *
     * Purpose:
     *   Tests merge of Interior output with direct Source input
     *   Validates handling of different input sources to merge
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto source2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto interior = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto merge = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source1);
    auto source2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source2);
    auto interior_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior);
    auto merge_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge);
    auto sink_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source1_typed);
    graph->AddNode(source2_typed);
    graph->AddNode(interior_typed);
    graph->AddNode(merge_typed);
    graph->AddNode(sink_typed);

    PluginInfrastructure::AddEdge<SourceTestNode, 0, InteriorTestNode, 0>(graph, source1_typed, interior_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 0>(graph, interior_typed, merge_typed);
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 1>(graph, source2_typed, merge_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SinkTestNode, 0>(graph, merge_typed, sink_typed);


    return graph;
}

// ============================================================================
// Topology 8: Parallel Merge with Interior (Source + Source + Interior -> Merge -> Sink)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildParallelMergeWithInterior() {
    /**
     * Graph Structure:
     *
     *   ┌────────────────────────────────────────────────┐
     *   │    Parallel Merge with Interior Topology       │
     *   └────────────────────────────────────────────────┘
     *
     *   [Source1]
     *      |
     *     Out(0)
     *      |
     *      +--+
     *         |
     *         +---> [Merge] --> [Sink]
     *         |      ^In(0)/In(1)
     *      In(0)     |
     *         |      |
     *      [Interior]
     *         ^
     *         |
     *        In(0)
     *         |
     *      Out(0)
     *         |
     *   [Source2]
     *
     * Data Flow:
     *   Source1 & (Source2 -> Interior) -> Merge -> Sink
     *
     * Purpose:
     *   Tests merge with one direct source and one processed source
     *   Validates combination of different input types
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto source2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto interior = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto merge = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto sink = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source1);
    auto source2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source2);
    auto interior_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior);
    auto merge_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge);
    auto sink_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink);

    graph->AddNode(source1_typed);
    graph->AddNode(source2_typed);
    graph->AddNode(interior_typed);
    graph->AddNode(merge_typed);
    graph->AddNode(sink_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 0>(graph, source1_typed, merge_typed);
    PluginInfrastructure::AddEdge<SourceTestNode, 0, InteriorTestNode, 0>(graph, source2_typed, interior_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 1>(graph, interior_typed, merge_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SinkTestNode, 0>(graph, merge_typed, sink_typed);
    
    return graph;
}

// ============================================================================
// Topology 9: Complex Network (Multiple interleaved merge/split operations)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildComplexNetwork() {
    /**
     * Graph Structure:
     *
     *   ┌────────────────────────────────────────────────────────────┐
     *   │    Complex Network Topology                                │
     *   └────────────────────────────────────────────────────────────┘
     *
     *   [Source1] --> [Merge1] --> [Split1] --> [Interior] --> [Merge2] --> [Sink]
     *      |            ^           /  \           |             ^
     *     Out(0)      In(0)     Out(0) Out(1)   In(0)/Out(0)  In(0)
     *                  |        /        \         |
     *               In(1)   In(0)      In(0)    [Interior]
     *                 |    /             \        |
     *            [Source2]             [Sink]  Out(0)
     *               |                              |
     *              Out(0)                       In(1)
     *
     * Data Flow:
     *   Source1 & Source2 -> Merge1 -> Split1 -> paths processed separately
     *     -> Interior -> Merge2 -> Sink
     *   Split also goes directly to Sink for comparison
     *
     * Purpose:
     *   Tests complex scenarios with multiple merge/split combinations
     *   Validates intricate data routing and flow control
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto source2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto merge1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto split1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SplitTestNode"));
    auto interior1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto interior2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("InteriorTestNode"));
    auto merge2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("MergeTestNode"));
    auto sink1 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    auto sink2 = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SinkTestNode"));
    
    auto source1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source1);
    auto source2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source2);
    auto merge1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge1);
    auto split1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(split1);
    auto interior1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior1);
    auto interior2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(interior2);
    auto merge2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(merge2);
    auto sink1_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink1);
    auto sink2_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(sink2);

    graph->AddNode(source1_typed);
    graph->AddNode(source2_typed);
    graph->AddNode(merge1_typed);
    graph->AddNode(split1_typed);
    graph->AddNode(interior1_typed);
    graph->AddNode(interior2_typed);
    graph->AddNode(merge2_typed);
    graph->AddNode(sink1_typed);
    graph->AddNode(sink2_typed);
    
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 0>(graph, source1_typed, merge1_typed);
    PluginInfrastructure::AddEdge<SourceTestNode, 0, MergeTestNode, 1>(graph, source2_typed, merge1_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SplitTestNode, 0>(graph, merge1_typed, split1_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 0, InteriorTestNode, 0>(graph, split1_typed, interior1_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 1, InteriorTestNode, 0>(graph, split1_typed, interior2_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 0>(graph, interior1_typed, merge2_typed);
    PluginInfrastructure::AddEdge<InteriorTestNode, 0, MergeTestNode, 1>(graph, interior2_typed, merge2_typed);
    PluginInfrastructure::AddEdge<MergeTestNode, 0, SinkTestNode, 0>(graph, merge2_typed, sink1_typed);
    PluginInfrastructure::AddEdge<SplitTestNode, 0, SinkTestNode, 0>(graph, split1_typed, sink2_typed);
    
    return graph;
}

// ============================================================================
// Topology 10: Source Only (Single source node - edge case)
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildSourceOnly() {
    /**
     * Graph Structure:
     *
     *   ┌────────────────────────────┐
     *   │    Source Only             │
     *   └────────────────────────────┘
     *
     *   [Source]
     *      |
     *     Out(0) - no connections
     *
     * Data Flow:
     *   Source produces Message (output not consumed)
     *
     * Purpose:
     *   Edge case test for disconnected source
     *   Tests graph validation with orphaned nodes
     *   Note: Some graph execution engines may reject this as invalid
     */
    
    auto graph = std::make_shared<graph::GraphManager>();
    auto factory = PluginInfrastructure::GetFactory();
    
    auto source = std::make_shared<graph::NodeFacadeAdapter>(factory->CreateDynamicNode("SourceTestNode"));
    auto source_typed = std::make_shared<graph::NodeFacadeAdapterWrapper>(source);  
    
    graph->AddNode(source_typed);
    
    return graph;
}

// ============================================================================
// Topology Factory
// ============================================================================

std::shared_ptr<graph::GraphManager> TopologyBuilder::BuildTopology(TopologyType type) {
    switch (type) {
        case TopologyType::LinearSequential:
            return BuildLinearSequential();
        case TopologyType::MergeSimple:
            return BuildMergeSimple();
        case TopologyType::SplitSimple:
            return BuildSplitSimple();
        case TopologyType::DiamondComplex:
            return BuildDiamondComplex();
        case TopologyType::MultiPathSequential:
            return BuildMultiPathSequential();
        case TopologyType::InteriorToMerge:
            return BuildInteriorToMerge();
        case TopologyType::ParallelMergeWithInterior:
            return BuildParallelMergeWithInterior();
        case TopologyType::ComplexNetwork:
            return BuildComplexNetwork();
        case TopologyType::MinimalGraph:
            return BuildMinimalGraph();
        case TopologyType::SourceOnly:
            return BuildSourceOnly();
        default:
            throw std::invalid_argument("Unknown topology type");
    }
}

// ============================================================================
// Metadata
// ============================================================================

TopologyMetadata TopologyBuilder::GetTopologyMetadata(TopologyType type) {
    switch (type) {
        case TopologyType::LinearSequential:
            return {
                "LinearSequential",
                "Source -> Interior -> Sink sequential processing",
                3, 2,
                {"source", "interior", "sink"}
            };
        case TopologyType::MergeSimple:
            return {
                "MergeSimple",
                "Two sources merged into single output",
                4, 3,
                {"source1", "source2", "merge", "sink"}
            };
        case TopologyType::SplitSimple:
            return {
                "SplitSimple",
                "Single source split to two outputs",
                4, 3,
                {"source", "split", "sink1", "sink2"}
            };
        case TopologyType::DiamondComplex:
            return {
                "DiamondComplex",
                "Diamond pattern: split->process->merge",
                6, 5,
                {"source", "split", "interior1", "interior2", "merge", "sink"}
            };
        case TopologyType::MultiPathSequential:
            return {
                "MultiPathSequential",
                "Long sequential chain with multiple interior nodes",
                5, 4,
                {"source", "interior1", "interior2", "interior3", "sink"}
            };
        case TopologyType::InteriorToMerge:
            return {
                "InteriorToMerge",
                "Interior node output merged with direct source",
                5, 4,
                {"source1", "source2", "interior", "merge", "sink"}
            };
        case TopologyType::ParallelMergeWithInterior:
            return {
                "ParallelMergeWithInterior",
                "Parallel paths: one direct, one through interior to merge",
                5, 4,
                {"source1", "source2", "interior", "merge", "sink"}
            };
        case TopologyType::ComplexNetwork:
            return {
                "ComplexNetwork",
                "Complex multi-stage network with merge/split combinations",
                9, 8,
                {"source1", "source2", "merge1", "split1", "interior1", "interior2", "merge2", "sink1", "sink2"}
            };
        case TopologyType::MinimalGraph:
            return {
                "MinimalGraph",
                "Minimal valid graph: source to sink",
                2, 1,
                {"source", "sink"}
            };
        case TopologyType::SourceOnly:
            return {
                "SourceOnly",
                "Edge case: single disconnected source",
                1, 0,
                {"source"}
            };
        default:
            throw std::invalid_argument("Unknown topology type");
    }
}

std::vector<TopologyType> TopologyBuilder::GetAllTopologyTypes() {
    return {
        TopologyType::LinearSequential,
        TopologyType::MergeSimple,
        TopologyType::SplitSimple,
        TopologyType::DiamondComplex,
        TopologyType::MultiPathSequential,
        TopologyType::InteriorToMerge,
        TopologyType::ParallelMergeWithInterior,
        TopologyType::ComplexNetwork,
        TopologyType::MinimalGraph,
        TopologyType::SourceOnly
    };
}

// ============================================================================
// Topology Descriptions
// ============================================================================

std::string GetTopologyDiagram(TopologyType type) {
    switch (type) {
        case TopologyType::LinearSequential:
            return "[Source] → [Interior] → [Sink]";
        case TopologyType::MergeSimple:
            return "[Source1] ┐\n           ├→ [Merge] → [Sink]\n[Source2] ┘";
        case TopologyType::SplitSimple:
            return "[Source] → [Split] ┬→ [Sink1]\n                    └→ [Sink2]";
        case TopologyType::DiamondComplex:
            return "[Source] → [Split] ┬→ [Interior1] ┐\n                    └→ [Interior2] ┴→ [Merge] → [Sink]";
        case TopologyType::MultiPathSequential:
            return "[Source] → [Int1] → [Int2] → [Int3] → [Sink]";
        case TopologyType::InteriorToMerge:
            return "[Source1] → [Interior] ┐\n                        ├→ [Merge] → [Sink]\n           [Source2] ┘";
        case TopologyType::ParallelMergeWithInterior:
            return "[Source1] ┐\n           ├→ [Merge] → [Sink]\n[Source2] → [Interior] ┘";
        case TopologyType::ComplexNetwork:
            return "[S1] ┐    ┬→ [Int1] ┐\n      ├→ [M1] → [Split]        ├→ [M2] → [Sink1]\n[S2] ┘    └→ [Int2] ┘\n                             → [Sink2]";
        case TopologyType::MinimalGraph:
            return "[Source] → [Sink]";
        case TopologyType::SourceOnly:
            return "[Source]";
        default:
            return "Unknown topology";
    }
}

std::string GetTopologyDocumentation(TopologyType type) {
    auto metadata = TopologyBuilder::GetTopologyMetadata(type);
    std::ostringstream oss;
    
    oss << "Topology: " << metadata.name << "\n"
        << "Description: " << metadata.description << "\n"
        << "Nodes: " << metadata.expected_node_count << "\n"
        << "Edges: " << metadata.expected_edge_count << "\n"
        << "Diagram:\n  " << GetTopologyDiagram(type) << "\n";
    
    return oss.str();
}

} // namespace test
