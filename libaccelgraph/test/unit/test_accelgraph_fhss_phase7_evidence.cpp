// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "AccelGraphTopologyTestUtils.hpp"
#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "accelgraph/fhss/FHSSBranchMetricGraphNode.hpp"
#include "accelgraph/fhss/FHSSChannelizerGraphNode.hpp"
#include "accelgraph/fhss/FHSSDownconverterGraphNode.hpp"
#include "accelgraph/fhss/FHSSPerChannelPulseDetectorGraphNode.hpp"
#include "graph/GraphExecutor.hpp"
#include "dsp/fhss/FHSSMessageAssemblerNode.hpp"
#include "dsp/fhss/FHSSMessageSinkNode.hpp"
#include "dsp/fhss/FHSSPreambleDetectorNode.hpp"
#include "dsp/fhss/FHSSPulseWordDecoderNode.hpp"

namespace {

using Clock = std::chrono::steady_clock;

enum class EvidenceKind {
    Downconverter,
    Channelizer,
    Detector,
    BranchMetric,
    Hybrid,
};

struct EvidenceCase {
    std::string name;
    std::string phase;
    std::string stage_name;
    EvidenceKind kind;
    std::filesystem::path topology_path;
    std::string requested_backend;
    bool strict_mode{true};
    bool allow_fallback{false};
};

struct StageEvidence {
    std::string stage_name;
    std::string node_type;
    std::string requested_backend;
    std::string selected_backend;
    bool native_gpu_execution{false};
    bool used_fallback{false};
    std::string fallback_diagnostic;
};

struct RepoIdentity {
    std::string branch;
    std::string commit_sha;
    nlohmann::json diff_identity;
};

std::filesystem::path RepoRoot() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path EvidenceArtifactPath() {
    return RepoRoot() / "build/fhss_accelgraph_evidence.json";
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string CaptureCommand(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return {};
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }
    (void)pclose(pipe);
    return Trim(output);
}

std::string UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

RepoIdentity ReadRepoIdentity() {
    RepoIdentity identity;
    identity.branch = CaptureCommand("git rev-parse --abbrev-ref HEAD 2>/dev/null");
    identity.commit_sha = CaptureCommand("git rev-parse HEAD 2>/dev/null");
    const std::string status = CaptureCommand("git status --porcelain 2>/dev/null | head -n 1");
    const bool dirty = !status.empty();
    identity.diff_identity = {
        {"type", "working_tree"},
        {"value", dirty ? "uncommitted_changes_present" : "clean"},
        {"working_tree_dirty", dirty},
    };
    return identity;
}

std::string BackendToString(accelgraph::AcceleratorBackend backend) {
    switch (backend) {
        case accelgraph::AcceleratorBackend::Cpu:
            return "cpu";
        case accelgraph::AcceleratorBackend::Metal:
            return "metal";
        case accelgraph::AcceleratorBackend::Cuda:
            return "cuda";
    }
    return "unknown";
}

std::filesystem::path TopologyPath(const char* file_name) {
    return accelgraph::test::TopologyConfigPath(__FILE__, file_name);
}

std::vector<EvidenceCase> EvidenceCases() {
    return {
        {"phase2_downconverter_cpu", "phase2", "downconverter", EvidenceKind::Downconverter,
         TopologyPath("accelgraph_fhss_downconverter_cpu_topology.json"), "cpu", true, false},
        {"phase2_downconverter_metal_allow_fallback", "phase2", "downconverter",
         EvidenceKind::Downconverter,
         TopologyPath("accelgraph_fhss_downconverter_metal_allow_fallback_topology.json"),
         "metal", false, true},
        {"phase3_channelizer_cpu", "phase3", "channelizer", EvidenceKind::Channelizer,
         TopologyPath("accelgraph_fhss_channelizer_cpu_topology.json"), "cpu", true, false},
        {"phase3_channelizer_metal_allow_fallback", "phase3", "channelizer",
         EvidenceKind::Channelizer,
         TopologyPath("accelgraph_fhss_channelizer_metal_allow_fallback_topology.json"),
         "metal", false, true},
        {"phase4_detector_cpu", "phase4", "detector", EvidenceKind::Detector,
         TopologyPath("accelgraph_fhss_detector_cpu_topology.json"), "cpu", true, false},
        {"phase4_detector_cuda_allow_fallback", "phase4", "detector", EvidenceKind::Detector,
         TopologyPath("accelgraph_fhss_detector_cuda_allow_fallback_topology.json"), "cuda",
         false, true},
        {"phase5_branch_metric_cpu", "phase5", "branch_metric", EvidenceKind::BranchMetric,
         TopologyPath("accelgraph_fhss_branch_metric_cpu_topology.json"), "cpu", true, false},
        {"phase5_branch_metric_cuda_allow_fallback", "phase5", "branch_metric",
         EvidenceKind::BranchMetric,
         TopologyPath("accelgraph_fhss_branch_metric_cuda_allow_fallback_topology.json"),
         "cuda", false, true},
        {"phase6_hybrid_cpu", "phase6", "hybrid", EvidenceKind::Hybrid,
         TopologyPath("accelgraph_fhss_e2e_hybrid_cpu_topology.json"), "cpu", true, false},
        {"phase6_hybrid_metal_allow_fallback", "phase6", "hybrid", EvidenceKind::Hybrid,
         TopologyPath("accelgraph_fhss_e2e_hybrid_metal_allow_fallback_topology.json"),
         "metal", false, true},
        {"phase6_hybrid_cuda_strict", "phase6", "hybrid", EvidenceKind::Hybrid,
         TopologyPath("accelgraph_fhss_e2e_hybrid_cuda_topology.json"), "cuda", true, false},
    };
}

std::set<std::filesystem::path> ExpectedTopologyFiles() {
    std::set<std::filesystem::path> files;
    for (const auto& item : EvidenceCases()) {
        files.insert(item.topology_path);
    }
    return files;
}

StageEvidence CaptureStageEvidence(const std::string& stage_name,
                                   const std::string& node_type,
                                   const auto& node) {
    StageEvidence evidence;
    evidence.stage_name = stage_name;
    evidence.node_type = node_type;
    evidence.requested_backend = BackendToString(node->RequestedBackend());
    evidence.selected_backend = BackendToString(node->SelectedBackend());
    evidence.native_gpu_execution = node->SelectedBackend() != accelgraph::AcceleratorBackend::Cpu &&
                                    !node->UsedFallback();
    evidence.used_fallback = node->UsedFallback();
    evidence.fallback_diagnostic = node->FallbackDiagnostic();
    return evidence;
}

nlohmann::json StageEvidenceToJson(const StageEvidence& stage) {
    return {
        {"stage_name", stage.stage_name},
        {"node_type", stage.node_type},
        {"requested_backend", stage.requested_backend},
        {"selected_backend", stage.selected_backend},
        {"native_gpu_execution", stage.native_gpu_execution},
        {"used_fallback", stage.used_fallback},
        {"fallback_diagnostic", stage.fallback_diagnostic.empty() ? nlohmann::json(nullptr)
                                                                    : nlohmann::json(stage.fallback_diagnostic)},
    };
}

bool IsKnownBackendBuildDiagnostic(const std::string& message) {
    return accelgraph::test::IsGraphBuildFailureDiagnostic(message) ||
           accelgraph::test::IsExpectedMetalDiagnostic(message) ||
           accelgraph::test::IsExpectedCudaDiagnostic(message) ||
           message.find(accelgraph::kMetalSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalRuntimeUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaToolkitUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaRuntimeHeadersUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssDownconverterMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssDownconverterCudaNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssChannelizerMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssChannelizerCudaNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssPerChannelPulseDetectorCudaNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssBranchMetricMetalNativeNotImplementedDiagnostic) != std::string::npos ||
           message.find(accelgraph::fhss::kFhssBranchMetricCudaNativeNotImplementedDiagnostic) != std::string::npos;
}

nlohmann::json BuildReportSummary(bool output_present,
                                  const std::string& summary,
                                  const nlohmann::json& details = nlohmann::json::object()) {
    return {
        {"output_present", output_present},
        {"summary", summary},
        {"details", details},
    };
}

std::string ClassifyOutcome(const nlohmann::json& row) {
    const std::string status = row.value("status", "unknown");
    const std::string requested_backend = row.value("requested_backend", "");
    const std::string diagnostic = row.value("diagnostic", "");

    if (status == "skipped") {
        if (diagnostic.find("missing topology") != std::string::npos ||
            diagnostic.find("not compiled") != std::string::npos ||
            diagnostic.find("not detected") != std::string::npos ||
            diagnostic.find("unavailable") != std::string::npos) {
            return "build_or_backend_unavailable";
        }
        if (requested_backend == "cuda" || requested_backend == "metal") {
            return "strict_skipped";
        }
        return "skipped";
    }

    if (requested_backend == "cpu") {
        return row.value("native_gpu_execution", false) ? "native_gpu" : "cpu_reference";
    }

    if (row.value("native_gpu_execution", false)) {
        return requested_backend == "cuda" ? "native_cuda" : "native_metal";
    }

    if (!row.value("fallback_stages", nlohmann::json::array()).empty()) {
        return "cpu_fallback";
    }

    return requested_backend == "cuda" ? "host_specific_cuda_pending" : "host_specific_pending";
}

nlohmann::json ExecuteEvidenceCase(const EvidenceCase& item) {
    nlohmann::json row = {
        {"benchmark_name", item.name},
        {"phase", item.phase},
        {"stage_name", item.stage_name},
        {"topology_path", item.topology_path.string()},
        {"requested_backend", item.requested_backend},
        {"strict_mode", item.strict_mode},
        {"allow_fallback", item.allow_fallback},
        {"status", "pending"},
        {"native_gpu_execution", false},
        {"fallback_stages", nlohmann::json::array()},
        {"stages", nlohmann::json::array()},
        {"backend_diagnostics", nlohmann::json::array()},
        {"timing_summary", nlohmann::json::object()},
        {"validation_summary", nlohmann::json::object()},
        {"diagnostic", nullptr},
        {"outcome_classification", "pending"},
    };

    if (!std::filesystem::exists(item.topology_path)) {
        row["status"] = "skipped";
        row["diagnostic"] = "missing topology: " + item.topology_path.string();
        row["outcome_classification"] = ClassifyOutcome(row);
        return row;
    }

#if !ACCELGRAPH_ENABLE_CUDA
    if (item.requested_backend == "cuda") {
        row["status"] = "skipped";
        row["diagnostic"] = accelgraph::kCudaSupportNotCompiledDiagnostic;
        row["outcome_classification"] = ClassifyOutcome(row);
        return row;
    }
#endif

#if !ACCELGRAPH_ENABLE_METAL
    if (item.requested_backend == "metal") {
        row["status"] = "skipped";
        row["diagnostic"] = accelgraph::kMetalSupportNotCompiledDiagnostic;
        row["outcome_classification"] = ClassifyOutcome(row);
        return row;
    }
#endif

    std::shared_ptr<graph::GraphExecutor> executor;
    const auto build_start = Clock::now();
    try {
        executor = accelgraph::test::BuildExecutor(item.topology_path, std::chrono::seconds(20));
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        if (IsKnownBackendBuildDiagnostic(message)) {
            row["status"] = "skipped";
            row["diagnostic"] = message;
            row["outcome_classification"] = ClassifyOutcome(row);
            return row;
        }
        throw;
    }
    const auto build_end = Clock::now();

    if (!executor) {
        throw std::runtime_error("failed to build executor for " + item.topology_path.string());
    }
    auto graph_manager = executor->GetGraphManager();
    if (!graph_manager) {
        throw std::runtime_error("missing graph manager for " + item.topology_path.string());
    }

    const auto execute_start = Clock::now();
    const auto run_result = executor->Execute();
    const auto execute_end = Clock::now();
    if (!run_result.success) {
        throw std::runtime_error("graph execute failed for " + item.name + ": " + run_result.message + " " + run_result.error_details);
    }

    nlohmann::json validation_summary = nlohmann::json::object();
    std::vector<StageEvidence> stages;

    switch (item.kind) {
        case EvidenceKind::Downconverter: {
            auto node = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssDownconverterNode>(graph_manager);
            auto sink = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssDownconverterSinkNode>(graph_manager);
            if (!node || !sink) {
                throw std::runtime_error("failed to resolve downconverter nodes for " + item.name);
            }
            auto output = sink->LastPacket();
            if (!output.has_value()) {
                throw std::runtime_error("downconverter sink produced no packet for " + item.name);
            }
            EXPECT_TRUE(output->sidecar.downconverter.passthrough);
            EXPECT_EQ(output->sidecar.downconverter.phase_convention,
                      dsp::fhss::FHSSGraphXDownconverterPhaseConvention::PassthroughNoPhaseRotation);
            validation_summary = BuildReportSummary(
                true,
                "downconverter packet present",
                {
                    {"passthrough", output->sidecar.downconverter.passthrough},
                    {"phase_convention", static_cast<int>(output->sidecar.downconverter.phase_convention)},
                    {"translation_frequency_hz", output->sidecar.downconverter.translation_frequency_hz},
                });
            stages.push_back(CaptureStageEvidence(item.stage_name, "AccelFhssDownconverterNode", node));
            break;
        }
        case EvidenceKind::Channelizer: {
            auto node = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssChannelizerNode>(graph_manager);
            auto sink = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssChannelizerSinkNode>(graph_manager);
            if (!node || !sink) {
                throw std::runtime_error("failed to resolve channelizer nodes for " + item.name);
            }
            auto output = sink->LastPacket();
            if (!output.has_value()) {
                throw std::runtime_error("channelizer sink produced no packet for " + item.name);
            }
            EXPECT_EQ(output->sidecar.channel.channel_id, 24u);
            EXPECT_EQ(output->sidecar.channel.frequency_index, 24u);
            validation_summary = BuildReportSummary(
                true,
                "channelizer packet present",
                {
                    {"channel_id", output->sidecar.channel.channel_id},
                    {"frequency_index", output->sidecar.channel.frequency_index},
                    {"decimation_factor", output->sidecar.channel.decimation_factor},
                });
            stages.push_back(CaptureStageEvidence(item.stage_name, "AccelFhssChannelizerNode", node));
            break;
        }
        case EvidenceKind::Detector: {
            auto node = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode>(graph_manager);
            auto sink = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssPerChannelPulseDetectorSinkNode>(graph_manager);
            if (!node || !sink) {
                throw std::runtime_error("failed to resolve detector nodes for " + item.name);
            }
            auto output = sink->LastPacket();
            if (!output.has_value()) {
                throw std::runtime_error("detector sink produced no packet for " + item.name);
            }
            EXPECT_EQ(output->sidecar.channel.channel_id, 24u);
            EXPECT_EQ(output->sidecar.channel.frequency_index, 24u);
            EXPECT_FALSE(output->sidecar.detected_pulses.empty());
            validation_summary = BuildReportSummary(
                true,
                "detector packet present",
                {
                    {"channel_id", output->sidecar.channel.channel_id},
                    {"frequency_index", output->sidecar.channel.frequency_index},
                    {"detected_pulses", output->sidecar.detected_pulses.size()},
                });
            stages.push_back(CaptureStageEvidence(item.stage_name, "AccelFhssPerChannelPulseDetectorNode", node));
            break;
        }
        case EvidenceKind::BranchMetric: {
            auto node = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssBranchMetricNode>(graph_manager);
            auto sink = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssBranchMetricSinkNode>(graph_manager);
            if (!node || !sink) {
                throw std::runtime_error("failed to resolve branch metric nodes for " + item.name);
            }
            auto output = sink->LastPacket();
            if (!output.has_value()) {
                throw std::runtime_error("branch metric sink produced no packet for " + item.name);
            }
            if (output->sidecar.pulse_metrics.empty()) {
                throw std::runtime_error("branch metric sink produced no pulse metrics for " + item.name);
            }
            const auto& metric = output->sidecar.pulse_metrics.front();
            EXPECT_EQ(metric.candidate.pulse.timing.channel_id, 24u);
            EXPECT_EQ(metric.candidate.pulse.frequency.frequency_index, 24u);
            validation_summary = BuildReportSummary(
                true,
                "branch metric packet present",
                {
                    {"pulse_metrics", output->sidecar.pulse_metrics.size()},
                    {"first_channel_id", metric.candidate.pulse.timing.channel_id},
                    {"first_frequency_index", metric.candidate.pulse.frequency.frequency_index},
                });
            stages.push_back(CaptureStageEvidence(item.stage_name, "AccelFhssBranchMetricNode", node));
            break;
        }
        case EvidenceKind::Hybrid: {
            auto downconverter = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssDownconverterNode>(graph_manager);
            auto channelizer = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssChannelizerNode>(graph_manager);
            auto detector = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssPerChannelPulseDetectorNode>(graph_manager);
            auto branch_metric = accelgraph::test::ResolveNode<accelgraph::fhss::AccelFhssBranchMetricNode>(graph_manager);
            auto sink = accelgraph::test::ResolveNode<dsp::fhss::FHSSMessageSinkNode>(graph_manager);
            if (!downconverter || !channelizer || !detector || !branch_metric || !sink) {
                throw std::runtime_error("failed to resolve hybrid nodes for " + item.name);
            }

            auto diagnostics_view = sink->GetDiagnostics();
            const auto& diagnostics = diagnostics_view.Raw();
            if (!diagnostics.is_object()) {
                throw std::runtime_error("hybrid sink diagnostics are not an object for " + item.name);
            }
            EXPECT_TRUE(diagnostics.contains("pulse_count"));
            EXPECT_TRUE(diagnostics.contains("preamble_lock"));
            EXPECT_TRUE(diagnostics.contains("decoded_pulses"));
            EXPECT_TRUE(diagnostics.contains("message_status"));
            const bool decoded_pulses_present = diagnostics.contains("decoded_pulses") &&
                                                diagnostics.at("decoded_pulses").is_array() &&
                                                !diagnostics.at("decoded_pulses").empty();
            validation_summary = BuildReportSummary(
                decoded_pulses_present,
                decoded_pulses_present ? "hybrid sink diagnostics present"
                                       : "hybrid sink diagnostics missing",
                {
                    {"pulse_count_present", diagnostics.contains("pulse_count")},
                    {"preamble_lock_present", diagnostics.contains("preamble_lock")},
                    {"decoded_pulses_present", decoded_pulses_present},
                    {"message_status_present", diagnostics.contains("message_status")},
                });

            stages.push_back(CaptureStageEvidence("downconverter", "AccelFhssDownconverterNode", downconverter));
            stages.push_back(CaptureStageEvidence("channelizer", "AccelFhssChannelizerNode", channelizer));
            stages.push_back(CaptureStageEvidence("detector", "AccelFhssPerChannelPulseDetectorNode", detector));
            stages.push_back(CaptureStageEvidence("branch_metric", "AccelFhssBranchMetricNode", branch_metric));
            break;
        }
    }

    row["timing_summary"] = {
        {"build_wall_ms", std::chrono::duration<double, std::milli>(build_end - build_start).count()},
        {"execute_wall_ms", std::chrono::duration<double, std::milli>(execute_end - execute_start).count()},
        {"run_elapsed_ms", run_result.run_elapsed_time_ms},
    };
    row["validation_summary"] = validation_summary;

    bool native_gpu_execution = false;
    nlohmann::json fallback_stages = nlohmann::json::array();
    nlohmann::json backend_diagnostics = nlohmann::json::array();
    for (const auto& stage : stages) {
        row["stages"].push_back(StageEvidenceToJson(stage));
        backend_diagnostics.push_back({
            {"stage_name", stage.stage_name},
            {"selected_backend", stage.selected_backend},
            {"used_fallback", stage.used_fallback},
            {"fallback_diagnostic", stage.fallback_diagnostic.empty() ? nlohmann::json(nullptr)
                                                                         : nlohmann::json(stage.fallback_diagnostic)},
        });
        native_gpu_execution = native_gpu_execution || stage.native_gpu_execution;
        if (stage.used_fallback) {
            fallback_stages.push_back(stage.stage_name);
        }
    }

    row["native_gpu_execution"] = native_gpu_execution;
    row["fallback_stages"] = fallback_stages;
    row["backend_diagnostics"] = backend_diagnostics;
    row["status"] = "pass";
    row["outcome_classification"] = ClassifyOutcome(row);

    if (item.requested_backend != "cpu" && fallback_stages.empty()) {
        row["native_gpu_execution"] = true;
        row["outcome_classification"] = ClassifyOutcome(row);
    }

    return row;
}

nlohmann::json BuildReport(const std::vector<EvidenceCase>& cases) {
    const RepoIdentity repo_identity = ReadRepoIdentity();
    nlohmann::json report;
    report["schema"] = "graphx.accelgraph.phase7.fhss.evidence.v1";
    report["phase"] = "7";
    report["workload_family"] = "fhss";
    report["generated_at_utc"] = UtcNow();
    report["host"] = {
        {"hostname", std::getenv("HOSTNAME") ? std::getenv("HOSTNAME") : "unknown"},
#if defined(__APPLE__)
        {"operating_system", "macOS"},
#elif defined(__linux__)
        {"operating_system", "Linux"},
#else
        {"operating_system", "unknown"},
#endif
#if defined(__aarch64__)
        {"architecture", "aarch64"},
#elif defined(__x86_64__)
        {"architecture", "x86_64"},
#else
        {"architecture", "unknown"},
#endif
        {"metal_enabled", ACCELGRAPH_ENABLE_METAL},
        {"cuda_enabled", ACCELGRAPH_ENABLE_CUDA},
        {"metal_runtime_available_for_tests", accelgraph::test::IsMetalRuntimeAvailableForTests()},
        {"cuda_runtime_available_for_tests", accelgraph::test::IsCudaRuntimeAvailableForTests()},
    };
    report["branch"] = repo_identity.branch;
    report["commit_sha"] = repo_identity.commit_sha;
    report["diff_identity"] = repo_identity.diff_identity;

    std::set<std::filesystem::path> discovered_topologies;
    for (const auto& item : cases) {
        discovered_topologies.insert(item.topology_path);
        report["results"].push_back(ExecuteEvidenceCase(item));
    }

    report["discovered_topologies"] = nlohmann::json::array();
    for (const auto& topology : discovered_topologies) {
        report["discovered_topologies"].push_back(topology.string());
    }

    return report;
}

void WriteReport(const nlohmann::json& report) {
    const auto output_path = EvidenceArtifactPath();
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream out(output_path);
    ASSERT_TRUE(out.is_open()) << "failed to open " << output_path;
    out << std::setw(2) << report << "\n";
}

}  // namespace

TEST(AccelGraphFhssEvidenceTest, DiscoversExpectedTopologyFiles) {
    const auto expected_files = ExpectedTopologyFiles();
    ASSERT_FALSE(expected_files.empty());

    for (const auto& path : expected_files) {
        SCOPED_TRACE(path.string());
        if (path.filename() == "accelgraph_fhss_e2e_hybrid_cuda_topology.json") {
            EXPECT_FALSE(std::filesystem::exists(path)) << path.string();
            continue;
        }
        EXPECT_TRUE(std::filesystem::exists(path)) << path.string();
    }
}

TEST(AccelGraphFhssEvidenceTest, FallbackExecutionIsNeverMarkedAsNativeGpuExecution) {
    StageEvidence fallback_stage;
    fallback_stage.stage_name = "downconverter";
    fallback_stage.node_type = "AccelFhssDownconverterNode";
    fallback_stage.requested_backend = "metal";
    fallback_stage.selected_backend = "cpu";
    fallback_stage.native_gpu_execution = false;
    fallback_stage.used_fallback = true;
    fallback_stage.fallback_diagnostic = "metal provider unavailable; cpu fallback selected";

    EXPECT_FALSE(fallback_stage.native_gpu_execution);
    EXPECT_TRUE(fallback_stage.used_fallback);
    EXPECT_FALSE(fallback_stage.fallback_diagnostic.empty());
}

TEST(AccelGraphFhssEvidenceTest, GeneratesBenchmarkEvidenceArtifactWithBackendDiagnostics) {
    const auto cases = EvidenceCases();
    ASSERT_FALSE(cases.empty());

    const auto report = BuildReport(cases);
    ASSERT_EQ(report.value("phase", ""), "7");
    ASSERT_EQ(report.value("workload_family", ""), "fhss");
    ASSERT_TRUE(report.contains("results"));
    ASSERT_TRUE(report["results"].is_array());
    ASSERT_FALSE(report["results"].empty());

    bool saw_cpu = false;
    bool saw_hybrid = false;
    bool saw_skipped = false;
    bool saw_backend_diagnostics = false;
    bool saw_native_gpu_execution = false;
    bool saw_fallback_stage = false;

    for (const auto& row : report["results"]) {
        ASSERT_TRUE(row.is_object());
        ASSERT_TRUE(row.contains("benchmark_name"));
        ASSERT_TRUE(row.contains("stage_name"));
        ASSERT_TRUE(row.contains("topology_path"));
        ASSERT_TRUE(row.contains("status"));
        ASSERT_TRUE(row.contains("native_gpu_execution"));
        ASSERT_TRUE(row.contains("stages"));
        ASSERT_TRUE(row.contains("backend_diagnostics"));
        ASSERT_TRUE(row.contains("validation_summary"));

        if (row.value("requested_backend", "") == "cpu") {
            saw_cpu = true;
        }
        if (row.value("stage_name", "") == "hybrid") {
            saw_hybrid = true;
        }
        if (row.value("status", "") == "skipped") {
            saw_skipped = true;
        }
        if (!row["backend_diagnostics"].empty()) {
            saw_backend_diagnostics = true;
        }
        if (row["native_gpu_execution"].get<bool>()) {
            saw_native_gpu_execution = true;
        }
        if (!row["fallback_stages"].empty()) {
            saw_fallback_stage = true;
        }

        if (row["status"].get<std::string>() == "pass" && row["stages"].is_array()) {
            for (const auto& stage : row["stages"]) {
                ASSERT_TRUE(stage.is_object());
                ASSERT_TRUE(stage.contains("used_fallback"));
                ASSERT_TRUE(stage.contains("native_gpu_execution"));
                if (stage["used_fallback"].get<bool>()) {
                    EXPECT_FALSE(stage["native_gpu_execution"].get<bool>());
                }
            }
        }
    }

    EXPECT_TRUE(saw_cpu);
    EXPECT_TRUE(saw_hybrid);
    EXPECT_TRUE(saw_backend_diagnostics);
    EXPECT_TRUE(saw_native_gpu_execution || saw_skipped);
    EXPECT_TRUE(saw_fallback_stage || saw_skipped);

    WriteReport(report);
    EXPECT_TRUE(std::filesystem::exists(EvidenceArtifactPath()));
}
