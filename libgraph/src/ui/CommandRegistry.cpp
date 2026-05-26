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
#include "capabilities/ICommandOutput.hpp"

bool CommandRegistry::RegisterCommand(
    const std::string& name,
    const std::string& description,
    const std::string& usage,
    CommandHandler handler) {
    
    if (commands_.find(name) != commands_.end()) {
        return false;  // Command already exists
    }
    
    CommandInfo info{name, description, usage, handler};
    commands_[name] = info;
    return true;
}

CommandResult CommandRegistry::ExecuteCommand(
    const std::string& name,
    const std::vector<std::string>& args) {
    
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return CommandResult(false, "Command not found: " + name);
    }
    
    try {
        std::vector<std::string> full_args{name};
        full_args.insert(full_args.end(), args.begin(), args.end());
        return it->second.handler(full_args);
    } catch (const std::exception& e) {
        return CommandResult(false, std::string("Command execution error: ") + e.what());
    }
}

std::vector<CommandInfo> CommandRegistry::GetAllCommands() const {
    std::vector<CommandInfo> result;
    result.reserve(commands_.size());
    
    for (const auto& [name, info] : commands_) {
        result.push_back(info);
    }
    
    return result;
}

bool CommandRegistry::HasCommand(const std::string& name) const {
    return commands_.find(name) != commands_.end();
}

const CommandInfo* CommandRegistry::GetCommandInfo(const std::string& name) const {
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return nullptr;
    }
    return &it->second;
}

void CommandRegistry::GenerateHelpText(capabilities::ICommandOutput* output) const {
    if (!output) return;
    
    // Get all commands and write help using abstracted output
    auto all_cmds = GetAllCommands();
    output->WriteHelp(all_cmds);
}
