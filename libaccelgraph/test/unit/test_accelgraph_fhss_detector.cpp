// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <complex>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"
#include "dsp/fhss/PerChannelPulseDetectorNode.hpp"

namespace {

using accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode;
using accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode;

std::filesystem::path FhssDetectorCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_detector_cpu_topology.json");
}

std::filesystem::path FhssDetectorMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_detector_metal_topology.json");
}

std::filesystem::path FhssDetectorMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_detector_metal_allow_fallback_topology.json");
}

std::filesystem::path FhssDetectorCudaTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_detector_cuda_topology.json");
}

std::filesystem::path FhssDetectorCudaAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_detector_cuda_allow_fallback_topology.json");
}

nlohmann::json MakeDetectorJsonConfig(const std::string& backend,
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
        {"detector_id", 17u},
        {"packet_sequence", 19u},
        {"min_power_linear", 1.0e-18},
        {"min_symbol_coherence", 0.1},
        {"noise_floor_db", -120.0},
        {"nominal_bandwidth_hz", 5000000.0},
        {"max_pulse_input_samples", 2048u}
    };
}

dsp::fhss::FHSSChannelizedIqToken MakeChannelizedInputToken() {
    const std::size_t sample_count = dsp::fhss::FHSSProtocolConstants::kPulseWidthSamples;
    auto samples = std::make_shared<const std::vector<std::complex<double>>>(
        std::vector<std::complex<double>>(sample_count, {1.0, 0.0}));

    dsp::fhss::FHSSGraphXSampleTimeMap map{};
    map.has_input_global_start_sample = true;
    map.input_packet_global_start_sample = 4096;
    map.output_start_sample = 4096;
    map.decimation_factor = 1;
    map.group_delay_input_samples = 0;
    map.input_sample_rate_hz = 500000000.0;
    map.output_sample_rate_hz = 500000000.0;

    dsp::fhss::FHSSChannelizedIqToken token{};
    token.token_id = 11;
    token.sidecar.channel.channel_id = 24;
    token.sidecar.channel.frequency_index = 24;
    token.sidecar.channel.rf_frequency_hz = 1240000000.0;
    token.sidecar.channel.iq_offset_frequency_hz = 0.0;
    token.sidecar.channel.downconverter_passthrough = true;
    token.sidecar.channel.downconverter_translation_frequency_hz = 0.0;
    token.sidecar.channel.channel_sample_rate_hz = 500000000.0;
    token.sidecar.channel.decimation_factor = 1;
    token.sidecar.channel.filter_group_delay_input_samples = 0;
    token.sidecar.channel.input_global_start_sample = 4096;
    token.sidecar.channel.channel_global_start_sample = 4096;
    token.sidecar.channel.sample_time_map = map;
    token.sidecar.iq = dsp::fhss::FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), map);
    return token;
}

}  // namespace

TEST(AccelGraphFhssDetectorTest, CpuReferenceParityMatchesDspPerChannelDetector) {
    const auto input = MakeChannelizedInputToken();

    AccelFhssPerChannelPulseDetectorNode accel_node;
    const auto accel_json = MakeDetectorJsonConfig("cpu", true, "strict", "cpu.default");
    const graph::JsonView accel_view{accel_json};
    accel_node.Configure(accel_view);

    dsp::fhss::PerChannelPulseDetectorNode ref_node;
    const auto ref_json = MakeDetectorJsonConfig("cpu", true, "strict", "cpu.default");
    const graph::JsonView ref_view{ref_json};
    ref_node.Configure(ref_view);

    const auto accel = accel_node.Transfer(input,
                                           std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    const auto reference = ref_node.Transfer(input,
                                             std::integral_constant<std::size_t, 0>{},
                                             std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(accel.has_value());
    ASSERT_TRUE(reference.has_value());

    EXPECT_EQ(accel->sidecar.channel.channel_id, reference->sidecar.channel.channel_id);
    EXPECT_EQ(accel->sidecar.channel.frequency_index, reference->sidecar.channel.frequency_index);
    EXPECT_EQ(accel->sidecar.channel.sample_time_map.input_packet_global_start_sample,
              reference->sidecar.channel.sample_time_map.input_packet_global_start_sample);
    EXPECT_DOUBLE_EQ(accel->sidecar.channel.channel_sample_rate_hz,
                     reference->sidecar.channel.channel_sample_rate_hz);

    ASSERT_EQ(accel->sidecar.detected_pulses.size(), reference->sidecar.detected_pulses.size());
    ASSERT_EQ(accel->sidecar.pulse_evidence.size(), reference->sidecar.pulse_evidence.size());

    ASSERT_FALSE(accel->sidecar.detected_pulses.empty());
    const auto& accel_pulse = accel->sidecar.detected_pulses.front();
    const auto& ref_pulse = reference->sidecar.detected_pulses.front();

    EXPECT_EQ(accel_pulse.timing.channel_id, ref_pulse.timing.channel_id);
    EXPECT_EQ(accel_pulse.frequency.frequency_index, ref_pulse.frequency.frequency_index);
    EXPECT_EQ(accel_pulse.timing.global_start_sample, ref_pulse.timing.global_start_sample);
    EXPECT_EQ(accel_pulse.timing.duration_samples, ref_pulse.timing.duration_samples);
    EXPECT_DOUBLE_EQ(accel_pulse.power_db, ref_pulse.power_db);
    EXPECT_DOUBLE_EQ(accel_pulse.snr_db, ref_pulse.snr_db);
    EXPECT_DOUBLE_EQ(accel_pulse.noise_floor_db, ref_pulse.noise_floor_db);
    EXPECT_DOUBLE_EQ(accel_pulse.confidence, ref_pulse.confidence);

    ASSERT_FALSE(accel->sidecar.pulse_evidence.empty());
    EXPECT_EQ(accel->sidecar.pulse_evidence.front().sample_count,
              reference->sidecar.pulse_evidence.front().sample_count);
    EXPECT_EQ(accel->sidecar.pulse_evidence.front().sample_time_map.output_start_sample,
              reference->sidecar.pulse_evidence.front().sample_time_map.output_start_sample);

    EXPECT_EQ(accel_node.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(accel_node.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(accel_node.UsedFallback());
    EXPECT_TRUE(accel_node.FallbackDiagnostic().empty());
}

TEST(AccelGraphFhssDetectorTest, CpuTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = FhssDetectorCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(15));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto node = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->sidecar.channel.channel_id, 24u);
    EXPECT_EQ(output->sidecar.channel.frequency_index, 24u);
    EXPECT_FALSE(output->sidecar.detected_pulses.empty());
    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssDetectorTest, MetalStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssDetectorMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(15));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorMetalNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssDetectorTest, MetalAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssDetectorMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(15));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorSinkNode>(graph_manager);
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

TEST(AccelGraphFhssDetectorTest, CudaStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssDetectorCudaTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(15));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedCudaDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorCudaNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cuda);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cuda);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssDetectorTest, CudaAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssDetectorCudaAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(15));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssPerChannelPulseDetectorSinkNode>(graph_manager);
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

TEST(AccelGraphFhssDetectorTest, DescriptorFieldsDeclareFullConfigSurface) {
    const auto fields = AccelFhssPerChannelPulseDetectorNode::Fields();
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
    EXPECT_TRUE(has_field("detector_id"));
    EXPECT_TRUE(has_field("packet_sequence"));
    EXPECT_TRUE(has_field("min_power_linear"));
    EXPECT_TRUE(has_field("min_symbol_coherence"));
    EXPECT_TRUE(has_field("noise_floor_db"));
    EXPECT_TRUE(has_field("nominal_bandwidth_hz"));
    EXPECT_TRUE(has_field("max_pulse_input_samples"));
}
