// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "dsp/fhss/FHSSDownconverterNode.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"

namespace {

using accelgraph::fhss::AccelFhssDownconverterNode;
using accelgraph::fhss::AccelFhssDownconverterSinkNode;

std::filesystem::path FhssDownconverterCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_downconverter_cpu_topology.json");
}

std::filesystem::path FhssDownconverterMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_downconverter_metal_topology.json");
}

std::filesystem::path FhssDownconverterMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_downconverter_metal_allow_fallback_topology.json");
}

dsp::fhss::FHSSSyntheticIqToken MakeSyntheticIqInputToken() {
    auto samples = std::make_shared<const std::vector<std::complex<double>>>(
        std::vector<std::complex<double>>{{1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}, {0.0, -1.0}});

    dsp::fhss::FHSSGraphXSampleTimeMap map{};
    map.input_packet_global_start_sample = 1234;
    map.output_start_sample = 1234;
    map.input_sample_rate_hz = 500000000.0;
    map.output_sample_rate_hz = 500000000.0;

    dsp::fhss::FHSSSyntheticIqToken token{};
    token.token_id = 7;
    token.sidecar.iq = dsp::fhss::FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), map);
    return token;
}

dsp::fhss::FHSSDownconverterConfig MakeTranslatedDownconverterConfig() {
    dsp::fhss::FHSSDownconverterConfig cfg{};
    cfg.input_iq_center_frequency_hz = 1240000000.0;
    cfg.input_reference_frequency_hz = 1240000000.0;
    cfg.output_iq_center_frequency_hz = 1248000000.0;
    cfg.output_reference_frequency_hz = 1248000000.0;
    cfg.translation_frequency_hz = 8000000.0;
    cfg.passthrough = false;
    cfg.phase_convention = dsp::fhss::FHSSGraphXDownconverterPhaseConvention::OutputTimesExpNegativeJTwoPiTranslationT;
    cfg.sample_rate_hz = 500000000.0;
    return cfg;
}

}  // namespace

TEST(AccelGraphFhssDownconverterTest, CpuReferenceParityMatchesDspDownconverterTranslationPath) {
    auto input = MakeSyntheticIqInputToken();
    const auto cfg = MakeTranslatedDownconverterConfig();

    nlohmann::json json = {
        {"backend", "cpu"},
        {"strict_fallback", true},
        {"fallback_policy", "strict"},
        {"input_iq_center_frequency_hz", cfg.input_iq_center_frequency_hz},
        {"input_reference_frequency_hz", cfg.input_reference_frequency_hz},
        {"output_iq_center_frequency_hz", cfg.output_iq_center_frequency_hz},
        {"output_reference_frequency_hz", cfg.output_reference_frequency_hz},
        {"translation_frequency_hz", cfg.translation_frequency_hz},
        {"passthrough", cfg.passthrough},
        {"sample_rate_hz", cfg.sample_rate_hz},
        {"phase_convention", "output_times_exp_negative_j_two_pi_translation_t"}
    };

    AccelFhssDownconverterNode accel_node;
    const graph::JsonView view{json};
    accel_node.Configure(view);

    dsp::fhss::FHSSDownconverterNode ref_node;
    ref_node.SetConfig(cfg);

    const auto accel = accel_node.Transfer(input, std::integral_constant<std::size_t, 0>{},
                                           std::integral_constant<std::size_t, 0>{});
    const auto reference = ref_node.Transfer(input, std::integral_constant<std::size_t, 0>{},
                                             std::integral_constant<std::size_t, 0>{});

    ASSERT_TRUE(accel.has_value());
    ASSERT_TRUE(reference.has_value());

    ASSERT_TRUE(accel->sidecar.iq.host_complex64_samples != nullptr);
    ASSERT_TRUE(reference->sidecar.iq.host_complex64_samples != nullptr);

    const auto& accel_samples = *accel->sidecar.iq.host_complex64_samples;
    const auto& ref_samples = *reference->sidecar.iq.host_complex64_samples;
    ASSERT_EQ(accel_samples.size(), ref_samples.size());

    for (std::size_t i = 0; i < accel_samples.size(); ++i) {
        EXPECT_NEAR(accel_samples[i].real(), ref_samples[i].real(), 1.0e-12);
        EXPECT_NEAR(accel_samples[i].imag(), ref_samples[i].imag(), 1.0e-12);
    }

    EXPECT_DOUBLE_EQ(accel->sidecar.downconverter.input_iq_center_frequency_hz,
                     reference->sidecar.downconverter.input_iq_center_frequency_hz);
    EXPECT_DOUBLE_EQ(accel->sidecar.downconverter.output_iq_center_frequency_hz,
                     reference->sidecar.downconverter.output_iq_center_frequency_hz);
    EXPECT_DOUBLE_EQ(accel->sidecar.downconverter.translation_frequency_hz,
                     reference->sidecar.downconverter.translation_frequency_hz);
    EXPECT_EQ(accel->sidecar.downconverter.phase_convention,
              reference->sidecar.downconverter.phase_convention);

    EXPECT_EQ(accel_node.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(accel_node.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(accel_node.UsedFallback());
    EXPECT_TRUE(accel_node.FallbackDiagnostic().empty());
}

TEST(AccelGraphFhssDownconverterTest, CpuTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = FhssDownconverterCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto node = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssDownconverterSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->sidecar.downconverter.phase_convention,
              dsp::fhss::FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation);
    EXPECT_TRUE(output->sidecar.downconverter.passthrough);
    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssDownconverterTest, MetalStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssDownconverterMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssDownconverterMetalNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssDownconverterTest, MetalAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssDownconverterMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssDownconverterNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssDownconverterSinkNode>(graph_manager);
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

TEST(AccelGraphFhssDownconverterTest, DescriptorFieldsDeclareFullConfigSurface) {
    const auto fields = AccelFhssDownconverterNode::Fields();
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
    EXPECT_TRUE(has_field("input_iq_center_frequency_hz"));
    EXPECT_TRUE(has_field("input_reference_frequency_hz"));
    EXPECT_TRUE(has_field("output_iq_center_frequency_hz"));
    EXPECT_TRUE(has_field("output_reference_frequency_hz"));
    EXPECT_TRUE(has_field("translation_frequency_hz"));
    EXPECT_TRUE(has_field("passthrough"));
    EXPECT_TRUE(has_field("phase_convention"));
    EXPECT_TRUE(has_field("sample_rate_hz"));
}
