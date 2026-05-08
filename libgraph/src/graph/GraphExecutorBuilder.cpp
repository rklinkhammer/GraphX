// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

/**
 * @file GraphExecutorBuilder.cpp
 * @brief Implementation of GraphExecutorBuilder
 */

#include "graph/GraphExecutorBuilder.hpp"
#include "graph/GraphExecutor.hpp"
#include "app/policies/CompletionPolicy.hpp"
#include "app/policies/DataInjectionPolicy.hpp"
#include "app/policies/MetricsPolicy.hpp"
#include "app/policies/DashboardPolicy.hpp"
#include "app/policies/CSVInjectionPolicy.hpp"
#include "app/policies/CommandPolicy.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/FactoryManager.hpp"
#include "app/GraphBuilder.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include <log4cxx/logger.h>
#include <filesystem>
#include <stdexcept>

namespace graph {

static log4cxx::LoggerPtr g_logger = log4cxx::Logger::getLogger("graph.GraphExecutorBuilder");

GraphExecutorBuilder::GraphExecutorBuilder()
    : json_config_(""),
      graph_manager_(nullptr),
      plugin_directory_(""),
      csv_inputs_(),
      csv_injection_rate_ms_(10),
      executor_timeout_(std::chrono::seconds(30)),
      graph_threads_(4),
      verbose_logging_(false),
      already_built_(false),
      cli_mode_(false) {
    LOG4CXX_TRACE(g_logger, "GraphExecutorBuilder constructed with defaults");
}

GraphExecutorBuilder::~GraphExecutorBuilder() {
    LOG4CXX_TRACE(g_logger, "GraphExecutorBuilder destroyed");
}

GraphExecutorBuilder& GraphExecutorBuilder::WithJsonConfig(const std::string& path) {
    if (path.empty()) {
        throw std::invalid_argument("GraphExecutorBuilder: JSON config path cannot be empty");
    }
    json_config_ = path;
    LOG4CXX_TRACE(g_logger, "Set JSON config path: " << path);
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithGraphManager(std::shared_ptr<graph::GraphManager> graph_manager) {
    if (!graph_manager) {
        throw std::invalid_argument("GraphExecutorBuilder: GraphManager cannot be null");
    }
    graph_manager_ = graph_manager;
    LOG4CXX_TRACE(g_logger, "Set shared GraphManager instance");
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithPluginDirectory(const std::string& directory) {
    if (directory.empty()) {
        throw std::invalid_argument("GraphExecutorBuilder: Plugin directory cannot be empty");
    }
    plugin_directory_ = directory;
    LOG4CXX_TRACE(g_logger, "Set plugin directory: " << directory);
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithCSVInput(const std::string& path, const std::string& node) {
    if (path.empty()) {
        throw std::invalid_argument("GraphExecutorBuilder: CSV path cannot be empty");
    }
    csv_inputs_.push_back({path, node});
    LOG4CXX_TRACE(g_logger, "Added CSV input: path=" << path << ", node=" << (node.empty() ? "(auto)" : node));
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithCSVInputs(const std::vector<std::pair<std::string, std::string>>& inputs) {
    csv_inputs_ = inputs;
    LOG4CXX_TRACE(g_logger, "Set CSV inputs: " << inputs.size() << " files");
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithCSVInjectionRate(uint32_t rate_ms) {
    if (rate_ms == 0) {
        throw std::invalid_argument("GraphExecutorBuilder: CSV injection rate must be > 0");
    }
    csv_injection_rate_ms_ = rate_ms;
    LOG4CXX_TRACE(g_logger, "Set CSV injection rate: " << rate_ms << "ms");
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithExecutorTimeout(const std::chrono::seconds& timeout) {
    if (timeout.count() <= 0) {
        throw std::invalid_argument("GraphExecutorBuilder: Executor timeout must be > 0 seconds");
    }
    executor_timeout_ = timeout;
    LOG4CXX_TRACE(g_logger, "Set executor timeout: " << timeout.count() << "s");
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithGraphThreads(size_t count) {
    if (count == 0) {
        throw std::invalid_argument("GraphExecutorBuilder: Graph threads must be > 0");
    }
    graph_threads_ = count;
    LOG4CXX_TRACE(g_logger, "Set graph threads: " << count);
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithCliMode(bool enabled) {
    cli_mode_ = enabled;
    LOG4CXX_TRACE(g_logger, "Set CLI mode: " << (enabled ? "enabled" : "disabled"));
    return *this;
}

GraphExecutorBuilder& GraphExecutorBuilder::WithVerboseLogging(bool enabled) {
    verbose_logging_ = enabled;
    LOG4CXX_TRACE(g_logger, "Set verbose logging: " << (enabled ? "enabled" : "disabled"));
    return *this;
}

std::string GraphExecutorBuilder::GetDefaultPluginDirectory() {
    // Strategy 1: Relative to executable directory
    // Note: /proc/self/exe works on Linux; macOS uses different approach
    #ifdef __APPLE__
        // macOS: use current working directory as base
        auto cwd = std::filesystem::current_path();
        auto default_dir = cwd / "plugins";
        if (std::filesystem::exists(default_dir)) {
            return default_dir.string();
        }
    #else
        // Linux: try relative to executable
        try {
            auto exe_path = std::filesystem::canonical("/proc/self/exe").parent_path();
            auto default_dir = exe_path / "plugins";
            if (std::filesystem::exists(default_dir)) {
                return default_dir.string();
            }
        } catch (...) {
            // Fall through to next strategy
        }
    #endif
    
    // Strategy 2: Relative to current working directory
    auto current_dir = std::filesystem::current_path() / "plugins";
    if (std::filesystem::exists(current_dir)) {
        return current_dir.string();
    }

    auto build_dir = std::filesystem::path(BUILD_DIR) / "plugins";
    if (std::filesystem::exists(build_dir)) {
        return build_dir.string();;
    }

    // Strategy 3: Return relative path (will be validated at Build time)
    return "./plugins";
}

void GraphExecutorBuilder::ValidateConfiguration() {
    LOG4CXX_TRACE(g_logger, "Validating GraphExecutorBuilder configuration");
    if(graph_manager_ && csv_inputs_.empty()) {
        LOG4CXX_TRACE(g_logger, "Using provided GraphManager without CSV inputs");
        return;
    }
    // Check required: JSON config
    if (json_config_.empty()) {
        throw std::invalid_argument(
            "GraphExecutorBuilder: WithJsonConfig() is required - no JSON config path set");
    }

    // Check JSON config file exists
    if (!std::filesystem::exists(json_config_)) {
        throw std::runtime_error(
            "GraphExecutorBuilder: JSON config file not found: " + json_config_);
    }

    // If plugin directory not set, use default
    if (plugin_directory_.empty()) {
        plugin_directory_ = GetDefaultPluginDirectory();
        LOG4CXX_TRACE(g_logger, "Using default plugin directory: " << plugin_directory_);
    }

    // Check plugin directory exists
    if (!std::filesystem::exists(plugin_directory_)) {
        throw std::runtime_error(
            "GraphExecutorBuilder: Plugin directory not found: " + plugin_directory_ +
            ". Set explicitly with WithPluginDirectory()");
    }

    LOG4CXX_TRACE(g_logger, "Configuration validation passed");
}

std::shared_ptr<GraphExecutor> GraphExecutorBuilder::Build() {
    LOG4CXX_TRACE(g_logger, "GraphExecutorBuilder::Build() starting");

    // Check for multiple Build() calls
    if (already_built_) {
        throw std::logic_error(
            "GraphExecutorBuilder: Build() already called. "
            "Create a new builder instance to build another executor.");
    }
    already_built_ = true;

    // Validate configuration
    ValidateConfiguration();

    LOG4CXX_TRACE(g_logger, "Building executor with:");
    LOG4CXX_TRACE(g_logger, "  json_config: " << json_config_);
    LOG4CXX_TRACE(g_logger, "  plugin_directory: " << plugin_directory_);
    LOG4CXX_TRACE(g_logger, "  csv_inputs: " << csv_inputs_.size());
    LOG4CXX_TRACE(g_logger, "  csv_injection_rate_ms: " << csv_injection_rate_ms_);
    LOG4CXX_TRACE(g_logger, "  executor_timeout: " << executor_timeout_.count() << "s");
    LOG4CXX_TRACE(g_logger, "  graph_threads: " << graph_threads_);
    LOG4CXX_TRACE(g_logger, "  verbose_logging: " << (verbose_logging_ ? "enabled" : "disabled"));

    try {
         // Step 2: Create FactoryManager and load plugins
        LOG4CXX_TRACE(g_logger, "Step 2: Creating factory and loading plugins");
        auto [factory, loader] = app::FactoryManager::CreateFactory(plugin_directory_);
        LOG4CXX_TRACE(g_logger, "Factory created and plugins loaded from: " << plugin_directory_);

        // Step 3: Create AppContext with loaded configuration
        LOG4CXX_TRACE(g_logger, "Step 3: Creating AppContext");
        std::shared_ptr<app::capabilities::GraphCapability> graph_cap =
            std::make_shared<app::capabilities::GraphCapability>();

        graph_cap->SetNodeFactory(factory);
        graph_cap->SetPluginLoader(loader);
        graph_cap->SetPluginRegistry(factory->GetPluginRegistry());
        graph_cap->SetJsonConfigPath(json_config_);
        graph_cap->SetCliMode(cli_mode_);

        LOG4CXX_TRACE(g_logger, "AppContext created");

        if (graph_manager_) {
            // Step 4a: Build graph via GraphBuilder
            graph_cap->SetGraphManager(graph_manager_);
            LOG4CXX_TRACE(g_logger, "Using provided shared GraphManager instance");
        } else {
            // Step 4b: Build graph via GraphBuilder
            LOG4CXX_TRACE(g_logger, "Step 4: Building graph");
            auto graph_builder = std::make_shared<app::GraphBuilder>(graph_cap);
            auto build_result = graph_builder->Build();
    
            if (!build_result.success) {
                throw std::runtime_error(
                    "GraphExecutorBuilder: Graph building failed: " + build_result.error_message);
            }
            LOG4CXX_TRACE(g_logger, "GraphManager built successfully with "
                         << build_result.node_count << " nodes and "
                         << build_result.edge_count << " edges");
            graph_cap->SetNodeNames(build_result.node_names);
            graph_cap->SetEdgeDescriptions(build_result.edge_descriptions);
            graph_cap->SetGraphManager(build_result.graph);

            LOG4CXX_TRACE(g_logger, "Graph built successfully");
        }

        // Step 5: Return appropriate executor type based on CSV configuration
        LOG4CXX_TRACE(g_logger, "Step 5: Creating executor");

        auto completion_policy = std::make_unique<app::policies::CompletionPolicy>();
        completion_policy->SetMaxDuration(std::chrono::duration_cast<std::chrono::milliseconds>(executor_timeout_));
        completion_policy->SetCliMode(cli_mode_);
        auto injection_policy = std::make_unique<app::policies::DataInjectionPolicy>();  
        auto metrics_policy = std::make_unique<app::policies::MetricsPolicy>();
        auto dashboard_policy = std::make_unique<app::policies::DashboardPolicy>();

        auto chain = std::make_unique<graph::ExecutionPolicyChain>(
            std::move(completion_policy), std::make_unique<graph::ExecutionPolicyChain>(
                std::move(injection_policy), std::make_unique<graph::ExecutionPolicyChain>(
                    std::move(metrics_policy), std::make_unique<graph::ExecutionPolicyChain>(
                        std::move(dashboard_policy), nullptr))));
       
        if (!csv_inputs_.empty()) {
            // CSV: return CSVGraphExecutor with configuration
            LOG4CXX_TRACE(g_logger, "Creating GraphExecutor (" << csv_inputs_.size() 
                         << " CSV inputs, rate=" << csv_injection_rate_ms_ << "ms)");
            auto csv_injection_policy = std::make_unique<app::policies::CSVInjectionPolicy>();
            
            // Configure CSV injection - extract paths from pairs
              // csv_injection_policy->SetCSVInjectionRate(csv_injection_rate_ms_);
            csv_injection_policy->SetCSVInputPaths(csv_inputs_);

            chain->AppendNext(std::make_unique<graph::ExecutionPolicyChain>(std::move(csv_injection_policy), nullptr));
            LOG4CXX_TRACE(g_logger, "GraphExecutor configured successfully with CSVDataInjectionPolicy");         
        }
  
        if(cli_mode_) {
            LOG4CXX_TRACE(g_logger, "CLI mode enabled - using CommandPolicy");
            auto command_policy = std::make_unique<app::policies::CommandPolicy>();
            chain->AppendNext(std::make_unique<graph::ExecutionPolicyChain>(std::move(command_policy), 
                nullptr));
        }
        auto executor = std::make_shared<GraphExecutor>(std::move(chain), graph_cap);
        return executor;

    } catch (const std::exception& e) {
        LOG4CXX_ERROR(g_logger, "GraphExecutorBuilder::Build() failed: " << e.what());
        throw;
    }
}

}  // namespace graph
