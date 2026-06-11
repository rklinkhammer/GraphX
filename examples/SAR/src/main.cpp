#include "sar/SarMessages.hpp"
#include "sar/SarDiagnosticsSinkNode.hpp"
#include "sar/SarRuntimeHelpers.hpp"

#include "graph/GraphExecutorBuilder.hpp"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

#ifndef SAR_PLUGIN_OUTPUT_DIRECTORY
#define SAR_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

namespace {

} // namespace

int main(int argc, char** argv) {
    [[maybe_unused]] const sar::SarSidecar contract_probe{};
    const char* defaultConfig = "examples/SAR/config/sar_stripmap_definitive.json";
    const char* configPath = (argc > 1) ? argv[1] : defaultConfig;
    const char* pluginDirectory = (argc > 2) ? argv[2] : SAR_PLUGIN_OUTPUT_DIRECTORY;

    std::cout << "GraphX SAR example runtime" << '\n';
    std::cout << "Topology config: " << configPath << '\n';
    std::cout << "Plugin directory: " << pluginDirectory << '\n';
    for (int i = 3; i < argc; ++i) {
        std::cout << "Additional plugin directory: " << argv[i] << '\n';
    }

    if (!std::filesystem::exists(configPath)) {
        std::cerr << "Config file not found: " << configPath << '\n';
        return 1;
    }

    if (!std::filesystem::exists(pluginDirectory)) {
        std::cerr << "Plugin directory not found: " << pluginDirectory << '\n';
        return 1;
    }
    for (int i = 3; i < argc; ++i) {
        if (!std::filesystem::exists(argv[i])) {
            std::cerr << "Additional plugin directory not found: " << argv[i] << '\n';
            return 1;
        }
    }

    try {
        graph::GraphExecutorBuilder builder;
        builder.WithJsonConfig(configPath)
            .WithPluginDirectory(pluginDirectory)
            .WithExecutorTimeout(std::chrono::seconds(15));
        for (int i = 3; i < argc; ++i) {
            builder.WithAdditionalPluginDirectory(argv[i]);
        }
        auto executor = builder.Build();
        if (!executor) {
            std::cerr << "Failed to build SAR graph executor" << '\n';
            return 1;
        }

        const auto graph_manager = executor->GetGraphManager();
        if (!graph_manager) {
            std::cerr << "Executor built without GraphManager" << '\n';
            return 1;
        }

        std::cout << "Loaded nodes: " << graph_manager->GetNodes().size() << '\n';
        std::cout << "Loaded edges: " << graph_manager->GetEdges().size() << '\n';

        const auto run_result = executor->Execute();
        if (!run_result.success) {
            std::cerr << "Execution failed: " << run_result.message;
            if (!run_result.error_details.empty()) {
                std::cerr << " | details: " << run_result.error_details;
            }
            std::cerr << '\n';
            return 1;
        }

        std::cout << "Execution completed successfully." << '\n';
        std::cout << "Completion signaled: "
                  << (executor->IsCompletionSignaled() ? "true" : "false")
                  << '\n';

        auto sink = sar::runtime::ResolveDiagnosticsSink(graph_manager);
        if (sink) {
            sink->UpdateFromGraphMetrics(graph_manager->GetMetrics());
            const auto& diagnostics = sink->last_diagnostics();
            std::cout << "Diagnostics queue_backpressure_events: "
                      << diagnostics.queue_backpressure_events << '\n';
            std::cout << "Diagnostics peak_queue_depth: "
                      << diagnostics.peak_queue_depth << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "Runtime exception: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
