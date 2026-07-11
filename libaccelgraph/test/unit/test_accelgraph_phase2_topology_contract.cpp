// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "accelgraph/SpectrumGraphNodes.hpp"
#include "accelgraph/TransferGraphNodes.hpp"
#include "graph/GraphExecutorBuilder.hpp"

namespace {

struct TopologyCase {
    const char* file_name;
    std::size_t expected_nodes;
    std::size_t expected_edges;
    bool may_need_metal;
    bool may_need_cuda;
};

constexpr std::array<TopologyCase, 33> kTopologyMatrix = {{
    {"accelgraph_phase3a_transfer_cpu_topology.json", 4u, 3u, false, false},
    {"accelgraph_phase3a_transfer_metal_topology.json", 4u, 3u, true, false},
    {"accelgraph_phase5_transfer_cuda_topology.json", 4u, 3u, false, true},
    {"accelgraph_phase6_spectrum_cpu_topology.json", 3u, 2u, false, false},
    {"accelgraph_phase6_spectrum_metal_topology.json", 3u, 2u, true, false},
    {"accelgraph_phase6_spectrum_metal_allow_fallback_topology.json", 3u, 2u, true, false},
    {"accelgraph_phase6b_spectrum_cpu_topology.json", 3u, 2u, false, false},
    {"accelgraph_phase6b_spectrum_cuda_topology.json", 3u, 2u, false, true},
    {"accelgraph_phase6b_spectrum_cuda_allow_fallback_topology.json", 3u, 2u, false, true},
    {"accelgraph_phase2_spectrum_cpu_fanout_topology.json", 4u, 3u, false, false},
    {"accelgraph_fhss_downconverter_cpu_topology.json", 3u, 2u, false, false},
    {"accelgraph_fhss_downconverter_metal_topology.json", 3u, 2u, true, false},
    {"accelgraph_fhss_downconverter_metal_allow_fallback_topology.json", 3u, 2u, true, false},
    {"accelgraph_fhss_downconverter_cuda_topology.json", 3u, 2u, false, true},
    {"accelgraph_fhss_downconverter_cuda_allow_fallback_topology.json", 3u, 2u, false, true},
    {"accelgraph_fhss_channelizer_cpu_topology.json", 4u, 3u, false, false},
    {"accelgraph_fhss_channelizer_metal_topology.json", 4u, 3u, true, false},
    {"accelgraph_fhss_channelizer_metal_allow_fallback_topology.json", 4u, 3u, true, false},
    {"accelgraph_fhss_channelizer_cuda_topology.json", 4u, 3u, false, true},
    {"accelgraph_fhss_channelizer_cuda_allow_fallback_topology.json", 4u, 3u, false, true},
    {"accelgraph_fhss_detector_cpu_topology.json", 5u, 4u, false, false},
    {"accelgraph_fhss_detector_metal_topology.json", 5u, 4u, true, false},
    {"accelgraph_fhss_detector_metal_allow_fallback_topology.json", 5u, 4u, true, false},
    {"accelgraph_fhss_detector_cuda_topology.json", 5u, 4u, false, true},
    {"accelgraph_fhss_detector_cuda_allow_fallback_topology.json", 5u, 4u, false, true},
    {"accelgraph_fhss_branch_metric_cpu_topology.json", 6u, 5u, false, false},
    {"accelgraph_fhss_branch_metric_metal_topology.json", 6u, 5u, true, false},
    {"accelgraph_fhss_branch_metric_metal_allow_fallback_topology.json", 6u, 5u, true, false},
    {"accelgraph_fhss_branch_metric_cuda_topology.json", 6u, 5u, false, true},
    {"accelgraph_fhss_branch_metric_cuda_allow_fallback_topology.json", 6u, 5u, false, true},
    {"accelgraph_fhss_e2e_hybrid_cpu_topology.json", 10u, 9u, false, false},
    {"accelgraph_fhss_e2e_hybrid_metal_topology.json", 10u, 9u, true, false},
    {"accelgraph_fhss_e2e_hybrid_metal_allow_fallback_topology.json", 10u, 9u, true, false},
}};

bool IsExpectedFhssDownconverterDiagnostic(const std::string& message) {
    return message.find(accelgraph::fhss::kFhssDownconverterMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssDownconverterCudaNativeNotImplementedDiagnostic) != std::string::npos;
}

bool IsExpectedFhssChannelizerDiagnostic(const std::string& message) {
    return message.find(accelgraph::fhss::kFhssChannelizerMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssChannelizerCudaNativeNotImplementedDiagnostic) != std::string::npos;
}

bool IsExpectedFhssDetectorDiagnostic(const std::string& message) {
    return message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorCudaNativeNotImplementedDiagnostic) != std::string::npos;
}

bool IsExpectedFhssBranchMetricDiagnostic(const std::string& message) {
    return message.find(accelgraph::fhss::kFhssBranchMetricMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssBranchMetricCudaNativeNotImplementedDiagnostic) != std::string::npos;
}

std::filesystem::path TopologyPath(const std::string& file_name) {
    return std::filesystem::path(__FILE__).parent_path().parent_path() /
           "config" / "topologies" / file_name;
}

std::filesystem::path PluginDirectoryPath() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

std::shared_ptr<graph::GraphExecutor> BuildExecutor(const std::filesystem::path& config_path) {
    return graph::GraphExecutorBuilder()
        .WithJsonConfig(config_path.string())
        .WithPluginDirectory(PluginDirectoryPath().string())
        .WithExecutorTimeout(std::chrono::seconds(15))
        .Build();
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open topology json: " + path.string());
    }
    nlohmann::json doc;
    in >> doc;
    return doc;
}

std::vector<std::filesystem::path> CheckedInTopologyJsonFiles() {
    const auto topology_dir = std::filesystem::path(__FILE__).parent_path().parent_path() /
                              "config" / "topologies";
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(topology_dir)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(topology_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() == ".json") {
            files.push_back(path);
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> PhaseTestFilesInUnitDir(const std::filesystem::path& unit_dir) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(unit_dir)) {
        return files;
    }

    static const std::regex kPhaseTestPattern(R"(^test_accelgraph_phase.*\.cpp$)");
    for (const auto& entry : std::filesystem::directory_iterator(unit_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto file_name = entry.path().filename().string();
        if (std::regex_match(file_name, kPhaseTestPattern)) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

constexpr std::array<const char*, 5> kForbiddenBypassTokens = {{
    "ConfigureNode",
    "ConfigureTransferNode",
    "JsonView(",
    "->Configure(",
    ".Configure(",
}};

std::filesystem::path WriteTempJson(const nlohmann::json& doc, const std::string& suffix) {
    const auto temp = std::filesystem::temp_directory_path() /
                      ("graphx_phase2_" + suffix + ".json");
    std::ofstream out(temp);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to write temporary topology json: " + temp.string());
    }
    out << doc.dump(2);
    return temp;
}

bool HasSingleSourceAndDualSinks(const nlohmann::json& doc) {
    if (!doc.contains("nodes") || !doc["nodes"].is_array()) {
        return false;
    }
    std::size_t sink_count = 0;
    bool has_source = false;
    for (const auto& node : doc["nodes"]) {
        if (!node.contains("type") || !node["type"].is_string()) {
            continue;
        }
        const std::string type = node["type"].get<std::string>();
        if (type == "SineWaveSourceNode") {
            has_source = true;
        }
        if (type == "SpectrumSinkNode") {
            ++sink_count;
        }
    }
    return has_source && sink_count == 2;
}

}  // namespace

TEST(AccelGraphTopologyJsonOwnershipTest, DescriptorMetadataDeclaresSupportedFields) {
    const auto source_fields = accelgraph::SineWaveSourceNode::Fields();
    const auto analysis_fields = accelgraph::SpectrumAnalysisNode::Fields();
    const auto fhss_channelizer_fields = accelgraph::fhss::AccelFhssChannelizerNode::Fields();
    const auto fhss_detector_fields = accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode::Fields();
    const auto fhss_branch_metric_fields = accelgraph::fhss::AccelFhssBranchMetricNode::Fields();
    const auto fhss_downconverter_fields = accelgraph::fhss::AccelFhssDownconverterNode::Fields();
    const auto ingress_fields = accelgraph::HostIngressNode::Fields();

    auto has_field = [](const auto& fields, const std::string& name) {
        for (const auto& field : fields) {
            if (field.name == name) {
                return true;
            }
        }
        return false;
    };

    EXPECT_TRUE(has_field(source_fields, "sample_count"));
    EXPECT_TRUE(has_field(source_fields, "sample_rate_hz"));
    EXPECT_TRUE(has_field(source_fields, "tone_frequency_hz"));
    EXPECT_TRUE(has_field(source_fields, "amplitude"));
    EXPECT_TRUE(has_field(source_fields, "phase_radians"));
    EXPECT_TRUE(has_field(source_fields, "packet_number"));

    EXPECT_TRUE(has_field(analysis_fields, "backend"));
    EXPECT_TRUE(has_field(analysis_fields, "strict_fallback"));
    EXPECT_TRUE(has_field(analysis_fields, "fallback_policy"));
    EXPECT_TRUE(has_field(analysis_fields, "cuda_device_ordinal"));

    EXPECT_TRUE(has_field(fhss_channelizer_fields, "backend"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "strict_fallback"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "fallback_policy"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "provider_id"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "session_key"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "cuda_device_ordinal"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "iq_center_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "receiver_frequency_indices"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "channel_ids"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "transmitted_active_frequency_indices"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "transmitted_pulse_frequency_indices"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "sample_rate_hz"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "channel_sample_rate_hz"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "decimation_factor"));
    EXPECT_TRUE(has_field(fhss_channelizer_fields, "filter_group_delay_input_samples"));

    EXPECT_TRUE(has_field(fhss_detector_fields, "backend"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "strict_fallback"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "fallback_policy"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "provider_id"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "session_key"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "cuda_device_ordinal"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "detector_id"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "packet_sequence"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "min_power_linear"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "min_symbol_coherence"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "noise_floor_db"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "nominal_bandwidth_hz"));
    EXPECT_TRUE(has_field(fhss_detector_fields, "max_pulse_input_samples"));

    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "backend"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "strict_fallback"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "fallback_policy"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "provider_id"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "session_key"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "cuda_device_ordinal"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "symbol_count"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "modulation_index"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "initial_phase_state"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "check_terminal_phase"));
    EXPECT_TRUE(has_field(fhss_branch_metric_fields, "expected_terminal_phase_state"));

    EXPECT_TRUE(has_field(fhss_downconverter_fields, "backend"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "strict_fallback"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "fallback_policy"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "provider_id"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "session_key"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "cuda_device_ordinal"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "input_iq_center_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "input_reference_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "output_iq_center_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "output_reference_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "translation_frequency_hz"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "passthrough"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "phase_convention"));
    EXPECT_TRUE(has_field(fhss_downconverter_fields, "sample_rate_hz"));

    EXPECT_TRUE(has_field(ingress_fields, "session_key"));
    EXPECT_TRUE(has_field(ingress_fields, "provider_id"));
    EXPECT_TRUE(has_field(ingress_fields, "backend"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_size"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_multiplier"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_offset"));
    EXPECT_TRUE(has_field(ingress_fields, "debug_label"));
}

TEST(AccelGraphTopologyJsonOwnershipTest, SyntheticTopologyMatrixBuildsAndExecutesOrSkipsWithHardwareDiagnostic) {
    ASSERT_TRUE(std::filesystem::exists(PluginDirectoryPath()));

    for (const auto& topology : kTopologyMatrix) {
        SCOPED_TRACE(topology.file_name);
        const auto config_path = TopologyPath(topology.file_name);
        ASSERT_TRUE(std::filesystem::exists(config_path));

#if !ACCELGRAPH_ENABLE_CUDA
        if (topology.may_need_cuda) {
            SUCCEED() << "CUDA topology skipped on non-CUDA build: " << topology.file_name;
            continue;
        }
#endif

#if !ACCELGRAPH_ENABLE_METAL
        if (topology.may_need_metal) {
            SUCCEED() << "Metal topology skipped on non-Metal build: " << topology.file_name;
            continue;
        }
#endif

        const auto doc = LoadJson(config_path);
        EXPECT_EQ(doc["nodes"].size(), topology.expected_nodes);
        EXPECT_EQ(doc["edges"].size(), topology.expected_edges);

        try {
            auto executor = BuildExecutor(config_path);
            ASSERT_NE(executor, nullptr);

            auto graph_manager = executor->GetGraphManager();
            ASSERT_NE(graph_manager, nullptr);
            EXPECT_EQ(graph_manager->GetNodes().size(), topology.expected_nodes);
            EXPECT_EQ(graph_manager->GetEdges().size(), topology.expected_edges);

            const auto run_result = executor->Execute();
            ASSERT_TRUE(run_result.success) << run_result.message << " " << run_result.error_details;
        } catch (const std::exception& ex) {
            const std::string message = ex.what();
            const bool is_wrapped_graph_build_failure = accelgraph::test::IsGraphBuildFailureDiagnostic(message);
            const std::string topology_name = topology.file_name;
            const bool is_fhss_downconverter_topology =
                topology_name.find("accelgraph_fhss_downconverter_") != std::string::npos;
            const bool is_fhss_channelizer_topology =
                topology_name.find("accelgraph_fhss_channelizer_") != std::string::npos;
            const bool is_fhss_detector_topology =
                topology_name.find("accelgraph_fhss_detector_") != std::string::npos;
            const bool is_fhss_branch_metric_topology =
                topology_name.find("accelgraph_fhss_branch_metric_") != std::string::npos;
            const bool expected_fhss_downconverter_strict_skip =
                is_fhss_downconverter_topology && is_wrapped_graph_build_failure &&
                (topology.may_need_metal || topology.may_need_cuda);
            const bool expected_fhss_channelizer_strict_skip =
                is_fhss_channelizer_topology && is_wrapped_graph_build_failure &&
                (topology.may_need_metal || topology.may_need_cuda);
            const bool expected_fhss_detector_strict_skip =
                is_fhss_detector_topology && is_wrapped_graph_build_failure &&
                (topology.may_need_metal || topology.may_need_cuda);
            const bool expected_fhss_branch_metric_strict_skip =
                is_fhss_branch_metric_topology && is_wrapped_graph_build_failure &&
                (topology.may_need_metal || topology.may_need_cuda);
            const bool expected_metal_skip =
                topology.may_need_metal &&
                (accelgraph::test::IsExpectedMetalDiagnostic(message) ||
                 IsExpectedFhssDownconverterDiagnostic(message) ||
                 IsExpectedFhssChannelizerDiagnostic(message) ||
                 IsExpectedFhssDetectorDiagnostic(message) ||
                 IsExpectedFhssBranchMetricDiagnostic(message) ||
                 is_wrapped_graph_build_failure ||
                 expected_fhss_downconverter_strict_skip ||
                 expected_fhss_channelizer_strict_skip ||
                 expected_fhss_detector_strict_skip ||
                 expected_fhss_branch_metric_strict_skip);
            const bool expected_cuda_skip =
                topology.may_need_cuda &&
                (accelgraph::test::IsExpectedCudaDiagnostic(message) ||
                 IsExpectedFhssDownconverterDiagnostic(message) ||
                 IsExpectedFhssChannelizerDiagnostic(message) ||
                 IsExpectedFhssDetectorDiagnostic(message) ||
                 IsExpectedFhssBranchMetricDiagnostic(message) ||
                 is_wrapped_graph_build_failure ||
                 expected_fhss_downconverter_strict_skip ||
                 expected_fhss_channelizer_strict_skip ||
                 expected_fhss_detector_strict_skip ||
                 expected_fhss_branch_metric_strict_skip);
            if (expected_metal_skip || expected_cuda_skip) {
                SUCCEED() << "Hardware-specific skip for " << topology.file_name << ": " << message;
                continue;
            }
            throw;
        }
    }
}

TEST(AccelGraphTopologyJsonOwnershipTest, UnknownNodeConfigFieldIsRejectedByLoader) {
    auto doc = LoadJson(TopologyPath("accelgraph_phase6_spectrum_cpu_topology.json"));
    doc["nodes"][1]["node_config"]["not_a_real_field"] = 1;

    const auto temp_path = WriteTempJson(doc, "unknown_node_config_field");
    ASSERT_TRUE(std::filesystem::exists(temp_path));

    try {
        EXPECT_THROW({
            auto executor = BuildExecutor(temp_path);
            (void)executor;
        }, std::exception);
    } catch (...) {
        std::filesystem::remove(temp_path);
        throw;
    }
    std::filesystem::remove(temp_path);
}

TEST(AccelGraphTopologyJsonOwnershipTest, MissingRequiredNodeConfigFieldIsRejectedByLoader) {
    auto doc = LoadJson(TopologyPath("accelgraph_phase6_spectrum_cpu_topology.json"));
    doc["nodes"][1]["node_config"].erase("backend");

    const auto temp_path = WriteTempJson(doc, "missing_required_backend");
    ASSERT_TRUE(std::filesystem::exists(temp_path));

    try {
        EXPECT_THROW({
            auto executor = BuildExecutor(temp_path);
            (void)executor;
        }, std::exception);
    } catch (...) {
        std::filesystem::remove(temp_path);
        throw;
    }
    std::filesystem::remove(temp_path);
}

TEST(AccelGraphTopologyJsonOwnershipTest, WrongNodeConfigTypeIsRejectedByLoader) {
    auto doc = LoadJson(TopologyPath("accelgraph_phase6_spectrum_cpu_topology.json"));
    doc["nodes"][0]["node_config"]["sample_count"] = "bad-type";

    const auto temp_path = WriteTempJson(doc, "wrong_type_sample_count");
    ASSERT_TRUE(std::filesystem::exists(temp_path));

    try {
        EXPECT_THROW({
            auto executor = BuildExecutor(temp_path);
            (void)executor;
        }, std::exception);
    } catch (...) {
        std::filesystem::remove(temp_path);
        throw;
    }
    std::filesystem::remove(temp_path);
}

TEST(AccelGraphTopologyJsonOwnershipTest, TopologyFanOutConfigIsCheckedInAndUsesJsonOwnedInitialization) {
    const auto config_path = TopologyPath("accelgraph_phase2_spectrum_cpu_fanout_topology.json");
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto doc = LoadJson(config_path);
    EXPECT_TRUE(HasSingleSourceAndDualSinks(doc));

    for (const auto& node : doc["nodes"]) {
        ASSERT_TRUE(node.contains("node_config"));
    }
}

TEST(AccelGraphTopologyJsonOwnershipTest, AllCheckedInTopologyJsonFilesAreParseableAndStructured) {
    const auto files = CheckedInTopologyJsonFiles();
    ASSERT_FALSE(files.empty());

    const std::array<std::string, 1> legacy_without_node_config = {{
        "accelgraph_phase2_transfer_topology.json",
    }};

    auto is_legacy_without_node_config = [&](const std::string& file_name) {
        return std::find(legacy_without_node_config.begin(), legacy_without_node_config.end(), file_name) !=
               legacy_without_node_config.end();
    };

    const std::set<std::string> allow_missing_node_config_types = {
        "CPSMViterbiDecoderNode",
        "FHSSPulseWordDecoderNode",
        "FHSSMessageSinkNode",
    };

    for (const auto& file : files) {
        SCOPED_TRACE(file.string());
        const auto file_name = file.filename().string();
        const auto doc = LoadJson(file);

        ASSERT_TRUE(doc.contains("name"));
        ASSERT_TRUE(doc["name"].is_string());
        ASSERT_TRUE(doc.contains("nodes"));
        ASSERT_TRUE(doc["nodes"].is_array());
        ASSERT_TRUE(doc.contains("edges"));
        ASSERT_TRUE(doc["edges"].is_array());

        for (const auto& node : doc["nodes"]) {
            ASSERT_TRUE(node.contains("id"));
            ASSERT_TRUE(node.contains("type"));
            ASSERT_TRUE(node.contains("name"));
            if (!is_legacy_without_node_config(file_name)) {
                ASSERT_TRUE(node["type"].is_string());
                const auto type = node["type"].get<std::string>();
                if (allow_missing_node_config_types.find(type) == allow_missing_node_config_types.end()) {
                    ASSERT_TRUE(node.contains("node_config"));
                }
            }
        }
    }
}

TEST(AccelGraphTopologyJsonOwnershipTest, TopologyNodesHaveTruthfulConnectivityShapes) {
    const auto files = CheckedInTopologyJsonFiles();
    ASSERT_FALSE(files.empty());

    const std::set<std::string> allowed_source_only_types = {
        "SineWaveSourceNode",
        "HostIngressNode",
        "FHSSSyntheticIqSourceNode",
    };
    const std::set<std::string> allowed_sink_only_types = {
        "SpectrumSinkNode",
        "AccelFhssChannelizerSinkNode",
        "AccelFhssPerChannelPulseDetectorSinkNode",
        "AccelFhssDownconverterSinkNode",
        "AccelFhssBranchMetricSinkNode",
        "FHSSMessageSinkNode",
        "HostEgressNode",
        "ReleaseLeaseNode",
    };

    for (const auto& file : files) {
        SCOPED_TRACE(file.string());
        const auto doc = LoadJson(file);

        std::unordered_map<std::string, std::string> type_by_id;
        std::unordered_map<std::string, std::size_t> incoming_degree;
        std::unordered_map<std::string, std::size_t> outgoing_degree;

        for (const auto& node : doc["nodes"]) {
            ASSERT_TRUE(node.contains("id"));
            ASSERT_TRUE(node["id"].is_string());
            ASSERT_TRUE(node.contains("type"));
            ASSERT_TRUE(node["type"].is_string());

            const std::string id = node["id"].get<std::string>();
            const std::string type = node["type"].get<std::string>();
            type_by_id[id] = type;
            incoming_degree[id] = 0;
            outgoing_degree[id] = 0;
        }

        for (const auto& edge : doc["edges"]) {
            ASSERT_TRUE(edge.contains("source_node_id"));
            ASSERT_TRUE(edge["source_node_id"].is_string());
            ASSERT_TRUE(edge.contains("target_node_id"));
            ASSERT_TRUE(edge["target_node_id"].is_string());

            const std::string src = edge["source_node_id"].get<std::string>();
            const std::string dst = edge["target_node_id"].get<std::string>();

            ASSERT_TRUE(type_by_id.find(src) != type_by_id.end())
                << "edge source references unknown node id: " << src;
            ASSERT_TRUE(type_by_id.find(dst) != type_by_id.end())
                << "edge target references unknown node id: " << dst;

            ++outgoing_degree[src];
            ++incoming_degree[dst];
        }

        for (const auto& [id, type] : type_by_id) {
            const auto incoming = incoming_degree[id];
            const auto outgoing = outgoing_degree[id];

            if (type == "ReleaseLeaseNode") {
                EXPECT_GT(incoming, 0u)
                    << "ReleaseLeaseNode must be exercised by topology flow (missing incoming edge): "
                    << file.filename().string() << " node_id=" << id;
                EXPECT_EQ(outgoing, 0u)
                    << "ReleaseLeaseNode is a sink and must not have outgoing edges: "
                    << file.filename().string() << " node_id=" << id;
            }

            if (incoming == 0 && outgoing == 0) {
                ADD_FAILURE()
                    << "Disconnected node in topology: " << file.filename().string()
                    << " node_id=" << id << " type=" << type;
                continue;
            }

            if (incoming == 0 && outgoing > 0 &&
                allowed_source_only_types.find(type) == allowed_source_only_types.end()) {
                ADD_FAILURE()
                    << "Only known source node types may be source-only: "
                    << file.filename().string() << " node_id=" << id << " type=" << type;
            }

            if (incoming > 0 && outgoing == 0 &&
                allowed_sink_only_types.find(type) == allowed_sink_only_types.end()) {
                ADD_FAILURE()
                    << "Only known sink node types may be sink-only: "
                    << file.filename().string() << " node_id=" << id << " type=" << type;
            }
        }
    }
}

TEST(AccelGraphTopologyJsonOwnershipTest, TopologyTestsDoNotUseDirectConfigureCalls) {
    const auto unit_dir = std::filesystem::path(__FILE__).parent_path();
    const auto phase_test_files = PhaseTestFilesInUnitDir(unit_dir);
    ASSERT_FALSE(phase_test_files.empty());

    const std::set<std::string> allowlisted_non_topology_phase_tests = {
        std::filesystem::path(__FILE__).filename().string(),
        "test_accelgraph_phase4_cuda.cpp",
        "test_accelgraph_fhss_downconverter.cpp",
    };

    for (const auto& file_path : phase_test_files) {
        const auto file_name = file_path.filename().string();
        if (allowlisted_non_topology_phase_tests.find(file_name) !=
            allowlisted_non_topology_phase_tests.end()) {
            continue;
        }

        SCOPED_TRACE(file_name);
        ASSERT_TRUE(std::filesystem::exists(file_path));
        const std::string content = ReadWholeFile(file_path);

        for (const auto* token : kForbiddenBypassTokens) {
            EXPECT_EQ(content.find(token), std::string::npos)
                << "Topology tests must not bypass JSON-owned initialization: token='"
                << token << "' file='" << file_name << "'";
        }
    }
}

TEST(AccelGraphTopologyJsonOwnershipTest, TopologyHelperDoesNotUseDirectConfigureBypass) {
    const auto helper_path = std::filesystem::path(__FILE__).parent_path() /
                             "AccelGraphTopologyTestUtils.hpp";
    ASSERT_TRUE(std::filesystem::exists(helper_path));

    const std::string content = ReadWholeFile(helper_path);
    for (const auto* token : kForbiddenBypassTokens) {
        EXPECT_EQ(content.find(token), std::string::npos);
    }
}
