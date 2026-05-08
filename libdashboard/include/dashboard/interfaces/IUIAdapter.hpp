#pragma once

#include <memory>
#include <string>

namespace graph {
class CapabilityBus;
}  // namespace graph

namespace app::interfaces {

/**
 * @class IUIAdapter
 * @brief Abstract interface for UI implementations
 *
 * Defines lifecycle management interface that all UI adapters must implement.
 * Allows pluggable UI implementations (Terminal, Web, CLI) to be swapped at runtime.
 *
 * Lifecycle Pattern:
 * 1. Create adapter instance
 * 2. Call Initialize(bus) - adapter registers itself and initializes resources
 * 3. Call IsRunning() to check status
 * 4. UI runs in main loop (specific to each adapter)
 * 5. Call Shutdown() to cleanup when done
 *
 * Implementations:
 * - TerminalUIAdapter - ncurses-based terminal UI
 * - WebUIAdapter - HTTP/WebSocket server
 * - CLIAdapter - Command-line interactive or batch mode
 *
 * Responsibilities of Implementations:
 * 1. Register ICommandExecutor in Initialize()
 * 2. Register IMetricsPublisher in Initialize()
 * 3. Subscribe to MetricsCapability if needed
 * 4. Create and manage UI-specific resources
 * 5. Handle user input and produce DashboardCommand objects
 * 6. Display metrics to user
 * 7. Clean up gracefully in Shutdown()
 *
 * Example Implementation:
 * ```cpp
 * class MyUIAdapter : public IUIAdapter,
 *                     public ICommandExecutor,
 *                     public IMetricsPublisher {
 * private:
 *     shared_ptr<MyUIComponent> ui_component_;
 *     shared_ptr<CapabilityBus> bus_;
 *     atomic<bool> is_running_{false};
 *
 * public:
 *     void Initialize(shared_ptr<CapabilityBus> bus) override {
 *         bus_ = bus;
 *         ui_component_ = make_shared<MyUIComponent>();
 *         ui_component_->Init();
 *
 *         // Register self as command executor
 *         bus_->Register<ICommandExecutor>(
 *             static_pointer_cast<ICommandExecutor>(shared_from_this()));
 *
 *         // Register self as metrics publisher
 *         bus_->Register<IMetricsPublisher>(
 *             static_pointer_cast<IMetricsPublisher>(shared_from_this()));
 *
 *         is_running_ = true;
 *     }
 *
 *     bool IsRunning() const override { return is_running_.load(); }
 *
 *     void Shutdown() override {
 *         is_running_ = false;
 *         if (ui_component_) {
 *             ui_component_->Cleanup();
 *         }
 *     }
 *
 *     string GetAdapterName() const override { return "MyUIAdapter"; }
 *
 *     // Implement ICommandExecutor...
 *     CommandResult Execute(const DashboardCommand& cmd) override { ... }
 *
 *     // Implement IMetricsPublisher...
 *     void OnMetricsEvent(const MetricsEvent& event) override { ... }
 *     void FlushMetrics() override { ... }
 *     MetricsEvent GetLatestMetrics() const override { ... }
 * };
 * ```
 *
 * Thread Safety:
 * - Initialize() called once at startup (main thread)
 * - IsRunning() may be called from any thread
 * - Shutdown() called once at exit (may be called from signal handler)
 * - UI event loop (if blocking) runs in main or dedicated thread
 */
class IUIAdapter {
public:
    virtual ~IUIAdapter() = default;

    /**
     * @brief Load graph configuration from JSON topology file
     *
     * Optional method to load and configure the graph that this adapter will display.
     * If called, must be called BEFORE Initialize().
     *
     * The JSON file should contain the graph topology definition:
     * ```json
     * {
     *   "nodes": [
     *     {"id": "node1", "type": "DataSource", ...},
     *     ...
     *   ],
     *   "edges": [
     *     {"source": "node1", "target": "node2", ...},
     *     ...
     *   ]
     * }
     * ```
     *
     * @param config_path Path to JSON graph configuration file
     * @throws std::runtime_error if file not found or JSON is invalid
     * @throws std::invalid_argument if graph configuration is incomplete
     *
     * Thread: Main thread before Initialize()
     * Called: Optional, before Initialize()
     */
    virtual void LoadGraphConfig(const std::string& config_path) {
        // Default implementation: no-op (adapters can override if needed)
        (void)config_path;  // Avoid unused parameter warning
    }

    /**
     * @brief Get the loaded graph configuration path
     *
     * Returns the path to the graph configuration file that was loaded.
     * If no config was loaded, returns empty string.
     *
     * @return Path to loaded graph config, or empty string if none loaded
     */
    virtual std::string GetGraphConfigPath() const {
        return "";  // Default: no graph config loaded
    }

    /**
     * @brief Initialize UI adapter with capability bus
     *
     * Called once at application startup. The adapter should:
     * 1. Store reference to CapabilityBus
     * 2. Register itself as ICommandExecutor in bus
     * 3. Register itself as IMetricsPublisher in bus
     * 4. Subscribe to MetricsCapability if needed
     * 5. Create and initialize UI-specific resources
     * 6. Load configuration (port numbers, themes, etc.)
     * 7. Prepare for event loop or server startup
     *
     * Typical implementation flow:
     * ```cpp
     * void Initialize(shared_ptr<CapabilityBus> bus) override {
     *     capability_bus_ = bus;
     *
     *     // Create UI resources
     *     SetupUI();
     *
     *     // Register as executor and publisher
     *     bus->Register<ICommandExecutor>(
     *         static_pointer_cast<ICommandExecutor>(shared_from_this()));
     *     bus->Register<IMetricsPublisher>(
     *         static_pointer_cast<IMetricsPublisher>(shared_from_this()));
     *
     *     is_running_ = true;
     * }
     * ```
     *
     * @param bus CapabilityBus to register with
     *            Provides access to: MetricsCapability, GraphCapability, etc.
     *
     * @throws std::runtime_error if initialization fails
     *         (e.g., network port in use, UI library unavailable)
     *
     * Thread: Main thread at startup
     * Called once, idempotent
     */
    virtual void Initialize(std::shared_ptr<graph::CapabilityBus> bus) = 0;

    /**
     * @brief Check if adapter is currently running
     *
     * Returns true if Initialize() was called successfully and
     * Shutdown() has not been called.
     *
     * Used by:
     * - Main loop to detect shutdown requests
     * - Signal handlers to gracefully exit
     * - Health check code
     *
     * @return true if adapter is initialized and running, false otherwise
     *
     * Thread: Any thread (must be fast, thread-safe)
     * Called frequently (e.g., every event loop iteration)
     */
    virtual bool IsRunning() const = 0;

    /**
     * @brief Graceful shutdown of the adapter
     *
     * Called once at application exit. The adapter should:
     * 1. Stop accepting new input
     * 2. Unregister from CapabilityBus if needed
     * 3. Close UI resources (windows, sockets, threads)
     * 4. Flush any remaining output
     * 5. Release allocated resources
     * 6. Wait for background threads to finish (if any)
     *
     * Safe to call multiple times (idempotent).
     * May be called from signal handler or main thread.
     *
     * Typical implementation:
     * ```cpp
     * void Shutdown() override {
     *     if (!is_running_) {
     *         return;  // Idempotent
     *     }
     *
     *     is_running_ = false;
     *
     *     // Stop accepting input
     *     StopAcceptingInput();
     *
     *     // Flush buffered output
     *     FlushMetrics();
     *
     *     // Wait for background threads
     *     if (background_thread_.joinable()) {
     *         background_thread_.join();
     *     }
     *
     *     // Close resources
     *     CloseUI();
     * }
     * ```
     *
     * Thread: May be called from signal handler or main thread
     * Idempotent: Safe to call multiple times
     * Blocking: May wait for background threads to finish
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Get human-readable adapter name for logging
     *
     * Returns a string identifying the UI adapter implementation.
     * Used for logging, debugging, and diagnostics.
     *
     * Examples:
     * - "TerminalUIAdapter"
     * - "WebUIAdapter"
     * - "CLIAdapter"
     *
     * @return String name of this adapter (non-empty)
     *
     * Thread: Any thread (must be thread-safe)
     */
    virtual std::string GetAdapterName() const = 0;
};

}  // namespace app::interfaces
