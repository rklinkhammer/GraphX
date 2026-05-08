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

#ifndef GDASHBOARD_DASHBOARD_HPP
#define GDASHBOARD_DASHBOARD_HPP

#include <ncurses.h>
#include <string>
#include <chrono>
#include <thread>
#include <memory>

#include "ui/MetricsPanel.hpp"
#include "ui/LogWindow.hpp"
#include "ui/CommandWindow.hpp"
#include "ui/StatusBar.hpp"
#include "ui/CommandRegistry.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/MetricsCapability.hpp"
#include "app/capabilities/DashboardCapability.hpp"
#include "app/metrics/MetricsEvent.hpp"
#include "app/metrics/IMetricsSubscriber.hpp"

// Using declarations for production types

/**
 * @class Dashboard
 * @brief Main dashboard application orchestrator for real-time metrics visualization
 *
 * Dashboard is the central coordinator that manages a 4-panel layout displaying
 * real-time metrics from a graph execution engine. It subscribes to metrics events,
 * manages user input, executes commands, and renders the FTXUI-based interface.
 *
 * Layout:
 * - **Metrics Panel** (68%): Tabbed display of node metrics organized by node
 * - **Logging Window** (15%): Scrollable log of system and execution events
 * - **Command Window** (15%): User input with command output display
 * - **Status Bar** (2%): Runtime state indicators (running, paused, stopped)
 *
 * Thread Safety: Dashboard is not internally thread-safe. All operations should
 * occur in the main event loop thread. Metrics updates from executor callback
 * threads are received via OnMetricsEvent() and queued in MetricsPanel with
 * mutex protection.
 *
 * @see MetricsPanel, LogWindow, CommandWindow, StatusBar
 *
 * @example
 *   auto dashboard = std::make_shared<Dashboard>(graph_cap, metrics_cap, registry);
 *   dashboard->Initialize();
 *   dashboard->Run();  // Blocks until user quits
 */
class Dashboard : public app::metrics::IMetricsSubscriber {

public:
    /**
     * @brief Construct a Dashboard with capabilities and command registry
     *
     * @param graph_cap Graph execution capability for state queries
     * @param metrics_cap Metrics capability for discovery and subscription
     * @param registry Command registry for user command execution
     *
     * @see app::capabilities::GraphCapability
     * @see app::capabilities::MetricsCapability
     * @see CommandRegistry
     */
    Dashboard(std::shared_ptr<app::capabilities::GraphCapability> graph_cap,
              std::shared_ptr<app::capabilities::MetricsCapability> metrics_cap,
              std::shared_ptr<app::capabilities::DashboardCapability> dashboard_cap) 
        : metrics_panel(nullptr), log_window(nullptr), 
          command_window(nullptr), status_bar(nullptr),
          metrics_win(nullptr), log_win(nullptr), 
          cmd_win(nullptr), status_win(nullptr),
          last_update_(std::chrono::steady_clock::now()),
          graph_capability_(graph_cap),
          metrics_capability_(metrics_cap),
          dashboard_capability_(dashboard_cap) {}
    
    /**
     * @brief Destructor: cleans up ncurses and resources
     */
    ~Dashboard() {
        Cleanup();
    }

    /**
     * @brief Initialize the dashboard: discover metrics, setup panels, start ncurses
     *
     * This method:
     * 1. Initializes ncurses terminal interface
     * 2. Discovers available metrics from graph nodes
     * 3. Creates MetricsTiles for each node
     * 4. Sets up logging appender for log4cxx events
     * 5. Prepares command window with built-in commands
     * 6. Initializes status bar
     *
     * @return true if initialization succeeded, false otherwise
     *
     * @see MetricsPanel::Initialize
     * @see LogWindow::Initialize
     * @see CommandWindow::Initialize
     */
    bool Initialize();
    
    /**
     * @brief Handle a metrics update event from executor callback thread
     *
     * This method is called by MetricsCapability when graph nodes publish metrics.
     * It updates the MetricsPanel with the latest value in a thread-safe manner.
     *
     * @param event The metrics event containing node name, metric ID, and value
     *
     * @note This method is called from executor threads and must be thread-safe
     * @see app::metrics::MetricsEvent
     */
    void OnMetricsEvent(const app::metrics::MetricsEvent& event);
    
    /**
     * @brief Process keyboard input from ncurses
     *
     * Handles arrow keys for tab navigation, 'q' for quit, and passes
     * other input to CommandWindow for command interpretation.
     *
     * @param ch The character/key code from getch()
     */
    void HandleInput(int ch);
    
    /**
     * @brief Execute a command entered in the command window
     *
     * Routes the command to CommandRegistry for execution and updates
     * status and log windows with results.
     *
     * @param cmd The command string (may include arguments)
     *
     * @see CommandRegistry::ExecuteCommand
     */
    //void ExecuteCommand(const std::string& cmd);
    
    /**
     * @brief Render the dashboard (called at ~30 FPS from main loop)
     *
     * Updates all panels with current data and refreshes the screen.
     * Includes timing logic to skip frames if rendering takes too long.
     */
    void Render();
    
    /**
     * @brief Handle terminal resize event (called from SIGWINCH handler)
     *
     * Recalculates panel positions and sizes, then refreshes display.
     */
    void HandleResize();
    
    /**
     * @brief Clean up ncurses and release resources
     *
     * Should be called before application exit. Restores terminal state.
     */
    void Cleanup();
    
    /**
     * @brief Run the main event loop until user exits
     *
     * Blocks execution in a loop:
     * 1. Poll for keyboard input
     * 2. Handle metrics updates
     * 3. Render at ~30 FPS
     * 4. Continue until 'q' is pressed
     *
     * Exits when user presses 'q' or receives shutdown signal.
     */
    void Run();

    /**
     * @brief Add a metric tile for a new node
     *
     * @param tile The node metrics tile to add as a new tab
     *
     * @see NodeMetricsTile
     */
    void AddMetricsTile(const NodeMetricsTile& tile);
    
    /**
     * @brief Add a line to the logging window
     *
     * @param line The log message to display
     */
    void AddLog(const std::string& line);

    /**
     * @brief Update the command registry (can be called after construction)
     *
     * @param registry New command registry to use
     */
    //void SetCommandRegistry(std::shared_ptr<CommandRegistry> registry) { registry_ = registry; }

    /**
     * @brief Get pointer to metrics panel for testing or advanced operations
     *
     * @return Pointer to MetricsPanel (or nullptr if not initialized)
     */
    MetricsPanel* GetMetricsPanel() const { return metrics_panel; }
    LogWindow* GetLoggingWindow() const { return log_window; }
    CommandWindow* GetCommandWindow() const { return command_window; }
    StatusBar* GetStatusBar() const { return status_bar; }
    bool GetFilterActive() const { return filter_active_; }
    std::string GetFilterPattern() const { return filter_pattern_; }
    void ClearFilter() { filter_pattern_.clear(); filter_active_ = false; }
    void SetFilterPattern(const std::string& pattern) { filter_pattern_ = pattern; filter_active_ = true; }
    void UpdateStatusBarWithFilter();
    bool GetResizeSignal() const { return resize_signal_; }
    void SetResizeSignal(bool val) { resize_signal_ = val; }

private:
    MetricsPanel* metrics_panel;
    LogWindow* log_window;
    CommandWindow* command_window;
    StatusBar* status_bar;

    WINDOW* metrics_win;
    WINDOW* log_win;
    WINDOW* cmd_win;
    WINDOW* status_win;

    int term_h = 0;
    int term_w = 0;
    
    // Real-time update mechanism
    std::chrono::steady_clock::time_point last_update_;
    
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability_;
    std::shared_ptr<app::capabilities::MetricsCapability> metrics_capability_;
    std::shared_ptr<app::capabilities::DashboardCapability> dashboard_capability_;

    // Filter state
    std::string filter_pattern_;
    bool filter_active_ = false;
    bool resize_signal_ = false;

    // Helper methods
    bool SetupTerminal();
    bool CreateWindows();
    bool CreatePanels();

};

#endif // GDASHBOARD_DASHBOARD_HPP
