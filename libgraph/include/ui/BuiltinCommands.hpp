/**
 * @file BuiltinCommands.hpp
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

#include "ui/CommandRegistry.hpp"
#include "capabilities/GraphCapability.hpp"
#include "capabilities/CommandRegistryCapability.hpp"
#include <memory>
#include <string>

namespace ui {

/**
 * @brief Register all built-in commands with the command registry capability
 *
 * Registers core execution commands via CommandRegistryCapability:
 * - help: Display available commands
 * - pause: Pause graph execution
 * - resume: Resume graph execution  
 * - stop: Stop graph execution
 * - status: Display execution status
 *
 * Thread-safe. All commands registered atomically via capability.
 *
 * @param cmd_registry CommandRegistryCapability to register commands with
 * @param graph_capability Optional graph capability used by execution commands
 * 
 * @see CommandRegistryCapability for thread-safety guarantees
 * @see CommandPolicy::OnInit() for call site
 *
 * **Phase 1 Implementation**: Uses CommandRegistryCapability pattern
 * (abstracted from Dashboard and direct CommandRegistry access)
 */
void RegisterBuiltinCommands(
    capabilities::CommandRegistryCapability& cmd_registry,
    std::shared_ptr<capabilities::GraphCapability> graph_capability = nullptr);

}  // namespace ui
