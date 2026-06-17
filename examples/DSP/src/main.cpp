#include "dsp/SpectrumSinkNode.hpp"
#include "graph/GraphExecutorBuilder.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef DSP_PLUGIN_OUTPUT_DIRECTORY
#define DSP_PLUGIN_OUTPUT_DIRECTORY "./plugins"
#endif

#ifndef DSP_SPECTRUM_CONFIG_PATH
#define DSP_SPECTRUM_CONFIG_PATH "libdsp/config/dsp_sine_fft_spectrum_256.json"
#endif

namespace {

constexpr std::size_t kFftSize = 256;

struct CliOptions {
    std::filesystem::path config_path{DSP_SPECTRUM_CONFIG_PATH};
    std::filesystem::path plugin_directory{DSP_PLUGIN_OUTPUT_DIRECTORY};
    std::vector<std::filesystem::path> additional_plugin_directories;
    std::filesystem::path summary_json;
};

std::chrono::seconds ResolveExecutorTimeout() {
    constexpr int kDefaultTimeoutSeconds = 5;
    const char* raw = std::getenv("GRAPHX_DSP_EXECUTOR_TIMEOUT_S");
    if (raw == nullptr || *raw == '\0') {
        return std::chrono::seconds{kDefaultTimeoutSeconds};
    }

    try {
        const int value = std::stoi(raw);
        if (value <= 0) {
            return std::chrono::seconds{kDefaultTimeoutSeconds};
        }
        return std::chrono::seconds{value};
    } catch (...) {
        return std::chrono::seconds{kDefaultTimeoutSeconds};
    }
}

CliOptions ParseArgs(int argc, char** argv) {
    CliOptions options;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--summary-json") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--summary-json requires a path");
            }
            options.summary_json = argv[++i];
        } else if (arg == "--extra-plugin-dir") {
            if (i + 1 >= argc) {
                throw std::invalid_argument("--extra-plugin-dir requires a path");
            }
            options.additional_plugin_directories.emplace_back(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: graphx-dsp-spectrum-demo [config] [plugin-dir] "
                   "[--summary-json path] [--extra-plugin-dir path...]\n";
            std::exit(0);
        } else {
            positional.push_back(arg);
        }
    }

    if (!positional.empty()) {
        options.config_path = positional[0];
    }
    if (positional.size() > 1) {
        options.plugin_directory = positional[1];
    }
    if (positional.size() > 2) {
        throw std::invalid_argument("too many positional arguments");
    }

    return options;
}

std::shared_ptr<dsp::SpectrumSinkNode<float, kFftSize>> ResolveSpectrumSink(
    const std::shared_ptr<graph::GraphManager>& graph_manager) {
    if (!graph_manager) {
        return nullptr;
    }

    for (const auto& node : graph_manager->GetNodes()) {
        auto wrapper = std::dynamic_pointer_cast<graph::NodeFacadeAdapterWrapper>(node);
        if (!wrapper) {
            continue;
        }
        auto sink = wrapper->GetNode<dsp::SpectrumSinkNode<float, kFftSize>>();
        if (sink) {
            return sink;
        }
    }

    return nullptr;
}

const char* WindowTypeName(std::size_t window_type) {
    switch (window_type) {
    case 0:
        return "rectangular";
    case 1:
        return "hann";
    case 2:
        return "hamming";
    case 3:
        return "blackman";
    default:
        return "unknown";
    }
}

nlohmann::json BuildSummary(
    const dsp::SpectrumSinkNode<float, kFftSize>& sink,
    const std::filesystem::path& config_path,
    const std::filesystem::path& plugin_directory,
    bool completion_signaled) {
    const auto latest = sink.GetLatestSpectrum();
    if (!latest) {
        throw std::runtime_error("spectrum sink did not capture a spectrum frame");
    }

    const auto stats = sink.ComputeStatistics();
    return nlohmann::json{
        {"schema", "graphx.dsp.spectrum_summary.v1"},
        {"cpu_only", true},
        {"config_path", config_path.string()},
        {"plugin_directory", plugin_directory.string()},
        {"completion_signaled", completion_signaled},
        {"frame_count", sink.GetFrameCount()},
        {"peak_frequency_hz", latest->peak_frequency_hz},
        {"peak_magnitude", latest->peak_magnitude},
        {"sample_rate_hz", latest->sample_rate_hz},
        {"fft_size", kFftSize},
        {"window_type", latest->window_type},
        {"window_type_name", WindowTypeName(latest->window_type)},
        {"packet_number", latest->packet_number},
        {"num_accumulated_packets", latest->num_accumulated_packets},
        {"node_metrics", {
            {"spectrum", {
                {"frame_count", stats.frame_count},
                {"latest_peak_frequency_hz", stats.latest_peak_frequency},
                {"latest_peak_magnitude", stats.latest_peak_magnitude},
                {"avg_rms_power", stats.avg_rms_power},
                {"spectral_centroid_hz", stats.spectral_centroid},
                {"spectral_spread_hz", stats.spectral_spread}
            }}
        }}
    };
}

void WriteSummary(const std::filesystem::path& path, const nlohmann::json& summary) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output.good()) {
        throw std::runtime_error("failed to open summary path: " + path.string());
    }
    output << std::setw(2) << summary << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = ParseArgs(argc, argv);
        const auto timeout = ResolveExecutorTimeout();

        std::cout << "GraphX DSP spectrum demo runtime\n";
        std::cout << "Topology config: " << options.config_path << '\n';
        std::cout << "Plugin directory: " << options.plugin_directory << '\n';
        for (const auto& path : options.additional_plugin_directories) {
            std::cout << "Additional plugin directory: " << path << '\n';
        }
        std::cout << "Executor timeout (s): " << timeout.count() << '\n';
        std::cout << "Execution mode: CPU-only direct DFT\n";

        if (!std::filesystem::exists(options.config_path)) {
            std::cerr << "Config file not found: " << options.config_path << '\n';
            return 1;
        }
        if (!std::filesystem::exists(options.plugin_directory)) {
            std::cerr << "Plugin directory not found: " << options.plugin_directory << '\n';
            return 1;
        }
        for (const auto& path : options.additional_plugin_directories) {
            if (!std::filesystem::exists(path)) {
                std::cerr << "Additional plugin directory not found: " << path << '\n';
                return 1;
            }
        }

        graph::GraphExecutorBuilder builder;
        builder.WithJsonConfig(options.config_path.string())
            .WithPluginDirectory(options.plugin_directory.string())
            .WithExecutorTimeout(timeout);
        for (const auto& path : options.additional_plugin_directories) {
            builder.WithAdditionalPluginDirectory(path.string());
        }

        auto executor = builder.Build();
        if (!executor) {
            std::cerr << "Failed to build DSP graph executor\n";
            return 1;
        }

        const auto graph_manager = executor->GetGraphManager();
        if (!graph_manager) {
            std::cerr << "Executor built without GraphManager\n";
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

        auto sink = ResolveSpectrumSink(graph_manager);
        if (!sink) {
            std::cerr << "Failed to resolve SpectrumSinkNode<float, 256>\n";
            return 1;
        }

        const auto summary = BuildSummary(
            *sink,
            options.config_path,
            options.plugin_directory,
            executor->IsCompletionSignaled());

        std::cout << "Execution completed successfully.\n";
        std::cout << "Completion signaled: "
                  << (executor->IsCompletionSignaled() ? "true" : "false") << '\n';
        std::cout << "Spectrum frames: " << summary.at("frame_count") << '\n';
        std::cout << "Peak frequency (Hz): " << summary.at("peak_frequency_hz") << '\n';
        std::cout << "Peak magnitude: " << summary.at("peak_magnitude") << '\n';

        if (!options.summary_json.empty()) {
            WriteSummary(options.summary_json, summary);
            std::cout << "Summary JSON: " << options.summary_json << '\n';
        }
    } catch (const std::exception& ex) {
        std::cerr << "Runtime exception: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
