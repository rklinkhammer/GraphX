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

#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <fstream>
#include <log4cxx/logger.h>
#include <nlohmann/json.hpp>

#include "dashboard/interfaces/IUIAdapter.hpp"
#include "dashboard/interfaces/ICommandExecutor.hpp"
#include "dashboard/interfaces/IMetricsPublisher.hpp"
#include "dashboard/interfaces/DashboardCommand.hpp"
#include "dashboard/metrics/MetricsEvent.hpp"
#include "ui/Dashboard.hpp"
#include "ui/CommandRegistry.hpp"

using json = nlohmann::json;

namespace app::adapters {

static auto terminal_adapter_logger = log4cxx::Logger::getLogger("app.adapters.TerminalUIAdapter");

/**
 * @class TerminalUIAdapter
 * @brief ncurses-based UI adapter implementing standardized interfaces
 *
 * TerminalUIAdapter wraps the existing Dashboard (ncurses-based) and exposes
 * it through the standardized IUIAdapter, ICommandExecutor, and IMetricsPublisher
 * interfaces. This allows the terminal UI to coexist with web and CLI adapters
 * while sharing command/metrics infrastructure.
 *
 * Architecture:
 * - **Ownership**: Owns Dashboard instance and manages its lifecycle
 * - **Threading**: Dashboard runs in main thread, metrics updates from background threads
 * - **Command Routing**: DashboardCommand -> CommandRegistry -> Dashboard UI
 * - **Metrics Flow**: OnMetricsEvent() (fast, <1ms) -> FlusMetrics() (50ms) -> Display
 *
 * Lifecycle:
 * 1. Constructor creates Dashboard but doesn't initialize ncurses
 * 2. Initialize() registers self with CapabilityBus and starts UI event loop
 * 3. IsRunning() reports dashboard status
 * 4. Shutdown() stops event loop and cleans up ncurses
 *
 * Thread Safety:
 * - OnMetricsEvent(): Called from MetricsPolicy thread, uses atomic storage (<1ms)
 * - FlushMetrics(): Called from UI thread (Dashboard main loop, ~50ms)
 * - Execute(): Called from command sources, delegates to CommandRegistry
 * - Dashboard.Run() blocks in main/UI thread for event loop
 *
 * Example Usage:
 * ```cpp
 * auto adapter = std::make_shared<TerminalUIAdapter>(
 *     graph_capability,
 *     metrics_capability,
 *     dashboard_capability);
 *
 * adapter->Initialize(bus);  // Register and start UI
 *
 * // Main loop can now:
 * // - Call adapter->IsRunning() to check status
 * // - Receive metrics via metrics subscription
 * // - Execute commands via bus->Get<ICommandExecutor>()->Execute(cmd)
 *
 * adapter->Shutdown();  // Cleanup when done
 * ```
 *
 * @see IUIAdapter, ICommandExecutor, IMetricsPublisher, Dashboard
 */
class TerminalUIAdapter : public app::interfaces::IUIAdapter,
                          public app::interfaces::ICommandExecutor,
                          public app::interfaces::IMetricsPublisher,
                          public std::enable_shared_from_this<TerminalUIAdapter> {
public:
    /**
     * @brief Construct terminal UI adapter with capability references
     *
     * Creates the Dashboard instance but does not initialize ncurses or
     * start the event loop. Call Initialize() to complete setup.
     *
     * @param graph_cap Graph execution capability
     * @param metrics_cap Metrics capability for discovery and subscription
     * @param dashboard_cap Dashboard capability for command/log queues
     */
    TerminalUIAdapter(
        std::shared_ptr<app::capabilities::GraphCapability> graph_cap,
        std::shared_ptr<app::capabilities::MetricsCapability> metrics_cap,
        std::shared_ptr<app::capabilities::DashboardCapability> dashboard_cap)
        : graph_capability_(graph_cap),
          metrics_capability_(metrics_cap),
          dashboard_capability_(dashboard_cap),
          is_running_(false),
          latest_metrics_() {
        LOG4CXX_TRACE(terminal_adapter_logger, "TerminalUIAdapter constructed");
    }

    /**
     * @brief Destructor: cleanup any remaining resources
     */
    ~TerminalUIAdapter() {
        LOG4CXX_TRACE(terminal_adapter_logger, "TerminalUIAdapter destructed");
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
        LOG4CXX_DEBUG(terminal_adapter_logger, "Loading graph config: " << config_path);
        // Validate file exists
        std::ifstream file(config_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open graph config file: " + config_path);
        }
        // Parse and validate JSON
        try {
            json config = json::parse(file);
            if (!config.contains("nodes") || !config.contains("edges")) {
                throw std::invalid_argument("Graph config missing 'nodes' or 'edges'");
            }
            config_path_ = config_path;
            LOG4CXX_INFO(terminal_adapter_logger, "Graph config loaded: " << config_path);
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
     * @brief Initialize adapter: register with bus and start UI event loop
     *
     * Implementation of IUIAdapter::Initialize()
     *
     * Responsibilities:
     * 1. Create and initialize Dashboard instance
     * 2. Register self as ICommandExecutor in CapabilityBus
     * 3. Register self as IMetricsPublisher in CapabilityBus
     * 4. Subscribe to MetricsCapability for metrics events
     * 5. Create command registry with built-in commands
     * 6. Start UI thread running Dashboard::Run()
     * 7. Set is_running_ = true
     *
     * @param bus CapabilityBus for service registration
     * @throws std::runtime_error if initialization fails
     *
     * Thread: Main thread at startup
     * Called once, before entering event loop
     */
    void Initialize(std::shared_ptr<graph::CapabilityBus> bus) override {
        LOG4CXX_TRACE(terminal_adapter_logger, "TerminalUIAdapter Initialize called");

        capability_bus_ = bus;

        // Create Dashboard instance (ncurses not initialized yet)
        dashboard_ = std::make_shared<Dashboard>(
            graph_capability_,
            metrics_capability_,
            dashboard_capability_);

        // Initialize Dashboard (sets up ncurses, discovers metrics, etc.)
        if (!dashboard_->Initialize()) {
            LOG4CXX_ERROR(terminal_adapter_logger, "Failed to initialize Dashboard");
            throw std::runtime_error("Dashboard initialization failed");
        }

        // Register self as ICommandExecutor
        try {
            bus->Register<app::interfaces::ICommandExecutor>(
                std::static_pointer_cast<app::interfaces::ICommandExecutor>(shared_from_this()));
            LOG4CXX_DEBUG(terminal_adapter_logger, "Registered as ICommandExecutor");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(terminal_adapter_logger, "Failed to register ICommandExecutor: " << e.what());
            dashboard_->Cleanup();
            throw;
        }

        // Register self as IMetricsPublisher
        try {
            bus->Register<app::interfaces::IMetricsPublisher>(
                std::static_pointer_cast<app::interfaces::IMetricsPublisher>(shared_from_this()));
            LOG4CXX_DEBUG(terminal_adapter_logger, "Registered as IMetricsPublisher");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(terminal_adapter_logger, "Failed to register IMetricsPublisher: " << e.what());
            dashboard_->Cleanup();
            throw;
        }

        // Subscribe to metrics events
        try {
            auto metrics_cap = bus->Get<app::capabilities::MetricsCapability>();
            if (metrics_cap) {
                // Dashboard implements IMetricsSubscriber, register it to receive metrics
                metrics_cap->RegisterMetricsCallback(dashboard_.get());
                LOG4CXX_DEBUG(terminal_adapter_logger, "Registered Dashboard as metrics subscriber");
            }
        } catch (const std::exception& e) {
            LOG4CXX_WARN(terminal_adapter_logger, "Failed to subscribe to metrics: " << e.what());
            // Non-fatal, continue without metrics subscription
        }

        // Mark as running before starting event loop
        is_running_ = true;

        // Start Dashboard event loop in separate thread
        ui_thread_ = std::thread([this]() {
            LOG4CXX_DEBUG(terminal_adapter_logger, "UI thread started, running Dashboard");
            try {
                dashboard_->Run();  // Blocks until user quits
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(terminal_adapter_logger, "Dashboard::Run() threw exception: " << e.what());
            }
            is_running_ = false;
            LOG4CXX_DEBUG(terminal_adapter_logger, "UI thread exiting");
        });

        LOG4CXX_INFO(terminal_adapter_logger, "TerminalUIAdapter initialized successfully");
    }

    /**
     * @brief Check if adapter is currently running
     *
     * Implementation of IUIAdapter::IsRunning()
     *
     * @return true if Dashboard is running, false if shutdown/failed
     *
     * Thread: Any thread (atomic read)
     * Called frequently from main loop
     */
    bool IsRunning() const override {
        return is_running_.load(std::memory_order_acquire);
    }

    /**
     * @brief Graceful shutdown of the adapter
     *
     * Implementation of IUIAdapter::Shutdown()
     *
     * Responsibilities:
     * 1. Set is_running_ = false (signals UI thread to exit)
     * 2. Wait for UI thread to finish (dashboard_->Run() returns)
     * 3. Clean up Dashboard ncurses resources
     * 4. Unsubscribe from metrics
     * 5. Idempotent (safe to call multiple times)
     *
     * Thread: May be called from signal handler or main thread
     * Idempotent: Safe to call multiple times
     */
    void Shutdown() override {
        LOG4CXX_TRACE(terminal_adapter_logger, "TerminalUIAdapter Shutdown called");

        // Idempotent check
        if (!is_running_.exchange(false, std::memory_order_release)) {
            LOG4CXX_TRACE(terminal_adapter_logger, "Already shut down");
            return;
        }

        // Signal Dashboard to stop (if it's running)
        if (dashboard_) {
            dashboard_->Cleanup();
        }

        // Wait for UI thread to finish
        if (ui_thread_.joinable()) {
            LOG4CXX_DEBUG(terminal_adapter_logger, "Waiting for UI thread to finish");
            ui_thread_.join();
            LOG4CXX_DEBUG(terminal_adapter_logger, "UI thread finished");
        }

        // Unsubscribe from metrics if available
        try {
            if (capability_bus_) {
                auto metrics_cap = capability_bus_->Get<app::capabilities::MetricsCapability>();
                if (metrics_cap) {
                    metrics_cap->UnregisterMetricsCallback(dashboard_.get());
                    LOG4CXX_DEBUG(terminal_adapter_logger, "Unregistered Dashboard as metrics subscriber");
                }
            }
        } catch (const std::exception& e) {
            LOG4CXX_WARN(terminal_adapter_logger, "Error unsubscribing from metrics: " << e.what());
        }

        LOG4CXX_INFO(terminal_adapter_logger, "TerminalUIAdapter shutdown complete");
    }

    /**
     * @brief Get human-readable adapter name for logging
     *
     * Implementation of IUIAdapter::GetAdapterName()
     *
     * @return "TerminalUIAdapter"
     */
    std::string GetAdapterName() const override {
        return "TerminalUIAdapter";
    }

    /**
     * @brief Execute a dashboard command
     *
     * Implementation of ICommandExecutor::Execute()
     *
     * Converts DashboardCommand to string format and queues via DashboardCapability.
     * The Dashboard process (if running) dequeues and executes the command later.
     *
     * This is non-blocking: commands are queued and executed asynchronously by
     * the Dashboard event loop.
     *
     * Thread Safety:
     * - May be called from multiple sources concurrently
     * - DashboardCapability::EnqueueCommand() is thread-safe
     *
     * Performance: Should complete in <10ms (just queue operation)
     *
     * @param cmd DashboardCommand with name, args, context_id
     * @return CommandResult with Success status (command queued)
     *
     * @see ICommandExecutor for supported commands
     */
    app::interfaces::CommandResult Execute(const app::interfaces::DashboardCommand& cmd) override {
        LOG4CXX_TRACE(terminal_adapter_logger, "Execute command: " << cmd.name);

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

        // Queue the command to the dashboard capability
        // The Dashboard event loop will dequeue and execute it
        dashboard_capability_->EnqueueCommand(command_str);

        // Return success - command has been queued
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
     * - Uses atomic storage (lock-free preferred)
     * - No heap allocation
     * - No UI updates (deferred to FlushMetrics)
     *
     * Thread: MetricsPolicy background thread
     * Frequency: Configurable (default ~100ms, configurable to 10ms)
     *
     * @param event Metrics event with source, event_type, timestamp, data
     *
     * @see FlushMetrics() for deferred UI updates
     */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event) override {
        // Store latest event for FlushMetrics to consume
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            latest_metrics_ = event;
        }
    }

    /**
     * @brief Flush buffered metrics to UI display
     *
     * Implementation of IMetricsPublisher::FlushMetrics()
     *
     * Called periodically by UI render loop to push buffered metrics to
     * display. Updates the Dashboard's display with latest metrics.
     *
     * Performance Requirements: Should complete in <50ms
     * - May perform heavier operations than OnMetricsEvent
     * - Updates Dashboard display (relatively fast)
     * - Safe to be called from UI thread
     *
     * Thread: UI thread (main loop)
     * Frequency: Every UI update cycle (e.g., ~50ms for 20 FPS)
     *
     * @see OnMetricsEvent() for fast event reception
     * @see GetLatestMetrics() for synchronous access
     */
    void FlushMetrics() override {
        if (!dashboard_) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            // Dashboard will be updated via MetricsCapability callback
            // This flush ensures any buffered updates are visible
            // The actual UI update happens in Dashboard::OnMetricsEvent()
        }
    }

    /**
     * @brief Get latest metrics snapshot
     *
     * Implementation of IMetricsPublisher::GetLatestMetrics()
     *
     * Returns the most recent MetricsEvent without flushing.
     * Used by API handlers and status commands for synchronous access.
     *
     * Performance Requirements: Must be thread-safe, <10ms
     * - Thread-safe: Uses mutex lock
     * - Fast: Simple copy operation
     *
     * Thread: Any thread (must be thread-safe)
     * Frequency: On-demand (API requests, commands)
     *
     * @return Copy of latest MetricsEvent (or default if none received)
     */
    app::metrics::MetricsEvent GetLatestMetrics() const override {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return latest_metrics_;
    }

    /**
     * @brief Get pointer to Dashboard for testing or advanced operations
     *
     * @return Shared pointer to Dashboard instance
     */
    std::shared_ptr<Dashboard> GetDashboard() const {
        return dashboard_;
    }

private:
    // Capabilities
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability_;
    std::shared_ptr<app::capabilities::MetricsCapability> metrics_capability_;
    std::shared_ptr<app::capabilities::DashboardCapability> dashboard_capability_;
    std::shared_ptr<graph::CapabilityBus> capability_bus_;

    // UI components
    std::shared_ptr<Dashboard> dashboard_;
    std::thread ui_thread_;
    std::atomic<bool> is_running_;

    // Configuration
    std::string config_path_;

    // Metrics storage
    mutable std::mutex metrics_mutex_;
    app::metrics::MetricsEvent latest_metrics_;
};

}  // namespace app::adapters
