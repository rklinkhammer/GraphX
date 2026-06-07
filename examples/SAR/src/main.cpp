#include "sar/SarMessages.hpp"

#include "graph/GraphExecutorBuilder.hpp"

#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>

#ifndef SAR_PLUGIN_OUTPUT_DIRECTORY
#define SAR_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

int main(int argc, char** argv) {
    [[maybe_unused]] const sar::SarMessageEnvelope contract_probe{};
    const char* defaultConfig = "examples/SAR/config/sar_stripmap_pr1.json";
    const char* configPath = (argc > 1) ? argv[1] : defaultConfig;
    const char* pluginDirectory = (argc > 2) ? argv[2] : SAR_PLUGIN_OUTPUT_DIRECTORY;

    std::cout << "GraphX SAR example runtime" << '\n';
    std::cout << "Topology config: " << configPath << '\n';
    std::cout << "Plugin directory: " << pluginDirectory << '\n';

    if (!std::filesystem::exists(configPath)) {
        std::cerr << "Config file not found: " << configPath << '\n';
        return 1;
    }

    if (!std::filesystem::exists(pluginDirectory)) {
        std::cerr << "Plugin directory not found: " << pluginDirectory << '\n';
        return 1;
    }

    try {
        auto executor = graph::GraphExecutorBuilder()
                            .WithJsonConfig(configPath)
                            .WithPluginDirectory(pluginDirectory)
                            .WithExecutorTimeout(std::chrono::seconds(5))
                            .Build();
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
    } catch (const std::exception& ex) {
        std::cerr << "Runtime exception: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
