// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "dsp/fhss/FHSSDownconverterNode.hpp"
#include "dsp/fhss/FHSSFixtureFrequencyChannelizerNode.hpp"
#include "dsp/fhss/FHSSPacketConversions.hpp"

namespace {

using accelgraph::fhss::AccelFhssChannelizerNode;
using accelgraph::fhss::AccelFhssChannelizerSinkNode;

std::filesystem::path FhssChannelizerCpuTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_channelizer_cpu_topology.json");
}

std::filesystem::path FhssChannelizerMetalTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_channelizer_metal_topology.json");
}

std::filesystem::path FhssChannelizerMetalAllowFallbackTopologyConfigPath() {
    return accelgraph::test::TopologyConfigPath(
        __FILE__,
        "accelgraph_fhss_channelizer_metal_allow_fallback_topology.json");
}

nlohmann::json MakeChannelizerJsonConfig(const std::string& backend,
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
        {"iq_center_frequency_hz", 1240000000.0},
        {"transmitted_active_frequency_indices", {24u, 28u, 32u, 36u}},
        {"transmitted_pulse_frequency_indices", {24u, 28u, 32u, 36u}},
        {"sample_rate_hz", 500000000.0},
        {"channel_sample_rate_hz", 250000000.0},
        {"decimation_factor", 2u},
        {"filter_group_delay_input_samples", 0u},
        {"iq_capture", {
            {"enabled", false},
            {"output_directory", ""},
            {"frequency_indices", nlohmann::json::array()},
            {"overwrite", true}
        }}
    };
}

dsp::fhss::FHSSDownconvertedIqToken MakeDownconvertedInputToken() {
    auto samples = std::make_shared<const std::vector<std::complex<double>>>(
        std::vector<std::complex<double>>(64, {1.0, 0.0}));

    dsp::fhss::FHSSGraphXSampleTimeMap map{};
    map.input_packet_global_start_sample = 1000;
    map.output_start_sample = 1000;
    map.input_sample_rate_hz = 500000000.0;
    map.output_sample_rate_hz = 500000000.0;

    dsp::fhss::FHSSDownconvertedIqToken token{};
    token.token_id = 9;
    token.sidecar.iq = dsp::fhss::FHSSGraphXComplexEvidenceFromHostSamples(samples, samples->size(), map);
    token.sidecar.downconverter.passthrough = true;
    token.sidecar.downconverter.translation_frequency_hz = 0.0;
    token.sidecar.downconverter.sample_rate_hz = 500000000.0;
    token.sidecar.downconverter.sample_time_map = map;
    return token;
}

}  // namespace

TEST(AccelGraphFhssChannelizerTest, CpuReferenceParityMatchesDspFixtureChannelizer) {
    const auto input = MakeDownconvertedInputToken();

    AccelFhssChannelizerNode accel_node;
    const auto accel_json = MakeChannelizerJsonConfig("cpu", true, "strict", "cpu.default");
    const graph::JsonView accel_view{accel_json};
    accel_node.Configure(accel_view);

    dsp::fhss::FHSSFixtureFrequencyChannelizerNode ref_node;
    const auto ref_json = MakeChannelizerJsonConfig("cpu", true, "strict", "cpu.default");
    const graph::JsonView ref_view{ref_json};
    ref_node.Configure(ref_view);

    ASSERT_TRUE(accel_node.ConsumeInput<0>(input));
    ASSERT_TRUE(ref_node.ConsumeInput<0>(input));

    const auto accel_ch24 = accel_node.ProduceOutput<24>();
    const auto ref_ch24 = ref_node.ProduceOutput<24>();
    ASSERT_TRUE(accel_ch24.has_value());
    ASSERT_TRUE(ref_ch24.has_value());

    EXPECT_EQ(accel_ch24->sidecar.channel.channel_id, ref_ch24->sidecar.channel.channel_id);
    EXPECT_EQ(accel_ch24->sidecar.channel.frequency_index, ref_ch24->sidecar.channel.frequency_index);
    EXPECT_EQ(accel_ch24->sidecar.channel.decimation_factor, ref_ch24->sidecar.channel.decimation_factor);
    EXPECT_DOUBLE_EQ(accel_ch24->sidecar.channel.channel_sample_rate_hz,
                     ref_ch24->sidecar.channel.channel_sample_rate_hz);
    EXPECT_EQ(accel_ch24->sidecar.iq.sample_count, ref_ch24->sidecar.iq.sample_count);

    ASSERT_TRUE(accel_ch24->sidecar.iq.host_complex64_samples != nullptr);
    ASSERT_TRUE(ref_ch24->sidecar.iq.host_complex64_samples != nullptr);
    ASSERT_EQ(accel_ch24->sidecar.iq.host_complex64_samples->size(),
              ref_ch24->sidecar.iq.host_complex64_samples->size());

    EXPECT_EQ(accel_node.RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(accel_node.SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(accel_node.UsedFallback());
    EXPECT_TRUE(accel_node.FallbackDiagnostic().empty());
}

TEST(AccelGraphFhssChannelizerTest, CpuTopologyExecutesViaGraphExecutorAndPlugins) {
    const auto config_path = FhssChannelizerCpuTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));
    ASSERT_TRUE(std::filesystem::exists(accelgraph::test::PluginDirectoryPath()));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);

    auto node = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssChannelizerSinkNode>(graph_manager);
    ASSERT_NE(node, nullptr);
    ASSERT_NE(sink, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    auto output = sink->LastPacket();
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->sidecar.channel.channel_id, 24u);
    EXPECT_EQ(output->sidecar.channel.frequency_index, 24u);
    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Cpu);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssChannelizerTest, MetalStrictExecutionOrSkipWithExactDiagnostic) {
    const auto config_path = FhssChannelizerMetalTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    std::shared_ptr<graph::GraphExecutor> executor;
    try {
        executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
            accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
            message.find(accelgraph::fhss::kFhssChannelizerMetalNativeNotImplementedDiagnostic) != std::string::npos) {
            GTEST_SKIP() << message;
        }
        throw;
    }

    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    ASSERT_NE(node, nullptr);

    const auto run_result = executor->Execute();
    ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;

    EXPECT_EQ(node->RequestedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_EQ(node->SelectedBackend(), accelgraph::AcceleratorBackend::Metal);
    EXPECT_FALSE(node->UsedFallback());
}

TEST(AccelGraphFhssChannelizerTest, MetalAllowFallbackUsesCpuWhenNativePathUnavailable) {
    const auto config_path = FhssChannelizerMetalAllowFallbackTopologyConfigPath();
    ASSERT_TRUE(std::filesystem::exists(config_path));

    auto executor = accelgraph::test::BuildExecutor(config_path, std::chrono::seconds(10));
    ASSERT_NE(executor, nullptr);

    auto graph_manager = executor->GetGraphManager();
    ASSERT_NE(graph_manager, nullptr);
    auto node = accelgraph::test::ResolveNode<AccelFhssChannelizerNode>(graph_manager);
    auto sink = accelgraph::test::ResolveNode<AccelFhssChannelizerSinkNode>(graph_manager);
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

TEST(AccelGraphFhssChannelizerTest, DescriptorFieldsDeclareFullConfigSurface) {
    const auto fields = AccelFhssChannelizerNode::Fields();
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
    EXPECT_TRUE(has_field("iq_center_frequency_hz"));
    EXPECT_TRUE(has_field("receiver_frequency_indices"));
    EXPECT_TRUE(has_field("channel_ids"));
    EXPECT_TRUE(has_field("transmitted_active_frequency_indices"));
    EXPECT_TRUE(has_field("transmitted_pulse_frequency_indices"));
    EXPECT_TRUE(has_field("sample_rate_hz"));
    EXPECT_TRUE(has_field("channel_sample_rate_hz"));
    EXPECT_TRUE(has_field("decimation_factor"));
    EXPECT_TRUE(has_field("filter_group_delay_input_samples"));
}
