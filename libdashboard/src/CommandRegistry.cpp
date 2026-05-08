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

#include "ui/CommandRegistry.hpp"
#include "ui/Dashboard.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <iomanip>

bool CommandRegistry::RegisterCommand(
    const std::string& name,
    const std::string& description,
    const std::string& usage,
    CommandHandler handler) {
    
    // Check if command already exists
    if (commands_.find(name) != commands_.end()) {
        std::cerr << "[CommandRegistry] Warning: Command '" << name << "' already registered\n";
        return false;
    }
    
    // Check for empty name or handler
    if (name.empty() || !handler) {
        std::cerr << "[CommandRegistry] Error: Invalid command name or handler\n";
        return false;
    }
    
    // Register command
    CommandInfo cmd{name, description, usage, handler};
    commands_[name] = cmd;
    
    return true;
}

CommandResult CommandRegistry::ExecuteCommand(
    const std::string& name,
    const std::vector<std::string>& args) {
    
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return CommandResult(false, "Command not found: " + name + " 'help' for available commands");
    }
    
    try {
        return it->second.handler(args);
    } catch (const std::exception& e) {
        return CommandResult(false, std::string("Error executing command: ") + e.what());
    }
}

std::vector<CommandInfo> CommandRegistry::GetAllCommands() const {
    // Phase 2: Modern C++26 ranges approach
    // Views lazily extract values from the map without intermediate allocation
    using namespace std::ranges;
    return commands_ 
        | views::values
        | to<std::vector>();
}

bool CommandRegistry::HasCommand(const std::string& name) const {
    // Phase 2: Modern ranges approach using any_of for semantic clarity
    // Equivalent to map::find but more expressive with algorithms
    return std::ranges::any_of(commands_, 
        [&name](const auto& pair) { return pair.first == name; });
}

const CommandInfo* CommandRegistry::GetCommandInfo(const std::string& name) const {
    auto it = commands_.find(name);
    if (it != commands_.end()) {
        return &it->second;
    }
    return nullptr;
}

void CommandRegistry::GenerateHelpText(Dashboard* dashboard) const {
    // Phase 2: Use ranges::for_each for explicit algorithm semantic
    // Clearer than traditional loop for side-effect operations
    std::ranges::for_each(commands_ | std::views::values,
        [dashboard](const auto& info) {
            dashboard->AddLog(info.name + " - " + info.description);
            dashboard->AddLog("   Usage: " + info.usage);
        });
}
