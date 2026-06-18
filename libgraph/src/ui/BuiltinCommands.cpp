/**
 * @file BuiltinCommands.cpp
 * @brief Builtin Commands Graph runtime support.
 *
 * @details Provides command and metric helpers for interactive graph tooling. This file is documented for Doxygen so public APIs and test support surfaces can be browsed consistently.
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


#include "ui/BuiltinCommands.hpp"
#include "ui/CommandRegistry.hpp"
#include "capabilities/CommandRegistryCapability.hpp"
#include <sstream>
#include <iomanip>

namespace ui {

void RegisterBuiltinCommands(
    capabilities::CommandRegistryCapability& cmd_registry,
    std::shared_ptr<capabilities::GraphCapability> graph_capability) {
    
    // =========================================================================
    // HELP Command
    // =========================================================================
    cmd_registry.RegisterCommand(
        "help",
        "Display available commands",
        "help [command]",
        [&cmd_registry](const std::vector<std::string>& args) -> CommandResult {
            auto cmds = cmd_registry.GetAllCommands();
            
            if (args.size() > 1) {
                // Show help for specific command
                const auto* info = cmd_registry.GetCommandInfo(args[1]);
                if (!info) {
                    return CommandResult(false, "Command not found: " + args[1]);
                }
                std::string output = "Command: " + info->name + "\n"
                                   + "Description: " + info->description + "\n"
                                   + "Usage: " + info->usage + "\n";
                return CommandResult(true, output);
            } else {
                // Show all commands
                std::string output = "Available Commands:\n";
                for (const auto& cmd : cmds) {
                    output += "  " + std::string(15, ' ');
                    output.erase(output.length() - cmd.name.length());
                    output += cmd.name + " - " + cmd.description + "\n";
                }
                output += "\nUse 'help <command>' for detailed help on a specific command.\n";
                return CommandResult(true, output);
            }
        });

    // =========================================================================
    // PAUSE Command
    // =========================================================================
    cmd_registry.RegisterCommand(
        "pause",
        "Pause graph execution",
        "pause",
        [](const std::vector<std::string>& /*args*/) -> CommandResult {
            // Note: Graph pause implementation would be done via GraphCapability
            // This is a placeholder that demonstrates the pattern
            return CommandResult(true, "Graph execution paused");
        });

    // =========================================================================
    // RESUME Command
    // =========================================================================
    cmd_registry.RegisterCommand(
        "resume",
        "Resume graph execution",
        "resume",
        [](const std::vector<std::string>& /*args*/) -> CommandResult {
            // Note: Graph resume implementation would be done via GraphCapability
            // This is a placeholder that demonstrates the pattern
            return CommandResult(true, "Graph execution resumed");
        });

    // =========================================================================
    // STOP Command
    // =========================================================================
    cmd_registry.RegisterCommand(
        "stop",
        "Stop graph execution",
        "stop",
        [graph_capability](const std::vector<std::string>& /*args*/) -> CommandResult {
            if (!graph_capability) {
                return CommandResult(false, "Graph capability unavailable");
            }

            graph_capability->SetStopped();
            return CommandResult(true, "Graph execution stopped");
        });

    // =========================================================================
    // STATUS Command
    // =========================================================================
    cmd_registry.RegisterCommand(
        "status",
        "Display execution status",
        "status",
        [graph_capability](const std::vector<std::string>& /*args*/) -> CommandResult {
            const auto state = graph_capability && graph_capability->IsStopped()
                ? "Stopped"
                : "Running";
            std::string output = "Graph Status:\n"
                               "  State: " + std::string(state) + "\n"
                               "  Nodes Executed: 0\n"
                               "  Edges Executed: 0\n";
            return CommandResult(true, output);
        });
}

}  // namespace ui
