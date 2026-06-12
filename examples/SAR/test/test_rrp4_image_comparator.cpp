#include <gtest/gtest.h>

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "sar/SarMaterializedImageSinkNode.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "sar/SarScenario001CpuReference.hpp"
#include "sar/SarScenario001GraphxRunner.hpp"

#include <nlohmann/json.hpp>

namespace {

#ifndef SAR_RRP4_IMAGE_COMPARATOR_PATH
#define SAR_RRP4_IMAGE_COMPARATOR_PATH "examples/SAR/tools/rrp4_image_comparator.py"
#endif

#ifndef SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH
#define SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH "examples/SAR/tools/rrp4_image_comparison_report.schema.json"
#endif

#ifndef SAR_SCENARIO_001_JSON_PATH
#define SAR_SCENARIO_001_JSON_PATH "examples/SAR/scenarios/scenario_001.json"
#endif

#ifndef SAR_SCENARIO_001_FIXTURE_DATA_PATH
#define SAR_SCENARIO_001_FIXTURE_DATA_PATH "examples/SAR/fixtures/scenario_001/deterministic_iq_phase_history_fixture_v1.json"
#endif

std::string Quote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

std::string PythonCommandPrefix() {
    return "PYTHONDONTWRITEBYTECODE=1 python3 -B";
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    EXPECT_TRUE(input.good()) << "unable to open text file: " << path;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    EXPECT_TRUE(input.good()) << "unable to open json file: " << path;

    nlohmann::json value;
    input >> value;
    return value;
}

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write json file: " << path;
    output << value.dump(2) << '\n';
}

void WriteFloat32Raster(const std::filesystem::path& path, const std::vector<float>& values) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good()) << "unable to write raw raster: " << path;

    for (const float value : values) {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
}

nlohmann::json MakeContract(std::string source_tool,
                            std::string provenance_class,
                            const std::filesystem::path& raw_path,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::string scenario_id = "scenario_001") {
    return {
        {"source_tool", std::move(source_tool)},
        {"provenance_class", std::move(provenance_class)},
        {"scenario_id", std::move(scenario_id)},
        {"format", "float32_raster"},
        {"layout", "row_major"},
        {"artifact_kind", "materialized_image"},
        {"dtype", "float32"},
        {"width", width},
        {"height", height},
        {"byte_count", static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4u},
        {"raw_path", raw_path.string()},
    };
}

class ScopedExternalDataEnvGuard {
public:
    explicit ScopedExternalDataEnvGuard(const std::string& value) {
        const char* current = std::getenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
        had_original_ = current != nullptr;
        if (had_original_) {
            original_ = current;
        }
#ifdef _WIN32
        _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str());
#else
        setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", value.c_str(), 1);
#endif
    }

    ~ScopedExternalDataEnvGuard() {
        if (had_original_) {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str());
#else
            setenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", original_.c_str(), 1);
#endif
        } else {
#ifdef _WIN32
            _putenv_s("GRAPHX_SAR_ALLOW_EXTERNAL_DATA", "");
#else
            unsetenv("GRAPHX_SAR_ALLOW_EXTERNAL_DATA");
#endif
        }
    }

private:
    bool had_original_{false};
    std::string original_{};
};

std::string ShellQuote(const std::filesystem::path& path) {
    return std::string("'") + path.string() + "'";
}

std::shared_ptr<sar::SarMaterializedImageSinkNode> ResolveMaterializedSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper || wrapper->GetType() != "SarMaterializedImageSinkNode") {
            continue;
        }
        return wrapper->GetNode<sar::SarMaterializedImageSinkNode>();
    }
    return nullptr;
}

struct GraphxRunCapture {
    std::vector<float> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::string hash{};
    std::filesystem::path artifact_bin_path{};
    std::filesystem::path artifact_contract_path{};
};

GraphxRunCapture RunScenario001Graphx(const std::filesystem::path& output_dir) {
    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto phase_fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};
    const auto plugin_dir = std::filesystem::path{PLUGIN_OUTPUT_DIRECTORY};

    if (!std::filesystem::exists(scenario_path)) {
        throw std::invalid_argument("missing scenario file");
    }
    if (!std::filesystem::exists(phase_fixture_path)) {
        throw std::invalid_argument("missing fixture file");
    }
    if (!std::filesystem::exists(plugin_dir)) {
        throw std::invalid_argument("missing plugin directory");
    }

    std::error_code remove_error;
    std::filesystem::remove_all(output_dir, remove_error);

    const std::string scaffold_command =
        "python3 " + ShellQuote(std::filesystem::path{SAR_RRP1_LOCAL_RUNNER_PATH}) +
        " --scenario " + ShellQuote(scenario_path) +
        " --output-dir " + ShellQuote(output_dir) +
        " > /dev/null";
    if (std::system(scaffold_command.c_str()) != 0) {
        throw std::invalid_argument("rrp1_local_runner failed to scaffold graphx layout");
    }

    const auto phase_fixture = sar::scenario001::graphx::LoadJsonFile(phase_fixture_path);
    const auto gotcha_fixture = sar::scenario001::graphx::ConvertToGotchaReplayFixture(phase_fixture);

    const auto converted_fixture_path = output_dir / "graphx" / "scenario_001_graphx_fixture.json";
    sar::scenario001::graphx::WriteJson(converted_fixture_path, gotcha_fixture);

    auto config = sar::scenario001::graphx::LoadJsonFile(output_dir / "graphx" / "graphx_config.json");
    for (auto& node : config["nodes"]) {
        if (node.at("id").get<std::string>() == "src") {
            node["node_config"]["fixture_path"] = converted_fixture_path.string();
            node["node_config"]["allow_external_fixture"] = true;
        }
    }

    const auto runtime_config_path = output_dir / "graphx" / "graphx_runtime_config.json";
    sar::scenario001::graphx::WriteJson(runtime_config_path, config);

    ScopedExternalDataEnvGuard external_data_opt_in("1");

    auto executor = graph::GraphExecutorBuilder()
                        .WithJsonConfig(runtime_config_path.string())
                        .WithPluginDirectory(plugin_dir.string())
                        .WithExecutorTimeout(std::chrono::seconds(10))
                        .Build();
    if (!executor) {
        throw std::invalid_argument("unable to construct graph executor");
    }
    if (!executor->GetGraphManager()) {
        throw std::invalid_argument("graph executor missing graph manager");
    }

    const auto run_result = executor->Execute();
    if (!run_result.success || !executor->IsCompletionSignaled()) {
        throw std::invalid_argument(run_result.message + " " + run_result.error_details);
    }

    auto sink = ResolveMaterializedSink(executor->GetGraphManager());
    if (!sink || !sink->has_materialized_image()) {
        throw std::invalid_argument("materialized image sink did not capture output");
    }

    const auto metadata = sink->last_capture_metadata();
    if (metadata.element_count == 0u) {
        throw std::invalid_argument("materialized image sink produced zero elements");
    }

    GraphxRunCapture out{};
    out.pixels = sink->last_materialized_image();
    out.width = static_cast<std::uint32_t>(metadata.element_count);
    out.height = 1u;
    out.hash = sar::scenario001::graphx::Fnv1a64Hex(out.pixels);
    out.artifact_bin_path = output_dir / "graphx_output_rrp4.bin";
    out.artifact_contract_path = output_dir / "graphx_output_contract_rrp4.json";

    sar::scenario001::graphx::WriteFloat32Raster(out.artifact_bin_path, out.pixels);
    sar::scenario001::graphx::WriteJson(
        out.artifact_contract_path,
        sar::scenario001::graphx::BuildGraphxArtifactContract(
            gotcha_fixture.at("derived_from_scenario").get<std::string>(),
            gotcha_fixture.at("source_fixture_id").get<std::string>(),
            out.width,
            out.height,
            out.artifact_bin_path,
            out.hash));

    return out;
}

} // namespace

TEST(Rrp4ImageComparatorTest, MatchingImageContractsProducePassReport) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_comparator_pass";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const std::vector<float> pixels(16u * 16u, 0.25f);
    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, pixels);
    WriteFloat32Raster(reference_raw, pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(
        graphx_contract_path,
        MakeContract("graphx", "graphx_runtime", graphx_raw, 16u, 16u));
        WriteJson(
            reference_contract_path,
            MakeContract("cpu-reference-backprojection", "deterministic_internal_reference", reference_raw, 16u, 16u));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(report_path));

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("schema_version").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report.at("passed").get<bool>());
    ASSERT_TRUE(report.at("checks").is_array());
    ASSERT_TRUE(report.at("reasons").is_array());
    EXPECT_TRUE(report.at("reasons").empty());

    const auto& metrics = report.at("metrics");
    EXPECT_DOUBLE_EQ(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("rms").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(metrics.at("relative_l2").get<double>(), 0.0);
}

TEST(Rrp4ImageComparatorTest, RealScenarioArtifactsProduceDeterministicFailReportWithReasons) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_real_compare_pass";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const auto scenario_path = std::filesystem::path{SAR_SCENARIO_001_JSON_PATH};
    const auto fixture_path = std::filesystem::path{SAR_SCENARIO_001_FIXTURE_DATA_PATH};

    const auto reference_artifact = sar::reference::scenario001::RunCpuReferenceBackprojection(
        scenario_path,
        fixture_path);
    const auto reference_paths = sar::reference::scenario001::WriteArtifact(
        reference_artifact,
        temp_dir / "reference",
        "scenario_001_cpu_reference");

    const auto graphx_capture = [&]() {
        const auto out_dir = temp_dir / "graphx";
        std::error_code cleanup_error;
        std::filesystem::remove_all(out_dir, cleanup_error);
        return RunScenario001Graphx(out_dir);
    }();

    ASSERT_TRUE(std::filesystem::exists(reference_paths.contract_path));
    ASSERT_TRUE(std::filesystem::exists(graphx_capture.artifact_contract_path));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_capture.artifact_contract_path) +
        " --reference-contract " + Quote(reference_paths.contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_NE(std::system(command.c_str()), 0);

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("schema_version").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    EXPECT_EQ(report.at("scenario_id").get<std::string>(), "scenario_001");
    EXPECT_EQ(report.at("verdict").get<std::string>(), "fail");
    EXPECT_FALSE(report.at("passed").get<bool>());
    ASSERT_FALSE(report.at("reasons").empty());
    const auto reasons = report.at("reasons").dump();
    EXPECT_NE(reasons.find("dimensions_match"), std::string::npos);
    const auto metrics = report.at("metrics");
    EXPECT_TRUE(metrics.at("l_inf").is_null());
    EXPECT_TRUE(metrics.at("rms").is_null());
    EXPECT_TRUE(metrics.at("relative_l2").is_null());
}

TEST(Rrp4ImageComparatorTest, MissingRequiredContractFieldIsReportedExplicitly) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_missing_field";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const std::vector<float> pixels(4u * 4u, 0.25f);
    const auto raw_path = temp_dir / "graphx.bin";
    WriteFloat32Raster(raw_path, pixels);

    auto graphx_contract = MakeContract("graphx", "graphx_runtime", raw_path, 4u, 4u);
    auto reference_contract = MakeContract("cpu-reference-backprojection", "deterministic_internal_reference", raw_path, 4u, 4u);
    graphx_contract.erase("raw_path");

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(graphx_contract_path, graphx_contract);
    WriteJson(reference_contract_path, reference_contract);

    const auto stderr_path = temp_dir / "stderr.log";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(temp_dir / "comparison_report.json") +
        " 1>/dev/null 2>" + Quote(stderr_path);

    ASSERT_NE(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(stderr_path));
    const auto stderr_text = ReadTextFile(stderr_path);
    EXPECT_NE(stderr_text.find("graphx_contract.raw_path is required"), std::string::npos);
}

TEST(Rrp4ImageComparatorTest, RepeatedRunsProduceIdenticalReportsForEqualArtifacts) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_repeatable_reports";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const std::vector<float> pixels(8u * 8u, 0.125f);
    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, pixels);
    WriteFloat32Raster(reference_raw, pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(graphx_contract_path, MakeContract("graphx", "graphx_runtime", graphx_raw, 8u, 8u));
    WriteJson(reference_contract_path, MakeContract("cpu-reference-backprojection", "deterministic_internal_reference", reference_raw, 8u, 8u));

    const auto report_path_1 = temp_dir / "comparison_report_1.json";
    const auto report_path_2 = temp_dir / "comparison_report_2.json";

    const std::string command_1 =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path_1) +
        " > /dev/null";
    const std::string command_2 =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path_2) +
        " > /dev/null";

    ASSERT_EQ(std::system(command_1.c_str()), 0);
    ASSERT_EQ(std::system(command_2.c_str()), 0);

    const auto report_1 = LoadJson(report_path_1);
    const auto report_2 = LoadJson(report_path_2);
    EXPECT_EQ(report_1.dump(), report_2.dump());
}

TEST(Rrp4ImageComparatorTest, MismatchedImageContractsProduceFailReport) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_comparator_fail";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    std::vector<float> graphx_pixels(16u * 16u, 0.25f);
    std::vector<float> reference_pixels(16u * 16u, 0.25f);
    graphx_pixels[17] = 0.75f;

    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "reference.bin";
    WriteFloat32Raster(graphx_raw, graphx_pixels);
    WriteFloat32Raster(reference_raw, reference_pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(
        graphx_contract_path,
        MakeContract("graphx", "graphx_runtime", graphx_raw, 16u, 16u));
    WriteJson(
        reference_contract_path,
        MakeContract("cpu-reference-backprojection", "deterministic_internal_reference", reference_raw, 16u, 16u));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_NE(std::system(command.c_str()), 0);
    ASSERT_TRUE(std::filesystem::exists(report_path));

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("verdict").get<std::string>(), "fail");
    EXPECT_FALSE(report.at("passed").get<bool>());
    ASSERT_TRUE(report.at("checks").is_array());
    ASSERT_FALSE(report.at("reasons").empty());

    const auto& metrics = report.at("metrics");
    EXPECT_GT(metrics.at("l_inf").get<double>(), 0.0);
    EXPECT_GT(metrics.at("rms").get<double>(), 0.0);
    EXPECT_GT(metrics.at("relative_l2").get<double>(), 0.0);
}

TEST(Rrp4ImageComparatorTest, ReportSchemaDeclaresPassFailShape) {
    const auto schema_path = std::filesystem::path{SAR_RRP4_IMAGE_COMPARISON_SCHEMA_PATH};
    ASSERT_TRUE(std::filesystem::exists(schema_path));

    const auto schema = LoadJson(schema_path);
    EXPECT_EQ(schema.at("$id").get<std::string>(), "graphx.sar.image_comparison_report.v1");
    ASSERT_TRUE(schema.contains("required"));
    ASSERT_TRUE(schema.contains("properties"));

    const auto required = schema.at("required");
    ASSERT_TRUE(required.is_array());
    EXPECT_NE(std::find(required.begin(), required.end(), "verdict"), required.end());
    EXPECT_NE(std::find(required.begin(), required.end(), "checks"), required.end());
    EXPECT_NE(std::find(required.begin(), required.end(), "metrics"), required.end());
}

TEST(Rrp4ImageComparatorTest, DeterministicInternalReferenceIsAcceptedByBoundaryChecks) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "graphx_rrp4_comparator_deterministic_ref";
    std::error_code remove_error;
    std::filesystem::remove_all(temp_dir, remove_error);
    ASSERT_TRUE(std::filesystem::create_directories(temp_dir));

    const std::vector<float> pixels(8u * 8u, 0.125f);
    const auto graphx_raw = temp_dir / "graphx.bin";
    const auto reference_raw = temp_dir / "deterministic_reference.bin";
    WriteFloat32Raster(graphx_raw, pixels);
    WriteFloat32Raster(reference_raw, pixels);

    const auto graphx_contract_path = temp_dir / "graphx_contract.json";
    const auto reference_contract_path = temp_dir / "reference_contract.json";
    WriteJson(
        graphx_contract_path,
        MakeContract("graphx", "graphx_runtime", graphx_raw, 8u, 8u));
    WriteJson(
        reference_contract_path,
        MakeContract("cpu-reference-backprojection", "deterministic_internal_reference", reference_raw, 8u, 8u));

    const auto report_path = temp_dir / "comparison_report.json";
    const std::string command =
        PythonCommandPrefix() + " " + Quote(std::filesystem::path{SAR_RRP4_IMAGE_COMPARATOR_PATH}) +
        " compare --graphx-contract " + Quote(graphx_contract_path) +
        " --reference-contract " + Quote(reference_contract_path) +
        " --report-json " + Quote(report_path) +
        " > /dev/null";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const auto report = LoadJson(report_path);
    EXPECT_EQ(report.at("verdict").get<std::string>(), "pass");
    EXPECT_TRUE(report.at("passed").get<bool>());
}