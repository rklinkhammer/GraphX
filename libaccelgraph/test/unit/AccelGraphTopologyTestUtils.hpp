// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "accelgraph/CudaAcceleratorProvider.hpp"
#include "accelgraph/MetalAcceleratorProvider.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphManagerCore.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

namespace accelgraph::test {

inline std::filesystem::path TopologyConfigPath(const char* source_file, const char* file_name) {
    return std::filesystem::path(source_file).parent_path().parent_path() /
           "config" / "topologies" / file_name;
}

inline std::filesystem::path PluginDirectoryPath() {
    return std::filesystem::path(PLUGIN_OUTPUT_DIRECTORY);
}

inline std::shared_ptr<graph::GraphExecutor> BuildExecutor(
    const std::filesystem::path& config_path,
    std::chrono::seconds timeout) {
    auto executor = graph::GraphExecutorBuilder()
        .WithJsonConfig(config_path.string())
        .WithPluginDirectory(PluginDirectoryPath().string())
        .WithExecutorTimeout(timeout)
        .Build();
    const auto initialized = executor->Init();
    if (!initialized.success) {
        throw std::runtime_error(initialized.message);
    }
    return executor;
}

template <typename NodeT>
std::shared_ptr<NodeT> ResolveNode(const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }

        auto typed = wrapper->GetNode<NodeT>();
        if (typed) {
            return typed;
        }
    }

    return nullptr;
}

inline bool IsExpectedMetalDiagnostic(const std::string& message) {
    return message.find(accelgraph::kMetalSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalRuntimeUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalNoCompatibleDeviceDiagnostic) != std::string::npos ||
           message.find(accelgraph::kMetalSessionCreationFailureDiagnostic) != std::string::npos;
}

inline bool IsExpectedCudaDiagnostic(const std::string& message) {
    return message.find(accelgraph::kCudaSupportNotCompiledDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaToolkitUnavailableDiagnostic) != std::string::npos ||
           message.find(accelgraph::kCudaRuntimeHeadersUnavailableDiagnostic) != std::string::npos ||
           message.find("driver") != std::string::npos ||
           message.find("device") != std::string::npos ||
           message.find("CUDA") != std::string::npos;
}

inline bool IsGraphBuildFailureDiagnostic(const std::string& message) {
    return message.find("Graph building failed") != std::string::npos ||
           message.find("Graph construction failed") != std::string::npos ||
           message.find("Failed to load graph configuration") !=
               std::string::npos;
}

inline bool IsMetalRuntimeAvailableForTests() {
#if !ACCELGRAPH_ENABLE_METAL
    return false;
#else
    accelgraph::MetalAcceleratorProvider provider;
    auto session = provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
    return session.has_value();
#endif
}

inline bool IsCudaRuntimeAvailableForTests() {
#if !ACCELGRAPH_ENABLE_CUDA
    return false;
#else
    accelgraph::CudaAcceleratorProvider provider;
    auto session = provider.CreateSession(accelgraph::AcceleratorSessionCreateRequest{});
    return session.has_value();
#endif
}

}  // namespace accelgraph::test
