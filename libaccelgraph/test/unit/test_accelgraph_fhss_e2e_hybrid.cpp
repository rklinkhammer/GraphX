// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "dsp/fhss/CPSMViterbiDecoderNode.hpp"
#include "dsp/fhss/FHSSMessageAssemblerNode.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "dsp/fhss/FHSSPreambleDetectorNode.hpp"
#include "dsp/fhss/FHSSPulseWordDecoderNode.hpp"

namespace {

using accelgraph::fhss::AccelFhssBranchMetricNode;
using accelgraph::fhss::AccelFhssChannelizerNode;
using accelgraph::fhss::AccelFhssDownconverterNode;
using accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode;

std::filesystem::path FhssE2EHybridCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_e2e_hybrid_cpu_topology.json");
}

std::filesystem::path FhssE2EHybridMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_e2e_hybrid_metal_topology.json");
}

std::filesystem::path FhssE2EHybridMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_e2e_hybrid_metal_allow_fallback_topology.json");
}

std::filesystem::path FhssE2EHybridCudaTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_e2e_hybrid_cuda_topology.json");
}

std::filesystem::path FhssE2EHybridCudaAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_e2e_hybrid_cuda_allow_fallback_topology.json");
}

bool HasField(const nlohmann::json& value, const char* key) {
    return value.contains(key) && !value.at(key).is_null();
}

bool IsExpectedE2EMetalDiagnostic(const std::string& message) {
    return accelgraph::test::IsExpectedMetalDiagnostic(message) ||
           accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
           message.find(accelgraph::fhss::kFhssDownconverterMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssChannelizerMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssBranchMetricMetalNativeNotImplementedDiagnostic) != std::string::npos;
}

void ExpectCpuDecodeTailWired(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    auto viterbi = accelgraph::test::ResolveNode<dsp::fhss::CPSMViterbiDecoderNode>(graph_manager);
    auto word_decoder = accelgraph::test::ResolveNode<dsp::fhss::FHSSPulseWordDecoderNode>(graph_manager);
    auto preamble_detector = accelgraph::test::ResolveNode<dsp::fhss::FHSSPreambleDetectorNode>(graph_manager);
    auto assembler = accelgraph::test::ResolveNode<dsp::fhss::FHSSMessageAssemblerNode>(graph_manager);

    ASSERT_NE(viterbi, nullptr);
    ASSERT_NE(word_decoder, nullptr);
    ASSERT_NE(preamble_detector, nullptr);
    ASSERT_NE(assembler, nullptr);
}

void ExpectSinkDiagnosticsContainHybridProvenance(const dsp::fhss::FHSSMessageSinkNode& sink) {
    const auto diagnostics_view = sink.GetDiagnostics();
    const auto& diagnostics = diagnostics_view.Raw();

    ASSERT_TRUE(diagnostics.is_object());
    EXPECT_TRUE(HasField(diagnostics, "pulse_count"));
    EXPECT_TRUE(HasField(diagnostics, "preamble_lock"));
    EXPECT_TRUE(HasField(diagnostics, "decoded_pulses"));

    ASSERT_TRUE(diagnostics.at("decoded_pulses").is_array());
    ASSERT_FALSE(diagnostics.at("decoded_pulses").empty());

    const auto& first = diagnostics.at("decoded_pulses").front();
    ASSERT_TRUE(first.is_object());
    EXPECT_TRUE(HasField(first, "global_start_sample"));
    EXPECT_TRUE(HasField(first, "duration_samples"));
    EXPECT_TRUE(HasField(first, "frequency_index"));
    EXPECT_TRUE(HasField(first, "channel_id"));
    EXPECT_TRUE(HasField(first, "downconverter_passthrough"));
    EXPECT_TRUE(HasField(first, "downconverter_translation_frequency_hz"));
    EXPECT_TRUE(HasField(first, "viterbi_path_metric"));
    EXPECT_TRUE(HasField(first, "decoded_value"));
    EXPECT_TRUE(HasField(first, "sample_time_mapping"));

    ASSERT_TRUE(first.at("sample_time_mapping").is_object());
    EXPECT_TRUE(HasField(first.at("sample_time_mapping"), "input_packet_global_start_sample"));
    EXPECT_TRUE(HasField(first.at("sample_time_mapping"), "output_start_sample"));
    EXPECT_TRUE(HasField(first.at("sample_time_mapping"), "decimation_factor"));
    EXPECT_TRUE(HasField(first.at("sample_time_mapping"), "group_delay_input_samples"));

    EXPECT_FALSE(diagnostics.at("decoded_pulses").empty());
    if (diagnostics.contains("message_status") && diagnostics.at("message_status").is_string()) {
        EXPECT_FALSE(diagnostics.at("message_status").get<std::string>().empty());
    }
}

void ExpectAccelStageCpu(const AccelFhssDownconverterNode& downconverter,
                        const AccelFhssChannelizerNode& channelizer,
                        const AccelFhssPerChannelPulseDetectorNode& detector,
                        const AccelFhssBranchMetricNode& branch_metric) {
    EXPECT_EQ(downconverter.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(channelizer.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(detector.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(branch_metric.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);

    EXPECT_EQ(downconverter.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(channelizer.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(detector.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(branch_metric.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);

    EXPECT_FALSE(downconverter.UsedFallback());
    EXPECT_FALSE(channelizer.UsedFallback());
    EXPECT_FALSE(detector.UsedFallback());
    EXPECT_FALSE(branch_metric.UsedFallback());
}

void ExpectAccelStageMetalRequest(const AccelFhssDownconverterNode& downconverter,
                                  const AccelFhssChannelizerNode& channelizer,
                                  const AccelFhssPerChannelPulseDetectorNode& detector,
                                  const AccelFhssBranchMetricNode& branch_metric) {
    EXPECT_EQ(downconverter.RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(channelizer.RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(detector.RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(branch_metric.RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
}

void ExpectFallbackState(const char* stage_name,
                         accelgraph::AcceleratorBackend selected,
                         bool used_fallback,
                         const std::string& diagnostic) {
    SCOPED_TRACE(stage_name);
    if (selected == accelgraph::AcceleratorBackend::Cpu) {
        EXPECT_TRUE(used_fallback);
        EXPECT_FALSE(diagnostic.empty());
    } else {
        EXPECT_EQ(selected, accelgraph::AcceleratorBackend::Metal);
        EXPECT_FALSE(used_fallback);
    }
}

}  // namespace

TEST(AccelGraphFhssHybridPipelineTest, CpuTopologyExecutesWithAccelFrontendAndCpuDecodeTail) {
    const auto config_path = FhssE2EHybridCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto downconverter = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    auto channelizer = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    auto detector = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto branch_metric = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    auto message_sink = accelgraph::test::ResolveNode<dsp::fhss::FHSSMessageSinkNode>(graph_manager);

    ASSERT_NE(downconverter, nullptr);
    ASSERT_NE(channelizer, nullptr);
    ASSERT_NE(detector, nullptr);
    ASSERT_NE(branch_metric, nullptr);
    ASSERT_NE(message_sink, nullptr);

    ExpectCpuDecodeTailWired(graph_manager);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    ExpectAccelStageCpu(*downconverter, *channelizer, *detector, *branch_metric);
    ExpectSinkDiagnosticsContainHybridProvenance(*message_sink);
}

TEST(AccelGraphFhssHybridPipelineTest, MetalStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssE2EHybridMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsExpectedE2EMetalDiagnostic(message)) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto downconverter = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    auto channelizer = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    auto detector = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto branch_metric = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);

    ASSERT_NE(downconverter, nullptr);
    ASSERT_NE(channelizer, nullptr);
    ASSERT_NE(detector, nullptr);
    ASSERT_NE(branch_metric, nullptr);

    ExpectAccelStageMetalRequest(*downconverter, *channelizer, *detector, *branch_metric);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(downconverter->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(channelizer->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(detector->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(branch_metric->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(downconverter->UsedFallback());
    EXPECT_FALSE(channelizer->UsedFallback());
    EXPECT_FALSE(detector->UsedFallback());
    EXPECT_FALSE(branch_metric->UsedFallback());
}

TEST(AccelGraphFhssHybridPipelineTest, MetalAllowFallbackExecutesAndReportsPerStageFallback) {
    const auto config_path = FhssE2EHybridMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto downconverter = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    auto channelizer = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    auto detector = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto branch_metric = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    auto message_sink = accelgraph::test::ResolveNode<dsp::fhss::FHSSMessageSinkNode>(graph_manager);

    ASSERT_NE(downconverter, nullptr);
    ASSERT_NE(channelizer, nullptr);
    ASSERT_NE(detector, nullptr);
    ASSERT_NE(branch_metric, nullptr);
    ASSERT_NE(message_sink, nullptr);

    ExpectCpuDecodeTailWired(graph_manager);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    ExpectAccelStageMetalRequest(*downconverter, *channelizer, *detector, *branch_metric);

    ExpectFallbackState("downconverter", downconverter->SelectedBackend(), downconverter->UsedFallback(),
                        downconverter->FallbackDiagnostic());
    ExpectFallbackState("channelizer", channelizer->SelectedBackend(), channelizer->UsedFallback(),
                        channelizer->FallbackDiagnostic());
    ExpectFallbackState("detector", detector->SelectedBackend(), detector->UsedFallback(),
                        detector->FallbackDiagnostic());
    ExpectFallbackState("branch_metric", branch_metric->SelectedBackend(), branch_metric->UsedFallback(),
                        branch_metric->FallbackDiagnostic());

    ExpectSinkDiagnosticsContainHybridProvenance(*message_sink);
}

TEST(AccelGraphFhssHybridPipelineTest, StrictCudaTopologyIsIntentionallyUnsupportedUntilNativePathIsComplete) {
    EXPECT_FALSE(std::filesystem::exists(FhssE2EHybridCudaTopologyConfigPath()));
    EXPECT_FALSE(std::filesystem::exists(FhssE2EHybridCudaAllowFallbackTopologyConfigPath()));
}
