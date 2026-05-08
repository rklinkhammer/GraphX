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
#include <mutex>
#include <set>
#include <log4cxx/logger.h>
#include <nlohmann/json.hpp>
#include <httplib.h>

#include "app/interfaces/IUIAdapter.hpp"
#include "app/interfaces/ICommandExecutor.hpp"
#include "app/interfaces/IMetricsPublisher.hpp"
#include "app/interfaces/DashboardCommand.hpp"
#include "app/metrics/MetricsEvent.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/MetricsCapability.hpp"
#include "app/capabilities/DashboardCapability.hpp"
#include "graph/CapabilityBus.hpp"

namespace app::adapters {

static auto web_adapter_logger = log4cxx::Logger::getLogger("app.adapters.WebUIAdapter");

using json = nlohmann::json;

/**
 * @class WebUIAdapter
 * @brief HTTP/WebSocket-based web UI adapter implementing standardized interfaces
 *
 * WebUIAdapter provides a web-based dashboard interface via:
 * - REST API for command execution (POST /api/commands/<command>)
 * - WebSocket for real-time metrics streaming (ws://localhost:8080/ws/metrics)
 * - Static HTML/JS frontend served from www/ directory
 *
 * Architecture:
 * - **Ownership**: Owns HTTP server and manages lifecycle
 * - **Threading**: HTTP server runs in dedicated thread, metrics from background threads
 * - **Command Routing**: HTTP POST → DashboardCommand → queue → business logic
 * - **Metrics Flow**: OnMetricsEvent() (fast, <1ms) → WebSocket broadcast (50ms)
 * - **Clients**: Multiple concurrent WebSocket clients supported
 *
 * REST API Endpoints:
 * - POST /api/commands/{command} - Execute dashboard command
 * - GET  /api/metrics/schemas    - Discover available metrics
 * - GET  /api/metrics/latest     - Get current metrics snapshot
 * - WS   /ws/metrics             - WebSocket for real-time metrics
 *
 * Lifecycle:
 * 1. Constructor initializes state but doesn't start server
 * 2. Initialize() starts HTTP server on port 8080 (configurable)
 * 3. IsRunning() reports server status
 * 4. Shutdown() stops server and cleans up
 *
 * Thread Safety:
 * - OnMetricsEvent(): Called from MetricsPolicy thread, uses mutex (<1ms)
 * - Execute(): Called from HTTP threads, delegates to DashboardCapability
 * - WebSocket clients: Protected by ws_clients_mutex_
 *
 * Example Usage:
 * ```cpp
 * auto adapter = std::make_shared<WebUIAdapter>(
 *     graph_capability,
 *     metrics_capability,
 *     dashboard_capability);
 *
 * adapter->Initialize(bus);  // Start HTTP server on port 8080
 *
 * // Open browser to http://localhost:8080
 * // REST API: curl -X POST http://localhost:8080/api/commands/status
 * // WebSocket: wscat -c ws://localhost:8080/ws/metrics
 *
 * adapter->Shutdown();  // Stop server
 * ```
 *
 * @see IUIAdapter, ICommandExecutor, IMetricsPublisher
 */
class WebUIAdapter : public app::interfaces::IUIAdapter,
                     public app::interfaces::ICommandExecutor,
                     public app::interfaces::IMetricsPublisher,
                     public std::enable_shared_from_this<WebUIAdapter> {
public:
    /**
     * @brief Construct web UI adapter with capability references
     *
     * @param graph_cap Graph execution capability
     * @param metrics_cap Metrics capability for discovery and subscription
     * @param dashboard_cap Dashboard capability for command/log queues
     * @param port HTTP server port (default 8080)
     */
    WebUIAdapter(
        std::shared_ptr<app::capabilities::GraphCapability> graph_cap,
        std::shared_ptr<app::capabilities::MetricsCapability> metrics_cap,
        std::shared_ptr<app::capabilities::DashboardCapability> dashboard_cap,
        int port = 8080)
        : graph_capability_(graph_cap),
          metrics_capability_(metrics_cap),
          dashboard_capability_(dashboard_cap),
          port_(port),
          is_running_(false),
          latest_metrics_() {
        LOG4CXX_TRACE(web_adapter_logger, "WebUIAdapter constructed on port " << port);
    }

    /**
     * @brief Destructor: cleanup any remaining resources
     */
    ~WebUIAdapter() {
        LOG4CXX_TRACE(web_adapter_logger, "WebUIAdapter destructed");
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
        LOG4CXX_DEBUG(web_adapter_logger, "Loading graph config: " << config_path);
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
            LOG4CXX_INFO(web_adapter_logger, "Graph config loaded: " << config_path);
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
     * @brief Initialize adapter: start HTTP server and register services
     *
     * Implementation of IUIAdapter::Initialize()
     *
     * Responsibilities:
     * 1. Create HTTP server (cpp-httplib)
     * 2. Setup REST endpoints (/api/commands/<command>, /api/metrics/<endpoint>)
     * 3. Setup WebSocket upgrade handler (/ws/metrics)
     * 4. Register self as ICommandExecutor and IMetricsPublisher
     * 5. Subscribe to MetricsCapability
     * 6. Start server in dedicated thread
     * 7. Set is_running_ = true
     *
     * @param bus CapabilityBus for service registration
     * @throws std::runtime_error if server startup fails
     *
     * Thread: Main thread at startup
     */
    void Initialize(std::shared_ptr<graph::CapabilityBus> bus) override {
        LOG4CXX_TRACE(web_adapter_logger, "WebUIAdapter Initialize called");

        capability_bus_ = bus;

        // Setup HTTP server endpoints
        SetupRestEndpoints();
        SetupWebSocketEndpoint();
        SetupStaticFileServing();

        // Register self as ICommandExecutor
        try {
            bus->Register<app::interfaces::ICommandExecutor>(
                std::static_pointer_cast<app::interfaces::ICommandExecutor>(shared_from_this()));
            LOG4CXX_DEBUG(web_adapter_logger, "Registered as ICommandExecutor");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(web_adapter_logger, "Failed to register ICommandExecutor: " << e.what());
            throw;
        }

        // Register self as IMetricsPublisher
        try {
            bus->Register<app::interfaces::IMetricsPublisher>(
                std::static_pointer_cast<app::interfaces::IMetricsPublisher>(shared_from_this()));
            LOG4CXX_DEBUG(web_adapter_logger, "Registered as IMetricsPublisher");
        } catch (const std::exception& e) {
            LOG4CXX_ERROR(web_adapter_logger, "Failed to register IMetricsPublisher: " << e.what());
            throw;
        }

        // Subscribe to metrics events
        try {
            auto metrics_cap = bus->Get<app::capabilities::MetricsCapability>();
            if (metrics_cap) {
                LOG4CXX_DEBUG(web_adapter_logger, "Subscribed to MetricsCapability");
            }
        } catch (const std::exception& e) {
            LOG4CXX_WARN(web_adapter_logger, "Failed to subscribe to metrics: " << e.what());
        }

        // Mark as running before starting server
        is_running_ = true;

        // Start HTTP server in separate thread
        server_thread_ = std::thread([this]() {
            LOG4CXX_DEBUG(web_adapter_logger, "HTTP server thread started on port " << port_);
            try {
                if (server_->listen("localhost", port_)) {
                    LOG4CXX_INFO(web_adapter_logger, "HTTP server listening on port " << port_);
                } else {
                    LOG4CXX_ERROR(web_adapter_logger, "Failed to start HTTP server on port " << port_);
                    is_running_ = false;
                }
            } catch (const std::exception& e) {
                LOG4CXX_ERROR(web_adapter_logger, "HTTP server exception: " << e.what());
                is_running_ = false;
            }
            LOG4CXX_DEBUG(web_adapter_logger, "HTTP server thread exiting");
        });

        LOG4CXX_INFO(web_adapter_logger, "WebUIAdapter initialized successfully");
    }

    /**
     * @brief Check if adapter is currently running
     *
     * Implementation of IUIAdapter::IsRunning()
     *
     * @return true if HTTP server is running, false otherwise
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
     * Responsibilities:
     * 1. Set is_running_ = false (signals server to stop)
     * 2. Stop HTTP server
     * 3. Close all WebSocket connections
     * 4. Wait for server thread to finish
     * 5. Idempotent (safe to call multiple times)
     *
     * Thread: Any thread
     */
    void Shutdown() override {
        LOG4CXX_TRACE(web_adapter_logger, "WebUIAdapter Shutdown called");

        // Idempotent check
        if (!is_running_.exchange(false, std::memory_order_release)) {
            LOG4CXX_TRACE(web_adapter_logger, "Already shut down");
            return;
        }

        // Stop the server
        LOG4CXX_DEBUG(web_adapter_logger, "Stopping HTTP server");
        server_->stop();

        // Wait for server thread to finish
        if (server_thread_.joinable()) {
            LOG4CXX_DEBUG(web_adapter_logger, "Waiting for HTTP server thread");
            server_thread_.join();
            LOG4CXX_DEBUG(web_adapter_logger, "HTTP server thread finished");
        }

        // Close all WebSocket connections
        {
            std::lock_guard<std::mutex> lock(ws_clients_mutex_);
            LOG4CXX_DEBUG(web_adapter_logger, "Closing " << ws_clients_.size() << " WebSocket connections");
            ws_clients_.clear();
        }

        LOG4CXX_INFO(web_adapter_logger, "WebUIAdapter shutdown complete");
    }

    /**
     * @brief Get human-readable adapter name for logging
     *
     * Implementation of IUIAdapter::GetAdapterName()
     *
     * @return "WebUIAdapter"
     */
    std::string GetAdapterName() const override {
        return "WebUIAdapter";
    }

    /**
     * @brief Execute a dashboard command
     *
     * Implementation of ICommandExecutor::Execute()
     *
     * Routes HTTP requests to command queue and returns result.
     *
     * Thread: HTTP handler thread (from cpp-httplib)
     * Performance: Should complete in <100ms
     *
     * @param cmd DashboardCommand with name, args, context_id
     * @return CommandResult with status and result data
     */
    app::interfaces::CommandResult Execute(const app::interfaces::DashboardCommand& cmd) override {
        LOG4CXX_TRACE(web_adapter_logger, "Execute command: " << cmd.name);

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
     * Stores event and broadcasts to WebSocket clients.
     *
     * Performance Requirements: MUST complete in <1ms
     * - Uses mutex-protected storage
     * - Returns immediately
     *
     * Thread: MetricsPolicy background thread
     * Frequency: Configurable (default ~100ms, up to 10ms)
     *
     * @param event Metrics event with source, event_type, timestamp, data
     */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event) override {
        // Store latest event atomically
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            latest_metrics_ = event;
        }

        // Broadcast to WebSocket clients
        BroadcastMetricsToWebSocket(event);
    }

    /**
     * @brief Flush buffered metrics to UI display
     *
     * Implementation of IMetricsPublisher::FlushMetrics()
     *
     * In WebUIAdapter, metrics are pushed via WebSocket immediately,
     * so this is a no-op (metrics already flushed in OnMetricsEvent).
     *
     * Thread: UI thread (main loop)
     */
    void FlushMetrics() override {
        // WebSocket metrics already pushed in OnMetricsEvent()
        // This is a no-op for web adapter
    }

    /**
     * @brief Get latest metrics snapshot
     *
     * Implementation of IMetricsPublisher::GetLatestMetrics()
     *
     * Returns copy of latest metrics for synchronous access.
     *
     * Thread: Any thread (mutex-protected)
     *
     * @return Copy of latest MetricsEvent
     */
    app::metrics::MetricsEvent GetLatestMetrics() const override {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return latest_metrics_;
    }

private:
    // Capabilities
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability_;
    std::shared_ptr<app::capabilities::MetricsCapability> metrics_capability_;
    std::shared_ptr<app::capabilities::DashboardCapability> dashboard_capability_;
    std::shared_ptr<graph::CapabilityBus> capability_bus_;

    // HTTP server (cpp-httplib)
    std::unique_ptr<httplib::Server> server_;

    // Server state
    std::thread server_thread_;
    int port_;
    std::atomic<bool> is_running_;
    std::string config_path_;

    // WebSocket client management
    struct WebSocketClient {
        std::string id;
        std::shared_ptr<httplib::Stream> stream;

        bool operator<(const WebSocketClient& other) const {
            return id < other.id;
        }
    };
    std::set<WebSocketClient> ws_clients_;
    std::mutex ws_clients_mutex_;

    // WebSocket protocol helpers
    void HandleWebSocketUpgrade(const httplib::Request& req, httplib::Response& res);
    void SendWebSocketFrame(const std::shared_ptr<httplib::Stream>& stream, const std::string& json_message);
    std::string BuildWebSocketResponseKey(const std::string& key);

    // Metrics storage
    mutable std::mutex metrics_mutex_;
    app::metrics::MetricsEvent latest_metrics_;

    // Setup methods (implemented in .cpp)
    void SetupRestEndpoints();
    void SetupWebSocketEndpoint();
    void SetupStaticFileServing();
    void BroadcastMetricsToWebSocket(const app::metrics::MetricsEvent& event);
};

}  // namespace app::adapters
