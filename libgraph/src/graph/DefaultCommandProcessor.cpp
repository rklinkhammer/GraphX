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

/**
 * @file DefaultCommandProcessor.cpp
 * @brief Implementation of default command processor
 *
 * Phase 2: CommandProcessorCapability - First concrete processor implementation.
 * Wraps CommandRegistryCapability to provide ICommandProcessor interface.
 */

#include "graph/DefaultCommandProcessor.hpp"
#include <sstream>
#include <algorithm>

namespace graph {

DefaultCommandProcessor::DefaultCommandProcessor(
    std::shared_ptr<capabilities::CommandRegistryCapability> registry)
    : registry_(registry) {
    if (!registry || !registry->IsReady()) {
        throw std::invalid_argument(
            "DefaultCommandProcessor: registry must be non-null and ready");
    }
}

CommandResult DefaultCommandProcessor::ProcessCommand(
    const std::string& raw_command) {
    
    // Parse raw command string into name and arguments
    std::string name;
    std::vector<std::string> args;
    
    if (!ParseCommand(raw_command, name, args)) {
        return CommandResult(false, "Empty command");
    }
    
    // Execute via pre-parsed variant
    return ProcessCommand(name, args);
}

CommandResult DefaultCommandProcessor::ProcessCommand(
    const std::string& name,
    const std::vector<std::string>& args) {
    
    // Validate command exists
    if (!registry_->HasCommand(name)) {
        return CommandResult(false, "Unknown command: " + name);
    }
    
    // Execute command via registry
    return registry_->ExecuteCommand(name, args);
}

bool DefaultCommandProcessor::HasCommand(const std::string& name) const {
    return registry_->HasCommand(name);
}

const CommandInfo* DefaultCommandProcessor::GetCommandInfo(
    const std::string& name) const {
    return registry_->GetCommandInfo(name);
}

std::vector<CommandInfo> DefaultCommandProcessor::GetAllCommands() const {
    return registry_->GetAllCommands();
}

bool DefaultCommandProcessor::ParseCommand(
    const std::string& raw_command,
    std::string& out_name,
    std::vector<std::string>& out_args) const {
    
    // Clear outputs
    out_name.clear();
    out_args.clear();
    
    // Use stringstream for simple whitespace-based tokenization
    std::istringstream iss(raw_command);
    std::string token;
    
    // Extract first token (command name)
    if (!(iss >> token)) {
        return false;  // Empty command
    }
    
    out_name = token;
    
    // Extract remaining tokens (arguments)
    while (iss >> token) {
        out_args.push_back(token);
    }
    
    return true;
}

}  // namespace graph
