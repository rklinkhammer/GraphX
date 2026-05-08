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
#include <chrono>
#include <log4cxx/logger.h>
#include "graph/IExecutionPolicy.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/MetricsCapability.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include "app/capabilities/DashboardCapability.hpp"
#include "ui/Dashboard.hpp"
#include "ui/BuiltinCommands.hpp"

namespace app::policies
{

    static auto dashboard_logger = log4cxx::Logger::getLogger("app.policies.DashboardPolicy");

    /**
     * @class DashboardPolicy
     * @brief Execution policy that manages the interactive dashboard UI
     *
     * DashboardPolicy creates and manages the ncurses-based dashboard that displays
     * real-time metrics and accepts user commands during graph execution.
     *
     * Key responsibilities:
     * 1. **Dashboard Creation**: Create Dashboard instance with capabilities
     * 2. **UI Thread Management**: Spawn separate thread for 30 FPS UI rendering
     * 3. **Command Registry**: Register built-in commands for user interaction
     * 4. **Lifecycle Coordination**: Initialize, start, and cleanly shutdown UI
     * 5. **Input Handling**: Route keyboard input to graph execution via commands
     *
     * Architecture:
     * - Dashboard runs on separate UI thread from executor
     * - Communicates with graph via MetricsCapability and CommandRegistry
     * - Receives metrics events from executor thread via callback
     * - Sends user commands back to executor for handling
     * - Gracefully shuts down when execution completes
     *
     * Integration:
     * - Last policy in chain (highest priority)
     * - Requires MetricsCapability and GraphCapability in bus
     * - Uses CommandRegistry for command execution
     * - Called during all execution phases (Init, Start, Stop, Join)
     *
     * Example usage (from GraphExecutor):
     * ```cpp
     * auto metrics_policy = std::make_shared<MetricsPolicy>();
     * auto dashboard_policy = std::make_shared<DashboardPolicy>();
     * 
     * ExecutionPolicyChain policies;
     * policies.Push(metrics_policy);      // Set up metrics
     * policies.Push(dashboard_policy);    // Set up UI
     * 
     * executor.SetPolicies(policies);
     * executor.Init(config);  // Creates Dashboard
     * executor.Start();       // Spawns UI thread
     * // User interacts with dashboard
     * executor.Stop();        // Signals UI to stop
     * executor.Join();        // Waits for UI thread to finish
     * ```
     *
     * @see IExecutionPolicy, Dashboard, MetricsCapability, CommandRegistry
     */
    class DashboardPolicy : public graph::IExecutionPolicy
    {
    public:
        /**
         * @brief Virtual destructor for proper cleanup
         */
        virtual ~DashboardPolicy() = default;

        /**
         * @brief Initialize the dashboard UI
         *
         * Called by GraphExecutor during Init() phase. Creates Dashboard instance,
         * registers built-in commands, and performs UI initialization.
         * Queries MetricsCapability and GraphCapability from bus.
         *
         * @param context GraphExecutorContext with capability bus
         * @return True if initialization succeeded, false on error
         *
         * @see OnStart, Dashboard::Initialize
         */
        bool OnInit(app::capabilities::GraphCapability &context) override
        {
            LOG4CXX_TRACE(dashboard_logger, "DashboardPolicy OnInit called");
            auto metrics_capability = context.GetCapabilityBus().Get<app::capabilities::MetricsCapability>();
            auto graph_capability = context.GetCapabilityBus().Get<app::capabilities::GraphCapability>();
            auto dashboard_capability = std::make_shared<app::capabilities::DashboardCapability>();
            context.GetCapabilityBus().Register<app::capabilities::DashboardCapability>(dashboard_capability);
            dashboard_ = std::make_shared<Dashboard>(graph_capability, metrics_capability, dashboard_capability);
            dashboard_->Initialize();
            // Initialize dashboard here
            return true;
        }

        /**
         * @brief Start the dashboard UI thread
         *
         * Called by GraphExecutor during Start() phase. Spawns a separate thread
         * that runs the 30 FPS event loop for dashboard rendering and input handling.
         *
         * @param context GraphExecutorContext (unused, passed for interface compatibility)
         * @return True if thread startup succeeded, false on error
         *
         * @see OnStop, Dashboard::Run
         */
        bool OnStart(app::capabilities::GraphCapability &) override
        {
            LOG4CXX_TRACE(dashboard_logger, "DashboardPolicy OnStart called");
            auto ui = [this]() {
                dashboard_->Run();
            };
            dashboard_thread_ = std::thread(ui);
            return true;
        }

        /**
         * @brief Stop the dashboard UI
         *
         * Called by GraphExecutor during Stop() phase. Signals dashboard to stop
         * accepting input and shut down gracefully.
         *
         * @param context GraphExecutorContext (unused, passed for interface compatibility)
         *
         * @see OnStart, OnJoin
         */
        void OnStop(app::capabilities::GraphCapability &context) override
        {
            LOG4CXX_TRACE(dashboard_logger, "DashboardPolicy OnStop called");
            auto dashboard_capability = context.GetCapabilityBus().Get<app::capabilities::DashboardCapability>();
            dashboard_capability->DisableCommandQueue();
            dashboard_capability->DisableLogQueue();        

            // Stop metrics collection and cleanup here if needed
        }

        void OnJoin(app::capabilities::GraphCapability &) override
        {
            LOG4CXX_TRACE(dashboard_logger, "DashboardPolicy OnJoin called");
            if (dashboard_thread_.joinable()) {
                dashboard_thread_.join();
            }
        }

    private:
        std::thread dashboard_thread_;
        std::shared_ptr<Dashboard> dashboard_;

    }; // class DashboardPolicy

} // namespace app::policies