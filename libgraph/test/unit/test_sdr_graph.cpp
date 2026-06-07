/**
 * @file test_sdr_graph.cpp
 * @brief Dynamic plugin test for a graph-based SDR pipeline.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>

#include "dsp/FFTNode.hpp"
#include "dsp/SineSignalNode.hpp"
#include "dsp/SpectrumSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManager.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/NodeProvider.hpp"
#include "test/PluginInfrastructure.hpp"

namespace {

constexpr size_t kPacketSize = 256;

using graph::GraphExecutorBuilder;
using graph::GraphManager;
using graph::NodeFacadeAdapter;
using graph::NodeFacadeAdapterWrapper;
using test::PluginInfrastructure;
using SineSourceNode = dsp::SineSignalNode<kPacketSize>;
using FFTProcessorNode = dsp::FFTNode<float, kPacketSize>;
using AnalyzerSinkNode = dsp::SpectrumSinkNode<float, kPacketSize>;

std::shared_ptr<NodeFacadeAdapterWrapper> CreatePluginNodeWithFallback(
    const std::initializer_list<const char*>& candidate_types,
    std::string& failure_reason) {
    std::shared_ptr<graph::INodeProvider> provider = PluginInfrastructure::GetProvider();
    if (!provider) {
        failure_reason = "Plugin provider is null";
        return nullptr;
    }

    std::string unavailable_types;
    for (const char* candidate : candidate_types) {
        if (!candidate) {
            continue;
        }

        const std::string type(candidate);
        if (!provider->IsNodeTypeAvailable(type)) {
            if (!unavailable_types.empty()) {
                unavailable_types += ", ";
            }
            unavailable_types += type;
            continue;
        }

        auto node = provider->CreateNodeExpected(type);
        if (!node) {
            failure_reason = "CreateNodeExpected failed for type: " + type;
            continue;
        }

        try {
            auto adapter = std::make_shared<NodeFacadeAdapter>(std::move(node).value());
            return std::make_shared<NodeFacadeAdapterWrapper>(adapter);
        } catch (const std::exception& ex) {
            failure_reason = std::string("NodeFacadeAdapter construction failed for type ") +
                type + ": " + ex.what();
        } catch (...) {
            failure_reason = "NodeFacadeAdapter construction failed for type: " + type;
        }
    }

    if (failure_reason.empty()) {
        failure_reason = "No candidate node type available";
        if (!unavailable_types.empty()) {
            failure_reason += ": " + unavailable_types;
        }
    }

    return nullptr;
}

std::shared_ptr<GraphManager> BuildSDRGraph(
    std::shared_ptr<NodeFacadeAdapterWrapper>& source_wrapper,
    std::shared_ptr<NodeFacadeAdapterWrapper>& fft_wrapper,
    std::shared_ptr<NodeFacadeAdapterWrapper>& analyzer_wrapper,
    std::string& failure_reason) {
    auto graph = std::make_shared<GraphManager>();

    source_wrapper = CreatePluginNodeWithFallback(
        {"SineSignalNode<256>", "SineSignalNode<>"},
        failure_reason);
    if (!source_wrapper) {
        failure_reason = "Source plugin unavailable: " + failure_reason;
        return nullptr;
    }

    fft_wrapper = CreatePluginNodeWithFallback(
        {"FFTNode<256>", "FFTNode<float, 256>"},
        failure_reason);
    if (!fft_wrapper) {
        failure_reason = "FFT plugin unavailable: " + failure_reason;
        return nullptr;
    }

    analyzer_wrapper = CreatePluginNodeWithFallback(
        {"SpectrumSinkNode<256>", "SpectrumSinkNode<float, 256>"},
        failure_reason);
    if (!analyzer_wrapper) {
        failure_reason = "Analyzer plugin unavailable: " + failure_reason;
        return nullptr;
    }

    if (!source_wrapper || !fft_wrapper || !analyzer_wrapper) {
        if (failure_reason.empty()) {
            failure_reason = "One or more required SDR plugin nodes are null";
        }
        return nullptr;
    }

    graph->AddNode(source_wrapper);
    graph->AddNode(fft_wrapper);
    graph->AddNode(analyzer_wrapper);

    const bool iq_edge_added =
        PluginInfrastructure::AddEdge<SineSourceNode, 0, FFTProcessorNode, 0>(
            graph, source_wrapper, fft_wrapper);
    const bool spectrum_edge_added =
        PluginInfrastructure::AddEdge<FFTProcessorNode, 0, AnalyzerSinkNode, 0>(
            graph, fft_wrapper, analyzer_wrapper);

    if (!iq_edge_added || !spectrum_edge_added) {
        failure_reason = "Failed to wire dynamic plugin edges (typed extraction failed)";
        return nullptr;
    }

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
        << "SpectrumSinkNode should signal completion after receiving the spectrum";

    auto stop_result = executor->Stop();
    ASSERT_TRUE(stop_result.success) << stop_result.message;

    auto join_result = executor->Join();
    ASSERT_TRUE(join_result.success) << join_result.message;
}

TEST(SDRGraphTest, DynamicSineIQToFFTToAnalyzerProducesSpectrum) {
    std::shared_ptr<NodeFacadeAdapterWrapper> source_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> fft_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> analyzer_wrapper;
    std::string build_error;
    auto graph = BuildSDRGraph(source_wrapper, fft_wrapper, analyzer_wrapper, build_error);
    if (!graph) {
        GTEST_SKIP() << "Skipping dynamic SDR plugin test: " << build_error;
    }

    ExecuteSuccessfully(graph);

    auto analyzer = analyzer_wrapper->GetNode<AnalyzerSinkNode>();
    ASSERT_NE(nullptr, analyzer);

    EXPECT_GE(analyzer->GetFrameCount(), 1u);

    const auto spectrum = analyzer->GetLatestSpectrum();
    ASSERT_TRUE(spectrum.has_value());
    EXPECT_TRUE(spectrum->IsValid());
    EXPECT_EQ(kPacketSize / 2, spectrum->magnitudes.size());
    EXPECT_DOUBLE_EQ(48000.0, spectrum->sample_rate_hz);
    EXPECT_GT(spectrum->peak_magnitude, 0.0f);
    EXPECT_LT(spectrum->peak_frequency_hz, 24000.0f);
}

TEST(SDRGraphTest, DynamicTopologyHasExpectedNodesAndEdges) {
    std::shared_ptr<NodeFacadeAdapterWrapper> source_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> fft_wrapper;
    std::shared_ptr<NodeFacadeAdapterWrapper> analyzer_wrapper;
    std::string build_error;
    auto graph = BuildSDRGraph(source_wrapper, fft_wrapper, analyzer_wrapper, build_error);
    if (!graph) {
        GTEST_SKIP() << "Skipping dynamic SDR topology test: " << build_error;
    }

    ASSERT_EQ(3u, graph->GetNodes().size());
    ASSERT_EQ(2u, graph->GetEdges().size());

    EXPECT_EQ("SineSignalNode<>", source_wrapper->GetType());
    EXPECT_EQ("FFTNode<float, 256>", fft_wrapper->GetType());
    EXPECT_EQ("SpectrumSinkNode<float, 256>", analyzer_wrapper->GetType());

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
