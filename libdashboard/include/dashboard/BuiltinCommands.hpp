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

#include "ui/CommandRegistry.hpp"
#include "app/capabilities/GraphCapability.hpp"
#include <memory>
#include <string>

// Forward declarations
class Dashboard;

namespace commands {

/**
 * Register all built-in commands with the command registry.
 * These commands provide core dashboard functionality:
 * - help: Display available commands
 * - clear: Clear command window output
 * - status: Display dashboard status
 * - run_graph: Execute the computation graph
 * - pause: Pause graph execution
 * - stop: Stop graph execution
 * - reset_layout: Reset window layout to defaults
 */
void RegisterBuiltinCommands(
    std::shared_ptr<CommandRegistry> registry,
    Dashboard* app,
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability);

// Individual command implementations
namespace cmd {

// System commands
CommandResult CmdStatus(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdListMetrics(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdToggleSparklines(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdClearFilter(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdFilterMetrics(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdSetConfidence(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdShowEvents(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdShowHistory(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdSetMetric(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdClearMetrics(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdExportMetrics(Dashboard* app, const std::vector<std::string>& args);
CommandResult CmdCsvStep(Dashboard* app, const std::vector<std::string>& args, 
    std::shared_ptr<app::capabilities::GraphCapability> graph_capability);


}  // namespace cmd

}  // namespace commands
