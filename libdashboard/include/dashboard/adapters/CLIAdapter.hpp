// MIT License
//
// Copyright (c) 2025 Robert Klinkhammer
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, and/or sell copies of
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

#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <iostream>
#include <sstream>
#include <log4cxx/logger.h>
#include <nlohmann/json.hpp>

#include "app/interfaces/IUIAdapter.hpp"
#include "app/interfaces/ICommandExecutor.hpp"
#include "app/interfaces/IMetricsPublisher.hpp"
#include "app/interfaces/DashboardCommand.hpp"
#include "app/metrics/MetricsEvent.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/MetricsCapability.hpp"
#include "app/capabilities/DashboardCapability.hpp"
#include "graph/CapabilityBus.hpp"

using json = nlohmann::json;

namespace app::adapters {

static auto cli_adapter_logger = log4cxx::Logger::getLogger("app.adapters.CLIAdapter");

/**
 * @class CLIAdapter
 * @brief Command-line interface adapter for graph execution
 *
 * CLIAdapter provides both interactive and batch mode command-line interfaces
 * to the dashboard. It implements the standardized IUIAdapter, ICommandExecutor,
 * and IMetricsPublisher interfaces, allowing CLI to coexist with Terminal and
 * Web UIs while sharing core business logic.
 *
 * Modes of Operation:
 * 1. **Interactive Mode**: Prompts user for commands, displays results and metrics
 * 2. **Batch Mode**: Reads commands from stdin or file, outputs results
 *
 * Architecture:
 * - Does not create its own UI components (no ncurses, no HTTP)
 * - Uses DashboardCapability queues for command/result passing
 * - Formatted text output suitable for terminals and logging
 * - Thread-safe metrics storage (atomic)
 * - Handles metrics events from background thread
 *
 * Thread Safety:
 * - OnMetricsEvent(): Called from MetricsPolicy thread, uses mutex
 * - FlushMetrics(): Called from CLI loop thread
 * - Execute(): Called from CLI command processor, queues via DashboardCapability
 * - IsRunning(): Atomic read, thread-safe
 *
 * Example Usage - Interactive Mode:
 * ```cpp
 * auto cli = std::make_shared<CLIAdapter>(
 *     graph_cap, metrics_cap, dashboard_cap,
 *     true,  // interactive mode
 *     "");   // no input file
 *
 * cli->Initialize(bus);
 * cli->Run();  // Blocks in interactive loop
 * cli->Shutdown();
 * ```
 *
 * Example Usage - Batch Mode:
 * ```cpp
 * auto cli = std::make_shared<CLIAdapter>(
 *     graph_cap, metrics_cap, dashboard_cap,
 *     false, // batch mode
 *     "/path/to/commands.txt");
 *
 * cli->Initialize(bus);
 * cli->Run();  // Processes file, returns when done
 * cli->Shutdown();
 * ```
 */
class CLIAdapter : public app::interfaces::IUIAdapter,
                   public app::interfaces::ICommandExecutor,
                   public app::interfaces::IMetricsPublisher,
                   public std::enable_shared_from_this<CLIAdapter> {
public:
    /**
     * @brief Construct CLI adapter with capabilities and mode configuration
     *
     * @param graph_cap Graph execution capability
     * @param metrics_cap Metrics capability for discovery and subscription
     * @param dashboard_cap Dashboard capability for command/result queues
     * @param interactive_mode If true, run interactive prompt loop
     *                         If false, read from input_file or stdin
     * @param input_file Path to file with batch commands (for batch mode)
     *                   Empty string means read from stdin
     */
    CLIAdapter(
        std::shared_ptr<app::capabilities::GraphCapability> graph_cap,
        std::shared_ptr<app::capabilities::MetricsCapability> metrics_cap,
        std::shared_ptr<app::capabilities::DashboardCapability> dashboard_cap,
        bool interactive_mode = true,
        const std::string& input_file = "")
        : graph_capability_(graph_cap),
          metrics_capability_(metrics_cap),
          dashboard_capability_(dashboard_cap),
          interactive_mode_(interactive_mode),
          input_file_(input_file),
          is_running_(false),
          latest_metrics_() {
        LOG4CXX_TRACE(cli_adapter_logger, 
            "CLIAdapter constructed in " << (interactive_mode ? "interactive" : "batch") << " mode");
    }

    /**
     * @brief Destructor: cleanup resources
     */
    ~CLIAdapter() {
        LOG4CXX_TRACE(cli_adapter_logger, "CLIAdapter destructed");
        Shutdown();
    }

    /**
     * @brief Load graph configuration from JSON file
     *
     * Implementation of IUIAdapter::LoadGraphConfig()
     *
     * @param config_path Path to JSON graph topology file
     * @throws std::runtime_error if file cannot be opened
     * @throws std::invalid_argument if JSON is invalid
     */
    void LoadGraphConfig(const std::string& config_path) override {
        LOG4CXX_DEBUG(cli_adapter_logger, "Loading graph config: " << config_path);
        std::ifstream file(config_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open graph config file: " + config_path);
        }
        try {
            json config = json::parse(file);
            if (!config.contains("nodes") || !config.contains("edges")) {
                throw std::invalid_argument("Graph config missing 'nodes' or 'edges'");
            }
            config_path_ = config_path;
            LOG4CXX_INFO(cli_adapter_logger, "Graph config loaded: " << config_path);
        } catch (const json::exception& e) {
            throw std::invalid_argument(std::string("Invalid graph JSON: ") + e.what());
        }
    }

    /**
     * @brief Get loaded graph configuration path
     *
     * Implementation of IUIAdapter::GetGraphConfigPath()
     *
     * @return Path to loaded config, or empty string if none
     */
    std::string GetGraphConfigPath() const override {
        return config_path_;
    }

    /**
     * @brief Initialize adapter with capability bus
     *
     * Implementation of IUIAdapter::Initialize()
     *
     * @param bus CapabilityBus for service registration
     * @throws std::runtime_error if initialization fails
     *
     * Thread: Main thread at startup
     * Called once, before entering command loop
     */
    void Initialize(std::shared_ptr<graph::CapabilityBus> bus) override {
        LOG4CXX_TRACE(cli_adapter_logger, "CLIAdapter Initialize called");

        capability_bus_ = bus;

        // Register self as ICommandExecutor
        try {
            bus->Register<app::interfaces::ICommandExecutor>(
                std::static_pointer_cast<app::interfaces::ICommandExecutor>(shared_from_this()));
            LOG4CXX_DEBUG(cli_adapter_logger, "Registered as ICommandExecutor");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(cli_adapter_logger, "Failed to register ICommandExecutor: " << e.what());
            throw;
        }

        // Register self as IMetricsPublisher
        try {
            bus->Register<app::interfaces::IMetricsPublisher>(
                std::static_pointer_cast<app::interfaces::IMetricsPublisher>(shared_from_this()));
            LOG4CXX_DEBUG(cli_adapter_logger, "Registered as IMetricsPublisher");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(cli_adapter_logger, "Failed to register IMetricsPublisher: " << e.what());
            throw;
        }

        // Metrics subscriptions are handled via IMetricsPublisher interface
        // OnMetricsEvent() will be called automatically by the metrics system

        is_running_ = true;
        LOG4CXX_INFO(cli_adapter_logger, "CLIAdapter initialized successfully");
    }

    /**
     * @brief Check if adapter is currently running
     *
     * Implementation of IUIAdapter::IsRunning()
     *
     * @return true if initialized and running, false otherwise
     *
     * Thread: Any thread (atomic read)
     */
    bool IsRunning() const override {
        return is_running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Graceful shutdown of the adapter
     *
     * Implementation of IUIAdapter::Shutdown()
     *
     * Thread: May be called from signal handler or main thread
     * Idempotent: Safe to call multiple times
     */
    void Shutdown() override {
        LOG4CXX_TRACE(cli_adapter_logger, "CLIAdapter Shutdown called");

        // Idempotent check
        if (!is_running_.exchange(false, std::memory_order_release)) {
            LOG4CXX_TRACE(cli_adapter_logger, "Already shut down");
            return;
        }

        // Metrics unsubscription handled by metrics system

        LOG4CXX_INFO(cli_adapter_logger, "CLIAdapter shutdown complete");
    }

    /**
     * @brief Get human-readable adapter name for logging
     *
     * Implementation of IUIAdapter::GetAdapterName()
     *
     * @return "CLIAdapter"
     */
    std::string GetAdapterName() const override {
        return "CLIAdapter";
    }

    /**
     * @brief Execute a dashboard command
     *
     * Implementation of ICommandExecutor::Execute()
     *
     * Queues the command for execution via DashboardCapability.
     * The command will be processed asynchronously by the command handler.
     *
     * @param cmd DashboardCommand with name, args, context_id
     * @return CommandResult with Success status if queued
     *
     * Thread: CLI command processor thread
     */
    app::interfaces::CommandResult Execute(const app::interfaces::DashboardCommand& cmd) override {
        LOG4CXX_TRACE(cli_adapter_logger, "Execute command: " << cmd.name);

        if (!dashboard_capability_) {
            return app::interfaces::CommandResult(
                app::interfaces::CommandStatus::ExecutionFailed,
                "DashboardCapability not available");
        }

        // Build command string: "name arg1 arg2 ..."
        std::string command_str = cmd.name;
        for (const auto& arg : cmd.args) {
            command_str += " " + arg;
        }

        // Queue the command
        if (!dashboard_capability_->EnqueueCommand(command_str)) {
            return app::interfaces::CommandResult(
                app::interfaces::CommandStatus::ExecutionFailed,
                "Command queue full");
        }

        return app::interfaces::CommandResult(
            app::interfaces::CommandStatus::Success,
            "Command queued for execution");
    }

    /**
     * @brief Receive metrics event from graph execution
     *
     * Implementation of IMetricsPublisher::OnMetricsEvent()
     *
     * Called from MetricsCapability when new metrics arrive.
     * Stores the event for the UI thread to process.
     *
     * Performance Requirements: MUST complete in <1ms
     *
     * Thread: MetricsPolicy background thread
     *
     * @param event Metrics event with source, event_type, timestamp, data
     */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event) override {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        latest_metrics_ = event;
    }

    /**
     * @brief Flush buffered metrics to output
     *
     * Implementation of IMetricsPublisher::FlushMetrics()
     *
     * Called by CLI loop to display updated metrics.
     * In interactive mode, updates the live display.
     * In batch mode, outputs metrics when requested.
     *
     * Performance Requirements: Should complete in <50ms
     *
     * Thread: CLI command loop thread
     */
    void FlushMetrics() override {
        // Metrics are displayed via GetLatestMetrics() when needed
        // This is a no-op for CLI since we pull metrics on-demand
    }

    /**
     * @brief Get latest metrics snapshot
     *
     * Implementation of IMetricsPublisher::GetLatestMetrics()
     *
     * Returns the most recent MetricsEvent.
     * Used by CLI output routines to format and display metrics.
     *
     * @return Copy of latest MetricsEvent
     *
     * Thread: Any thread (must be thread-safe)
     */
    app::metrics::MetricsEvent GetLatestMetrics() const override {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return latest_metrics_;
    }

    /**
     * @brief Run the CLI loop (interactive or batch)
     *
     * Blocking call that runs the appropriate CLI mode:
     * - Interactive: Displays prompt and processes user input from stdin
     * - Batch: Reads commands from input_file (or stdin if empty) and executes them
     *
     * This should be called from the main thread after Initialize().
     * Returns when user quits (interactive) or input exhausted (batch).
     *
     * @note This is the main event loop. Shutdown() can be called from another
     *       thread (e.g., signal handler) to gracefully exit this loop.
     *
     * Thread: Main/UI thread
     * Blocking: Yes, until user quits or input exhausted
     */
    void Run() {
        LOG4CXX_INFO(cli_adapter_logger, 
            "CLI loop starting in " << (interactive_mode_ ? "interactive" : "batch") << " mode");

        if (interactive_mode_) {
            RunInteractive();
        } else {
            RunBatch();
        }

        LOG4CXX_INFO(cli_adapter_logger, "CLI loop exiting");
    }

private:
    /**
     * @brief Run interactive mode: prompt user for commands
     */
    void RunInteractive() {
        if (!is_running_) return;

        std::cout << "\n=== Dashboard CLI - Interactive Mode ===\n";
        std::cout << "Type 'help' for available commands, 'quit' to exit.\n\n";

        while (is_running_) {
            std::cout << "> ";
            std::cout.flush();

            std::string input_line;
            if (!std::getline(std::cin, input_line)) {
                break;  // EOF on stdin
            }

            // Trim whitespace
            input_line.erase(0, input_line.find_first_not_of(" \t\r\n"));
            input_line.erase(input_line.find_last_not_of(" \t\r\n") + 1);

            if (input_line.empty()) {
                continue;
            }

            if (input_line == "quit" || input_line == "exit" || input_line == "q") {
                std::cout << "Exiting...\n";
                is_running_ = false;
                break;
            }

            ProcessCommand(input_line);
        }
    }

    /**
     * @brief Run batch mode: read commands from file or stdin
     */
    void RunBatch() {
        if (!is_running_) return;

        std::istream* input = &std::cin;
        std::unique_ptr<std::ifstream> file_stream;

        if (!input_file_.empty()) {
            file_stream = std::make_unique<std::ifstream>(input_file_);
            if (!file_stream->is_open()) {
                LOG4CXX_ERROR(cli_adapter_logger, "Cannot open input file: " << input_file_);
                return;
            }
            input = file_stream.get();
        }

        std::string line;
        while (is_running_ && std::getline(*input, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            if (line.empty() || line[0] == '#') {  // Skip empty lines and comments
                continue;
            }

            ProcessCommand(line);
        }
    }

    /**
     * @brief Parse and execute a single command
     */
    void ProcessCommand(const std::string& command_line) {
        // Parse command: "name arg1 arg2 ..."
        std::istringstream iss(command_line);
        std::string name;
        iss >> name;

        // Normalize to lowercase
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        // Collect arguments
        std::vector<std::string> args;
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }

        // Handle local CLI commands first
        if (name == "help") {
            PrintHelp(args.empty() ? "" : args[0]);
            return;
        }

        if (name == "metrics") {
            PrintMetrics();
            return;
        }

        if (name == "status") {
            PrintStatus();
            return;
        }

        // Queue command for execution via ICommandExecutor
        app::interfaces::DashboardCommand cmd(name, args, "cli");
        auto result = Execute(cmd);

        // Display result
        std::cout << "[" << result.GetStatusString() << "] " << result.message << "\n";
        if (!result.data.empty()) {
            std::cout << result.data << "\n";
        }
    }

    /**
     * @brief Print help information
     */
    void PrintHelp(const std::string& command = "") {
        if (command.empty()) {
            std::cout << "\n=== Available Commands ===\n";
            std::cout << "  pause               - Pause graph execution\n";
            std::cout << "  resume              - Resume graph execution\n";
            std::cout << "  stop                - Stop graph execution\n";
            std::cout << "  status              - Show execution status\n";
            std::cout << "  metrics             - Display latest metrics\n";
            std::cout << "  help [command]      - Show this help or help for specific command\n";
            std::cout << "  quit/exit           - Exit CLI\n";
            std::cout << "\nType 'help <command>' for more information.\n";
        } else {
            std::cout << "\nHelp for '" << command << "':\n";
            if (command == "pause") {
                std::cout << "  Pause graph execution\n";
                std::cout << "  Usage: pause\n";
            } else if (command == "resume") {
                std::cout << "  Resume graph execution\n";
                std::cout << "  Usage: resume\n";
            } else if (command == "stop") {
                std::cout << "  Stop graph execution\n";
                std::cout << "  Usage: stop\n";
            } else if (command == "metrics") {
                std::cout << "  Display latest metrics\n";
                std::cout << "  Usage: metrics\n";
            } else {
                std::cout << "  No help available for '" << command << "'\n";
            }
        }
    }

    /**
     * @brief Print current status
     */
    void PrintStatus() {
        std::cout << "\n=== Graph Execution Status ===\n";
        std::cout << "CLI Adapter: " << (is_running_ ? "Running" : "Stopped") << "\n";
        std::cout << "Mode: " << (interactive_mode_ ? "Interactive" : "Batch") << "\n";
        if (!config_path_.empty()) {
            std::cout << "Graph Config: " << config_path_ << "\n";
        }
        std::cout << "\n";
    }

    /**
     * @brief Print current metrics
     */
    void PrintMetrics() {
        auto metrics = GetLatestMetrics();
        std::cout << "\n=== Latest Metrics ===\n";
        auto time_t_value = std::chrono::system_clock::to_time_t(metrics.timestamp);
        std::cout << "Timestamp: " << time_t_value << "\n";
        std::cout << "Source: " << metrics.source << "\n";
        std::cout << "Event Type: " << metrics.event_type << "\n";
        if (!metrics.data.empty()) {
            std::cout << "Data:\n";
            for (const auto& [key, value] : metrics.data) {
                std::cout << "  " << key << ": " << value << "\n";
            }
        }
        std::cout << "\n";
    }

    // Capabilities
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability_;
    std::shared_ptr<app::capabilities::MetricsCapability> metrics_capability_;
    std::shared_ptr<app::capabilities::DashboardCapability> dashboard_capability_;
    std::shared_ptr<graph::CapabilityBus> capability_bus_;

    // Configuration
    bool interactive_mode_;
    std::string input_file_;
    std::string config_path_;
    std::atomic<bool> is_running_;

    // Metrics storage
    mutable std::mutex metrics_mutex_;
    app::metrics::MetricsEvent latest_metrics_;
};

}  // namespace app::adapters
