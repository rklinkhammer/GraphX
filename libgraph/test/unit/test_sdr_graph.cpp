/**
 * @file test_sdr_graph.cpp
 * @brief Dynamic plugin test for a graph-based SDR pipeline.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "test/PluginInfrastructure.hpp"
#include "test/SDRTestNodes.hpp"

namespace {

using graph::GraphExecutorBuilder;
using graph::GraphManager;
using graph::NodeFacadeAdapter;
using graph::NodeFacadeAdapterWrapper;
using test::PluginInfrastructure;
using test::sdr::AnalyzerSinkNode;
using test::sdr::FFTPowerSpectrumNode;
using test::sdr::SineIQSourceNode;

std::shared_ptr<NodeFacadeAdapterWrapper> CreatePluginNode(const std::string& type) {
    auto factory = PluginInfrastructure::GetFactory();
    auto adapter = std::make_shared<NodeFacadeAdapter>(factory->CreateDynamicNode(type));
    return std::make_shared<NodeFacadeAdapterWrapper>(adapter);
}

std::shared_ptr<GraphManager> BuildSDRGraph(
    std::shared_ptr<NodeFacadeAdapterWrapper>& source_wrapper,
    std::shared_ptr<NodeFacadeAdapterWrapper>& fft_wrapper,
    std::shared_ptr<NodeFacadeAdapterWrapper>& analyzer_wrapper) {
    auto graph = std::make_shared<GraphManager>();

    source_wrapper = CreatePluginNode("SineIQSourceNode");
    fft_wrapper = CreatePluginNode("FFTPowerSpectrumNode");
    analyzer_wrapper = CreatePluginNode("AnalyzerSinkNode");

    graph->AddNode(source_wrapper);
    graph->AddNode(fft_wrapper);
    graph->AddNode(analyzer_wrapper);

    const bool iq_edge_added =
        PluginInfrastructure::AddEdge<SineIQSourceNode, 0, FFTPowerSpectrumNode, 0>(
            graph, source_wrapper, fft_wrapper);
    const bool spectrum_edge_added =
        PluginInfrastructure::AddEdge<FFTPowerSpectrumNode, 0, AnalyzerSinkNode, 0>(
            graph, fft_wrapper, analyzer_wrapper);

    EXPECT_TRUE(iq_edge_added);
    EXPECT_TRUE(spectrum_edge_added);

    return graph;
}

void ExecuteSuccessfully(const std::shared_ptr<GraphManager>& graph) {
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .WithExecutorTimeout(std::chrono::seconds(5))
        .Build();
    ASSERT_NE(nullptr, executor);

    auto init_result = executor->Init();
    ASSERT_TRUE(init_result.success) << init_result.message;

    auto start_result = executor->Start();
    ASSERT_TRUE(start_result.success) << start_result.message;

    auto run_result = executor->Run();
    ASSERT_TRUE(run_result.success) << run_result.message;
    EXPECT_LT(run_result.elapsed_time_ms, 5000u)
        << "AnalyzerSinkNode should signal completion after receiving the spectrum";

    auto stop_result = executor->Stop();
    ASSERT_TRUE(stop_result.success) << stop_result.message;

    auto join_result = executor->Join();
    ASSERT_TRUE(join_result.success) << join_result.message;
}

TEST(SDRGraphTest, DynamicSineIQToFFTToAnalyzerDetectsToneBin) {
    std::shared_ptr<NodeFacadeAdapterWrapper> source_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> fft_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> analyzer_wrapper;
    auto graph = BuildSDRGraph(source_wrapper, fft_wrapper, analyzer_wrapper);
    ASSERT_NE(nullptr, graph);

    ExecuteSuccessfully(graph);

    auto analyzer = analyzer_wrapper->GetNode<AnalyzerSinkNode>();
    ASSERT_NE(nullptr, analyzer);

    constexpr size_t kToneBin = 5;
    constexpr size_t kSampleCount = 64;
    constexpr double kSampleRateHz = 64000.0;

    EXPECT_EQ(1u, analyzer->GetSpectrumCount());
    EXPECT_EQ(kToneBin, analyzer->GetPeakBin());

    const auto spectrum = analyzer->GetLastSpectrum();
    ASSERT_EQ(kSampleCount, spectrum.bins.size());
    EXPECT_DOUBLE_EQ(kSampleRateHz, spectrum.sample_rate_hz);
    EXPECT_NEAR(static_cast<double>(kSampleCount * kSampleCount), spectrum.bins[kToneBin], 1.0e-6);
}

TEST(SDRGraphTest, DynamicTopologyHasExpectedNodesAndEdges) {
    std::shared_ptr<NodeFacadeAdapterWrapper> source_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> fft_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> analyzer_wrapper;
    auto graph = BuildSDRGraph(source_wrapper, fft_wrapper, analyzer_wrapper);
    ASSERT_NE(nullptr, graph);

    ASSERT_EQ(3u, graph->GetNodes().size());
    ASSERT_EQ(2u, graph->GetEdges().size());

    EXPECT_EQ("SineIQSourceNode", source_wrapper->GetType());
    EXPECT_EQ("FFTPowerSpectrumNode", fft_wrapper->GetType());
    EXPECT_EQ("AnalyzerSinkNode", analyzer_wrapper->GetType());

    const auto* iq_edge = graph->GetEdgeMetadata(0);
    ASSERT_NE(nullptr, iq_edge);
    EXPECT_EQ(0u, iq_edge->source_node_id);
    EXPECT_EQ(0u, iq_edge->source_port_id);
    EXPECT_EQ(1u, iq_edge->dest_node_id);
    EXPECT_EQ(0u, iq_edge->dest_port_id);

    const auto* spectrum_edge = graph->GetEdgeMetadata(1);
    ASSERT_NE(nullptr, spectrum_edge);
    EXPECT_EQ(1u, spectrum_edge->source_node_id);
    EXPECT_EQ(0u, spectrum_edge->source_port_id);
    EXPECT_EQ(2u, spectrum_edge->dest_node_id);
    EXPECT_EQ(0u, spectrum_edge->dest_port_id);
}

} // namespace
