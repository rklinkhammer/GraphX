// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <complex>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "dsp/fhss/CPSMBranchMetricNode.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/FHSSPorts.hpp"

namespace {

using accelgraph::fhss::AccelFhssBranchMetricNode;
using accelgraph::fhss::AccelFhssBranchMetricSinkNode;
using accelgraph::fhss::FHSSPerChannelPulseEvidenceToken;

std::filesystem::path FhssBranchMetricCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_branch_metric_cpu_topology.json");
}

std::filesystem::path FhssBranchMetricMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_branch_metric_metal_topology.json");
}

std::filesystem::path FhssBranchMetricMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_branch_metric_metal_allow_fallback_topology.json");
}

std::filesystem::path FhssBranchMetricCudaTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_branch_metric_cuda_topology.json");
}

std::filesystem::path FhssBranchMetricCudaAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_branch_metric_cuda_allow_fallback_topology.json");
}

nlohmann::json MakeBranchMetricJsonConfig(const std::string& backend,
                                          bool strict_fallback,
                                          const std::string& fallback_policy,
                                          const std::string& provider_id) {
    return {
        {"backend", backend},
        {"strict_fallback", strict_fallback},
        {"fallback_policy", fallback_policy},
        {"provider_id", provider_id},
        {"session_key", "graph.default"},
        {"cuda_device_ordinal", 0u},
        {"symbol_count", 32u},
        {"modulation_index", 0.5},
        {"initial_phase_state", 0u},
        {"check_terminal_phase", false},
        {"expected_terminal_phase_state", 0u}
    };
}

FHSSPerChannelPulseEvidenceToken MakePulseEvidenceInputToken() {
    FHSSPerChannelPulseEvidenceToken token{};
    token.token_id = 41;

    dsp::fhss::FHSSGraphXSampleTimeMap map{};
    map.has_input_global_start_sample = true;
    map.input_packet_global_start_sample = 4096;
    map.output_start_sample = 4096;
    map.decimation_factor = 1;
    map.group_delay_input_samples = 0;
    map.input_sample_rate_hz = 500000000.0;
    map.output_sample_rate_hz = 500000000.0;

    auto samples = std::make_shared<const std::vector<std::complex<double>>>(
        std::vector<std::complex<double>>(dsp::fhss::FHSSProtocolConstants::kPulseWidthSamples, {1.0, 0.0}));

    dsp::fhss::FHSSGraphXPulseMetadata pulse{};
    pulse.timing.global_start_sample = 4096;
    pulse.timing.global_end_sample =
        4096 + dsp::fhss::FHSSProtocolConstants::kPulseWidthSamples;
    pulse.timing.duration_samples = dsp::fhss::FHSSProtocolConstants::kPulseWidthSamples;
    pulse.timing.channel_start_sample = 4096;
    pulse.timing.channel_id = 24;
    pulse.timing.sample_time_map = map;

    pulse.frequency.frequency_index = 24;
    pulse.frequency.rf_frequency_hz = 1240000000.0;
    pulse.frequency.iq_offset_frequency_hz = 0.0;
    pulse.frequency.estimated_center_frequency_hz = 0.0;

    pulse.downconverter_passthrough = true;
    pulse.downconverter_translation_frequency_hz = 0.0;
    pulse.power_db = -10.0;
    pulse.snr_db = 30.0;
    pulse.noise_floor_db = -120.0;
    pulse.detector_id = 17;
    pulse.packet_sequence = 19;
    pulse.confidence = 0.95;

    auto evidence =
        dsp::fhss::FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), map);

    token.sidecar.channel.channel_id = 24;
    token.sidecar.channel.frequency_index = 24;
    token.sidecar.channel.rf_frequency_hz = 1240000000.0;
    token.sidecar.channel.channel_sample_rate_hz = 500000000.0;
    token.sidecar.channel.sample_time_map = map;
    token.sidecar.detected_pulses.push_back(pulse);
    token.sidecar.pulse_evidence.push_back(evidence);
    token.sidecar.channel_iq = evidence;
    return token;
}

dsp::fhss::FHSSPulseCandidateToken MakePulseCandidateInputToken(
    const FHSSPerChannelPulseEvidenceToken& evidence_token) {
    dsp::fhss::FHSSPulseCandidateToken token{};
    token.token_id = evidence_token.token_id;
    token.sidecar.correlation = evidence_token.sidecar.correlation;

    for (std::size_t i = 0; i < evidence_token.sidecar.detected_pulses.size(); ++i) {
        dsp::fhss::FHSSGraphXPulseCandidate candidate{};
        candidate.pulse = evidence_token.sidecar.detected_pulses[i];
        candidate.complex_evidence = evidence_token.sidecar.pulse_evidence[i];
        token.sidecar.ordered_candidates.push_back(std::move(candidate));
    }
    token.sidecar.globally_ordered = true;
    token.sidecar.unsupported_overlap_rejected = true;
    return token;
}

}  // namespace

TEST(AccelGraphFhssBranchMetricTest, CpuReferenceParityMatchesDspCpsmBranchMetricNode) {
    const auto input = MakePulseEvidenceInputToken();
    const auto candidate_input = MakePulseCandidateInputToken(input);

    AccelFhssBranchMetricNode accel_node;
    const auto accel_json = MakeBranchMetricJsonConfig("cpu", true, "strict", "cpu.default");
    const graph::JsonView accel_view{accel_json};
    accel_node.Configure(accel_view);

    dsp::fhss::CPSMBranchMetricNode reference_node;
    dsp::fhss::CPSMDecoderConfig reference_config{};
    reference_node.SetConfig(reference_config);

    const auto accel = accel_node.Transfer(input,
                                           std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    const auto reference = reference_node.Transfer(candidate_input,
                                                   std::integral_constant<std::size_t, 0>{},
                                                   std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(accel.has_value());
    ASSERT_TRUE(reference.has_value());

    ASSERT_FALSE(accel->sidecar.pulse_metrics.empty());
    ASSERT_FALSE(reference->sidecar.pulse_metrics.empty());

    const auto& accel_metric = accel->sidecar.pulse_metrics.front();
    const auto& ref_metric = reference->sidecar.pulse_metrics.front();

    EXPECT_EQ(accel_metric.candidate.pulse.timing.channel_id,
              ref_metric.candidate.pulse.timing.channel_id);
    EXPECT_EQ(accel_metric.candidate.pulse.frequency.frequency_index,
              ref_metric.candidate.pulse.frequency.frequency_index);
    EXPECT_EQ(accel_metric.candidate.pulse.timing.global_start_sample,
              ref_metric.candidate.pulse.timing.global_start_sample);
    EXPECT_EQ(accel_metric.candidate.pulse.timing.duration_samples,
              ref_metric.candidate.pulse.timing.duration_samples);
    EXPECT_DOUBLE_EQ(accel_metric.candidate.pulse.power_db,
                     ref_metric.candidate.pulse.power_db);
    EXPECT_DOUBLE_EQ(accel_metric.candidate.pulse.snr_db,
                     ref_metric.candidate.pulse.snr_db);
    EXPECT_DOUBLE_EQ(accel_metric.candidate.pulse.noise_floor_db,
                     ref_metric.candidate.pulse.noise_floor_db);
    EXPECT_EQ(accel_metric.branch_costs.size(), ref_metric.branch_costs.size());
    ASSERT_FALSE(accel_metric.branch_costs.empty());
    for (std::size_t i = 0; i < accel_metric.branch_costs.size(); ++i) {
        EXPECT_NEAR(accel_metric.branch_costs[i], ref_metric.branch_costs[i], 1.0e-12);
    }

    EXPECT_EQ(accel_node.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(accel_node.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(accel_node.UsedFallback());
    EXPECT_TRUE(accel_node.FallbackDiagnostic().empty());
}

TEST(AccelGraphFhssBranchMetricTest, CpuTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = FhssBranchMetricCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto node = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssBranchMetricSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());
    ASSERT_FALSE(output->sidecar.pulse_metrics.empty());

    const auto& metric = output->sidecar.pulse_metrics.front();
    EXPECT_EQ(metric.candidate.pulse.timing.channel_id, 24u);
    EXPECT_EQ(metric.candidate.pulse.frequency.frequency_index, 24u);
    EXPECT_EQ(metric.candidate.pulse.detector_id, 17u);
    EXPECT_EQ(metric.candidate.pulse.packet_sequence, 19u);
    EXPECT_FALSE(metric.branch_costs.empty());

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssBranchMetricTest, MetalStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssBranchMetricMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssBranchMetricMetalNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssBranchMetricTest, MetalAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssBranchMetricMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssBranchMetricSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_TRUE(node->UsedFallback());
    EXPECT_FALSE(node->FallbackDiagnostic().empty());
}

TEST(AccelGraphFhssBranchMetricTest, CudaStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssBranchMetricCudaTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedCudaDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssBranchMetricCudaNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cuda);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssBranchMetricTest, CudaAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssBranchMetricCudaAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(20));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssBranchMetricNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssBranchMetricSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cuda);
    if (node->SelectedBackend() == accelgraph::AcceleratorBackend::Cpu) {
        EXPECT_TRUE(node->UsedFallback());
        EXPECT_FALSE(node->FallbackDiagnostic().empty());
    } else {
        EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cuda);
        EXPECT_FALSE(node->UsedFallback());
    }
}

TEST(AccelGraphFhssBranchMetricTest, DescriptorFieldsDeclareFullConfigSurface) {
    const auto fields = AccelFhssBranchMetricNode::Fields();
    auto has_field = [&](std::string_view name) {
        for (const auto& field : fields) {
            if (field.name == name) {
                return true;
            }
        }
        return false;
    };

    EXPECT_TRUE(has_field("backend"));
    EXPECT_TRUE(has_field("strict_fallback"));
    EXPECT_TRUE(has_field("fallback_policy"));
    EXPECT_TRUE(has_field("provider_id"));
    EXPECT_TRUE(has_field("session_key"));
    EXPECT_TRUE(has_field("cuda_device_ordinal"));
    EXPECT_TRUE(has_field("symbol_count"));
    EXPECT_TRUE(has_field("modulation_index"));
    EXPECT_TRUE(has_field("initial_phase_state"));
    EXPECT_TRUE(has_field("check_terminal_phase"));
    EXPECT_TRUE(has_field("expected_terminal_phase_state"));
}
