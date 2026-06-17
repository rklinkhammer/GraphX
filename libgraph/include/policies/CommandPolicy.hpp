/**
 * @file CommandPolicy.hpp
 * @brief GraphX source file.
 */

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
#include <sstream>
#include <vector>
#include <log4cxx/logger.h>
#include "graph/IExecutionPolicy.hpp"
#include "graph/DefaultCommandProcessor.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/DashboardCapability.hpp"
#include "capabilities/CommandRegistryCapability.hpp"
#include "capabilities/CommandProcessorCapability.hpp"
#include "capabilities/DashboardOutput.hpp"
#include "capabilities/CommandOutputCapability.hpp"
#include "ui/CommandRegistry.hpp"
#include "ui/BuiltinCommands.hpp"



namespace policies {

static auto command_logger = log4cxx::Logger::getLogger("app.policies.CommandPolicy");

/**
 * @class CommandPolicy
 * @brief Execution policy for command injection and execution during graph runtime
 *
 * CommandPolicy provides hooks for the dashboard to inject commands (start, stop, pause)
 * into the graph execution during runtime. Receives execution phase callbacks and can
 * trigger command processing at appropriate points in the execution lifecycle.
 *
 * Key responsibilities:
 * 1. **Initialization**: Set up command injection infrastructure
 * 2. **Phase Transitions**: Detect and respond to state changes
 * 3. **Command Processing**: Execute injected commands from dashboard
 *
 * Lifecycle:
 * - OnInit(): Prepare command processing (optional)
 * - OnStart(): Begin accepting commands (optional)
 * - OnStop(): Finish processing pending commands
 * - OnJoin(): Clean up after execution
 *
 * Integration Points:
 * - Dashboard sends commands to GraphExecutor
 * - GraphExecutor routes via policies during execution
 * - CommandPolicy hooks key transition points
 * - Policies can influence graph execution (pausing, stepping, etc.)
 *
 * @see IExecutionPolicy, GraphExecutor
 */
class CommandPolicy : public graph::IExecutionPolicy {
public:
    /**
     * @brief Construct a command policy
     */
    CommandPolicy() {
        LOG4CXX_TRACE(command_logger, "CommandPolicy initialized");
    }   

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CommandPolicy() = default;

    /**
     * @brief Initialize command infrastructure during graph setup
     *
     * Called by GraphExecutor during Init() phase.
     * Can set up command queues or other infrastructure.
     *
     * @param context GraphExecutorContext with graph reference
     * @return True if initialization succeeded, false on error
     *
     * @see OnStart, OnStop
     */
    bool OnInit(capabilities::GraphCapability &context) override {
        LOG4CXX_TRACE(command_logger, "CommandPolicy OnInit called");
        
        // Phase 1: Create and register CommandRegistryCapability
        auto cmd_registry_cap = std::make_shared<capabilities::CommandRegistryCapability>();
        context.GetCapabilityBus()
            .Register<capabilities::CommandRegistryCapability>(cmd_registry_cap);
        
        // Register built-in commands via capability
        auto graph_capability =
            context.GetCapabilityBus().Get<capabilities::GraphCapability>();
        ui::RegisterBuiltinCommands(*cmd_registry_cap, graph_capability);
        
        // Phase 3: Create and register CommandOutputCapability
        auto dashboard_capability = context.GetCapabilityBus().Get<capabilities::DashboardCapability>();
        if (dashboard_capability) {
            auto dashboard_output = std::make_shared<capabilities::DashboardOutput>(dashboard_capability);
            auto output_capability = std::make_shared<capabilities::CommandOutputCapability>(dashboard_output);
            context.GetCapabilityBus()
                .Register<capabilities::CommandOutputCapability>(output_capability);
            output_cap_ = output_capability;
            LOG4CXX_DEBUG(command_logger, "CommandOutputCapability registered");
        }
        
        // Phase 2: Create and register CommandProcessorCapability
        auto processor = std::make_shared<graph::DefaultCommandProcessor>(cmd_registry_cap);
        auto cmd_processor_cap = std::make_shared<capabilities::CommandProcessorCapability>(processor);
        context.GetCapabilityBus()
            .Register<capabilities::CommandProcessorCapability>(cmd_processor_cap);
        
        // Store for later use
        cmd_processor_cap_ = cmd_processor_cap;
        
        LOG4CXX_DEBUG(command_logger, "CommandProcessorCapability registered with "
            << cmd_processor_cap->GetAllCommands().size() << " commands");
        
        return true;
    }

    /**
     * @brief Start accepting commands from the dashboard
     *
     * Called by GraphExecutor during Start() phase.
     * Enables processing of dashboard commands during execution.
     *
     * @param context GraphExecutorContext for accessing shared resources
     * @return True if startup succeeded, false on error
     *
     * @see OnStop, OnJoin
     */
    bool OnStart(capabilities::GraphCapability &context) override {
        LOG4CXX_TRACE(command_logger, "CommandPolicy OnStart called");
        auto graph_capability = context.GetCapabilityBus().Get<capabilities::GraphCapability>();
        dashboard_capability_ = context.GetCapabilityBus().Get<capabilities::DashboardCapability>();
        if (!graph_capability) {
            LOG4CXX_WARN(command_logger, "CommandPolicy OnStart failed - no GraphCapability registered");
            return false;
        }
        if (!dashboard_capability_) {
            LOG4CXX_WARN(command_logger, "CommandPolicy OnStart failed - no DashboardCapability registered");
            return false;
        }
        if (!cmd_processor_cap_ || !cmd_processor_cap_->IsReady()) {
            LOG4CXX_WARN(command_logger, "CommandPolicy OnStart failed - command processor is not ready");
            return false;
        }

        auto command_processor = [this, graph_capability]() {
            // Command processing loop
            std::string command;
            while (dashboard_capability_->DequeueCommand(command) && !graph_capability->IsStopped()) {
                ExecuteCommand(command);
            }
        };
        command_thread_ = std::jthread(command_processor);
        // Start accepting commands here if needed
        return true;
    }

    /**
     * @brief Stop accepting new commands and finalize pending ones
     *
     * Called by GraphExecutor during Stop() phase.
     * Flushes any pending commands and stops accepting new ones.
     *
     * @param context GraphExecutorContext for cleanup
     *
     * @see OnStart, OnJoin
     */
    void OnStop(capabilities::GraphCapability &) override {
        LOG4CXX_TRACE(command_logger, "CommandPolicy OnStop called");
        if (dashboard_capability_) {
            dashboard_capability_->DisableCommandQueue();
        }
 
    }

    /**
     * @brief Finalize command processing after execution joins
     *
     * Called by GraphExecutor during Join() phase after all nodes complete.
     * Performs final cleanup and reporting of command execution.
     *
     * @param context GraphExecutorContext for final cleanup
     *
     * @see OnStop, OnStart
     */
    void OnJoin(capabilities::GraphCapability &) override {
        LOG4CXX_TRACE(command_logger, "CommandPolicy OnJoin called");
        // Finalize command processing here if needed
        if (command_thread_.joinable()) {
            command_thread_.join();
        }
    }   

private:

    std::jthread command_thread_;
    std::shared_ptr<capabilities::CommandProcessorCapability> cmd_processor_cap_;
    std::shared_ptr<capabilities::CommandOutputCapability> output_cap_;
    std::shared_ptr<capabilities::DashboardCapability> dashboard_capability_;

    void ExecuteCommand(const std::string& cmd) {
        // Phase 2: Use CommandProcessorCapability for parsing and execution
        if (!cmd_processor_cap_ || !cmd_processor_cap_->IsReady()) {
            dashboard_capability_->AddLog("[ERROR] Command processor not initialized");
            return;
        }
        
        // Process raw command string (parsing + validation + execution)
        CommandResult result = cmd_processor_cap_->ProcessCommand(cmd);
        
        if (!result.success) {
            dashboard_capability_->AddLog("[ERROR] " + result.message);
        } else {
            dashboard_capability_->AddLog("[OK] " + result.message);
        }
    }


}; // class CommandPolicy
    
}// namespace policies
