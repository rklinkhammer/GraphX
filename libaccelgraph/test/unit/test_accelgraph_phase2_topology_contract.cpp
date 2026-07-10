// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
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

constexpr std::array<TopologyCase, 9> kTopologyMatrix = {{
    {"accelgraph_phase3a_transfer_cpu_topology.json", 5u, 3u, false, false},
    {"accelgraph_phase3a_transfer_metal_topology.json", 5u, 3u, true, false},
    {"accelgraph_phase5_transfer_cuda_topology.json", 5u, 3u, false, true},
    {"accelgraph_phase6_spectrum_cpu_topology.json", 3u, 2u, false, false},
    {"accelgraph_phase6_spectrum_metal_topology.json", 3u, 2u, true, false},
    {"accelgraph_phase6_spectrum_metal_allow_fallback_topology.json", 3u, 2u, true, false},
    {"accelgraph_phase6b_spectrum_cuda_topology.json", 3u, 2u, false, true},
    {"accelgraph_phase6b_spectrum_cuda_allow_fallback_topology.json", 3u, 2u, false, true},
    {"accelgraph_phase2_spectrum_cpu_fanout_topology.json", 4u, 3u, false, false},
}};

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

bool IsExpectedMetalDiagnostic(const std::string& message) {
    return message.find(accelgraph::kMetalSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalRuntimeUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalNoCompatibleDeviceDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalSessionCreationFailureDiagnostic) != std::string::npos;
}

bool IsExpectedCudaDiagnostic(const std::string& message) {
    return message.find(accelgraph::kCudaSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaToolkitUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaRuntimeHeadersUnavailableDiagnostic) != std::string::npos ||
           message.find("driver") != std::string::npos ||
           message.find("device") != std::string::npos ||
           message.find("CUDA") != std::string::npos;
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

TEST(AccelGraphPhase2TopologyContractTest, DescriptorMetadataDeclaresSupportedFields) {
    const auto source_fields = accelgraph::SineWaveSourceNode::Fields();
    const auto analysis_fields = accelgraph::SpectrumAnalysisNode::Fields();
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

    EXPECT_TRUE(has_field(ingress_fields, "session_key"));
    EXPECT_TRUE(has_field(ingress_fields, "provider_id"));
    EXPECT_TRUE(has_field(ingress_fields, "backend"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_size"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_multiplier"));
    EXPECT_TRUE(has_field(ingress_fields, "payload_offset"));
    EXPECT_TRUE(has_field(ingress_fields, "debug_label"));
}

TEST(AccelGraphPhase2TopologyContractTest, SyntheticTopologyMatrixBuildsAndExecutesOrSkipsWithHardwareDiagnostic) {
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
            const bool expected_metal_skip = topology.may_need_metal && IsExpectedMetalDiagnostic(message);
            const bool expected_cuda_skip = topology.may_need_cuda && IsExpectedCudaDiagnostic(message);
            if (expected_metal_skip || expected_cuda_skip) {
                SUCCEED() << "Hardware-specific skip for " << topology.file_name << ": " << message;
                continue;
            }
            throw;
        }
    }
}

TEST(AccelGraphPhase2TopologyContractTest, UnknownNodeConfigFieldIsRejectedByLoader) {
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

TEST(AccelGraphPhase2TopologyContractTest, MissingRequiredNodeConfigFieldIsRejectedByLoader) {
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

TEST(AccelGraphPhase2TopologyContractTest, WrongNodeConfigTypeIsRejectedByLoader) {
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

TEST(AccelGraphPhase2TopologyContractTest, TopologyFanOutConfigIsCheckedInAndUsesJsonOwnedInitialization) {
    const auto config_path = TopologyPath("accelgraph_phase2_spectrum_cpu_fanout_topology.json");
    ASSERT_TRUE(std::filesystem::exists(config_path));

    const auto doc = LoadJson(config_path);
    EXPECT_TRUE(HasSingleSourceAndDualSinks(doc));

    for (const auto& node : doc["nodes"]) {
        ASSERT_TRUE(node.contains("node_config"));
    }
}

TEST(AccelGraphPhase2TopologyContractTest, AllCheckedInTopologyJsonFilesAreParseableAndStructured) {
    const auto files = CheckedInTopologyJsonFiles();
    ASSERT_FALSE(files.empty());

    const std::array<std::string, 1> legacy_without_node_config = {{
        "accelgraph_phase2_transfer_topology.json",
    }};

    auto is_legacy_without_node_config = [&](const std::string& file_name) {
        return std::find(legacy_without_node_config.begin(), legacy_without_node_config.end(), file_name) !=
               legacy_without_node_config.end();
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
                ASSERT_TRUE(node.contains("node_config"));
            }
        }
    }
}

TEST(AccelGraphPhase2TopologyContractTest, TopologyTestsDoNotUseDirectConfigureCalls) {
    const std::array<std::string, 4> topology_test_files = {{
        "test_accelgraph_phase3_metal.cpp",
        "test_accelgraph_phase5_cuda.cpp",
        "test_accelgraph_phase6_spectrum.cpp",
        "test_accelgraph_phase6b_spectrum_cuda.cpp",
    }};

    const auto unit_dir = std::filesystem::path(__FILE__).parent_path();

    for (const auto& file_name : topology_test_files) {
        SCOPED_TRACE(file_name);
        const auto file_path = unit_dir / file_name;
        ASSERT_TRUE(std::filesystem::exists(file_path));

        std::ifstream in(file_path);
        ASSERT_TRUE(in.is_open());
        const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        EXPECT_EQ(content.find("->Configure("), std::string::npos)
            << "Direct Configure calls are not allowed in topology tests: " << file_name;
        EXPECT_EQ(content.find(".Configure("), std::string::npos)
            << "Direct Configure calls are not allowed in topology tests: " << file_name;
    }
}

TEST(AccelGraphPhase2TopologyContractTest, TopologyHelperDoesNotUseDirectConfigureBypass) {
    const auto helper_path = std::filesystem::path(__FILE__).parent_path() /
                             "AccelGraphTopologyTestUtils.hpp";
    ASSERT_TRUE(std::filesystem::exists(helper_path));

    std::ifstream in(helper_path);
    ASSERT_TRUE(in.is_open());
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_EQ(content.find("ConfigureNode"), std::string::npos);
    EXPECT_EQ(content.find("ConfigureTransferNode"), std::string::npos);
    EXPECT_EQ(content.find("JsonView("), std::string::npos);
    EXPECT_EQ(content.find("->Configure("), std::string::npos);
    EXPECT_EQ(content.find(".Configure("), std::string::npos);
}
