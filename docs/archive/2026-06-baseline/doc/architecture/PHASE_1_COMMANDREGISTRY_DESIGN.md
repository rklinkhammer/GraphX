# Phase 1: CommandRegistryCapability Design Specification
## Detailed Technical Design for Command Registry Abstraction

**Date**: May 10, 2026  
**Phase**: 1 of 5  
**Status**: Design Specification  
**Priority**: HIGH - Enables all subsequent abstractions

---

## Overview

Phase 1 introduces the **CommandRegistryCapability**, which wraps CommandRegistry and exposes it through the capability bus. This allows:

- Multiple policies to register custom commands
- Decoupling command management from CommandPolicy
- Foundation for Phase 2 (CommandProcessorCapability)
- Built-in command standardization

---

## Design Specifications

### 1. CommandRegistryCapability Header

**File**: `include/capabilities/CommandRegistryCapability.hpp`

```cpp
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "ui/CommandRegistry.hpp"

namespace capabilities {

/**
 * @class CommandRegistryCapability
 * @brief Capability wrapper for CommandRegistry with no Dashboard dependency
 *
 * Provides unified access to command registration and execution through
 * the CapabilityBus. Allows multiple policies to register custom commands
 * without direct dependencies on CommandRegistry.
 *
 * Thread Safety:
 * - RegisterCommand() is thread-safe (locks internal mutex)
 * - GetRegistry() returns const reference (read-only after initialization)
 * - Command execution via registry is thread-safe
 *
 * Lifecycle:
 * - Created in CommandPolicy::OnInit()
 * - Registered in CapabilityBus
 * - Available to all subsequent policies
 * - Destroyed with CapabilityBus cleanup
 *
 * Usage:
 * ```cpp
 * // In CommandPolicy::OnInit()
 * auto cmd_registry_cap = std::make_shared<CommandRegistryCapability>();
 * context.GetCapabilityBus()
 *     .Register<CommandRegistryCapability>(cmd_registry_cap);
 *
 * // In other policies::OnInit()
 * auto cmd_registry_cap = context.GetCapabilityBus()
 *     .Get<CommandRegistryCapability>();
 * if (cmd_registry_cap) {
 *     cmd_registry_cap->RegisterCommand(
 *         "my_command",
 *         "Description of my command",
 *         "Usage: my_command [args]",
 *         [](const auto& args) { return CommandResult(true, "OK"); }
 *     );
 * }
 * ```
 *
 * @see CommandRegistry, CommandResult, CommandInfo, IExecutionPolicy
 */
class CommandRegistryCapability {
public:
    /**
     * @brief Construct with an empty CommandRegistry
     */
    CommandRegistryCapability()
        : registry_(std::make_shared<CommandRegistry>()) {}

    /**
     * @brief Construct with an existing CommandRegistry
     *
     * @param registry Shared pointer to existing CommandRegistry instance
     * @throws std::invalid_argument if registry is nullptr
     */
    explicit CommandRegistryCapability(std::shared_ptr<CommandRegistry> registry)
        : registry_(registry) {
        if (!registry) {
            throw std::invalid_argument("CommandRegistry cannot be null");
        }
    }

    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~CommandRegistryCapability() = default;

    // ========================================================================
    // Command Registration
    // ========================================================================

    /**
     * @brief Register a new command with the registry
     *
     * Thread-safe wrapper around CommandRegistry::RegisterCommand().
     * Allows multiple policies to register commands without conflicts.
     *
     * @param name Command name (must be unique, case-sensitive)
     * @param description One-line description of what command does
     * @param usage Usage format string (e.g., "command [options] [args]")
     * @param handler Function to execute when command is invoked
     *
     * @return true if command registered successfully
     * @return false if command already exists or parameters invalid
     *
     * @note Name and description should not be empty
     * @note Handler should return valid CommandResult
     *
     * Example:
     * ```cpp
     * auto result = cmd_registry_cap->RegisterCommand(
     *     "pause",
     *     "Pause graph execution",
     *     "pause",
     *     [&graph](const auto& args) {
     *         graph->Pause();
     *         return CommandResult(true, "Graph paused");
     *     }
     * );
     * if (!result) {
     *     LOG_ERROR("Failed to register pause command");
     * }
     * ```
     *
     * @see CommandResult, CommandHandler, CommandInfo
     */
    bool RegisterCommand(
        const std::string& name,
        const std::string& description,
        const std::string& usage,
        CommandHandler handler) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->RegisterCommand(name, description, usage, handler);
    }

    // ========================================================================
    // Command Execution
    // ========================================================================

    /**
     * @brief Execute a registered command by name with parsed arguments
     *
     * Thread-safe execution of command handler.
     *
     * @param name Command name to execute
     * @param args Command arguments (may be empty)
     *
     * @return CommandResult with success status and output/error message
     *
     * Example:
     * ```cpp
     * auto result = cmd_registry_cap->ExecuteCommand("status", {});
     * if (result.success) {
     *     logger->info("Command output: {}", result.message);
     * } else {
     *     logger->error("Command failed: {}", result.message);
     * }
     * ```
     *
     * @see CommandResult, CommandHandler
     */
    CommandResult ExecuteCommand(
        const std::string& name,
        const std::vector<std::string>& args) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->ExecuteCommand(name, args);
    }

    // ========================================================================
    // Command Query & Discovery
    // ========================================================================

    /**
     * @brief Check if a command is registered
     *
     * @param name Command name to check
     * @return true if command exists, false otherwise
     */
    bool HasCommand(const std::string& name) const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->HasCommand(name);
    }

    /**
     * @brief Get metadata for a specific command
     *
     * @param name Command name to query
     * @return Pointer to CommandInfo if found, nullptr otherwise
     *
     * @note Returned pointer valid only during current call
     * @note Do not store returned pointer; copy data instead
     */
    const CommandInfo* GetCommandInfo(const std::string& name) const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->GetCommandInfo(name);
    }

    /**
     * @brief Get all registered commands
     *
     * Thread-safe snapshot of all commands at time of call.
     * Safe for use with C++20+ std::ranges operations.
     *
     * @return Vector of all CommandInfo structures
     *
     * Example with ranges (C++20):
     * ```cpp
     * auto all_cmds = cmd_registry_cap->GetAllCommands();
     * 
     * // Filter commands by prefix
     * auto matching = all_cmds
     *     | std::views::filter([prefix](const auto& cmd) {
     *         return app::ranges::StartsWithI(cmd.name, prefix);
     *     })
     *     | std::ranges::to<std::vector>();
     * ```
     *
     * @see RangesUtilities.hpp for helper functions
     */
    std::vector<CommandInfo> GetAllCommands() const {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        return registry_->GetAllCommands();
    }

    // ========================================================================
    // Registry Access
    // ========================================================================

    /**
     * @brief Get const reference to underlying CommandRegistry
     *
     * Provides direct access to CommandRegistry if needed.
     * Use this only for operations not exposed by this capability.
     *
     * @return Const reference to CommandRegistry
     *
     * @note For most operations, use this capability's methods instead
     * @note Safe to call after construction
     */
    const CommandRegistry& GetRegistry() const {
        return *registry_;
    }

    /**
     * @brief Get shared pointer to underlying CommandRegistry
     *
     * Returns the managed CommandRegistry instance.
     * Use for advanced operations or delegation to other components.
     *
     * @return Shared pointer to CommandRegistry
     *
     * @note Caller must not call Delete on returned pointer
     * @note Safe for thread-safe access via this capability's methods
     */
    std::shared_ptr<CommandRegistry> GetRegistryPtr() const {
        return registry_;
    }

    // ========================================================================
    // Capability Metadata
    // ========================================================================

    /**
     * @brief Get human-readable capability name
     *
     * @return String identifier for logging/debugging
     */
    static constexpr const char* GetCapabilityName() {
        return "CommandRegistryCapability";
    }

    /**
     * @brief Check if capability is ready for use
     *
     * @return true if CommandRegistry is initialized, false otherwise
     */
    bool IsReady() const {
        return registry_ != nullptr;
    }

private:
    /// Underlying CommandRegistry instance
    std::shared_ptr<CommandRegistry> registry_;

    /// Synchronization for thread-safe operations
    mutable std::mutex registry_mutex_;
};

}  // namespace capabilities
```

---

### 2. Updated CommandPolicy

**File**: `include/policies/CommandPolicy.hpp`

**Key Changes**:
- Register CommandRegistryCapability in OnInit()
- Expose registry for built-in commands registration
- Document the new capability-based approach

```cpp
// In CommandPolicy::OnInit()
bool CommandPolicy::OnInit(capabilities::GraphCapability &context) {
    LOG4CXX_TRACE(command_logger, "CommandPolicy OnInit called");
    
    // Create and register CommandRegistryCapability
    auto cmd_registry_cap = std::make_shared<capabilities::CommandRegistryCapability>();
    context.GetCapabilityBus()
        .Register<capabilities::CommandRegistryCapability>(cmd_registry_cap);
    
    // Store for later use (built-in commands registration, etc.)
    cmd_registry_cap_ = cmd_registry_cap;
    
    // Register built-in commands
    RegisterBuiltinCommands(context);
    
    return true;
}

private:
    std::shared_ptr<capabilities::CommandRegistryCapability> cmd_registry_cap_;
    
    void RegisterBuiltinCommands(capabilities::GraphCapability &context) {
        if (!cmd_registry_cap_) return;
        
        // Help command
        cmd_registry_cap_->RegisterCommand(
            "help",
            "Display available commands",
            "help [command_name]",
            [this](const auto& args) { return OnHelpCommand(args); }
        );
        
        // Status command
        cmd_registry_cap_->RegisterCommand(
            "status",
            "Display current graph execution status",
            "status",
            [this](const auto& args) { return OnStatusCommand(args); }
        );
        
        // ... other built-in commands
    }
};
```

---

### 3. Built-in Commands Registration

**File**: `include/ui/BuiltinCommands.hpp`

**Key Changes**:
- Refactor to use CommandRegistryCapability instead of Dashboard*
- Make registration a standalone function

```cpp
namespace ui {

/**
 * @brief Register built-in dashboard commands
 *
 * Registers standard commands (help, status, pause, resume, etc.) with
 * the CommandRegistryCapability. Called by CommandPolicy::OnInit().
 *
 * @param cmd_registry_cap CommandRegistryCapability to register commands with
 * @param graph_capability GraphCapability for command implementation context
 *
 * Example:
 * ```cpp
 * auto cmd_reg = context.GetCapabilityBus()
 *     .Get<CommandRegistryCapability>();
 * RegisterBuiltinCommands(cmd_reg, graph_capability);
 * ```
 *
 * @see CommandRegistryCapability, GraphCapability
 */
void RegisterBuiltinCommands(
    std::shared_ptr<capabilities::CommandRegistryCapability> cmd_registry_cap,
    std::shared_ptr<capabilities::GraphCapability> graph_capability);

}  // namespace ui
```

**Implementation Example**:

```cpp
// src/ui/BuiltinCommands.cpp
void RegisterBuiltinCommands(
    std::shared_ptr<capabilities::CommandRegistryCapability> cmd_registry_cap,
    std::shared_ptr<capabilities::GraphCapability> graph_capability) {
    
    if (!cmd_registry_cap || !cmd_registry_cap->IsReady()) {
        LOG4CXX_WARN(command_logger, "CommandRegistryCapability not ready");
        return;
    }
    
    auto logger = log4cxx::Logger::getLogger("app.ui.BuiltinCommands");
    
    // HELP command
    cmd_registry_cap->RegisterCommand(
        "help",
        "Display available commands",
        "help [command_name]",
        [cmd_registry_cap, logger](const auto& args) -> CommandResult {
            if (args.size() > 1) {
                // Show help for specific command
                const auto* info = cmd_registry_cap->GetCommandInfo(args[1]);
                if (!info) {
                    return CommandResult(false, "Command not found: " + args[1]);
                }
                std::string help = fmt::format(
                    "Command: {}\nDescription: {}\nUsage: {}",
                    info->name, info->description, info->usage
                );
                return CommandResult(true, help);
            } else {
                // List all commands
                auto all = cmd_registry_cap->GetAllCommands();
                std::string output = fmt::format("Available commands ({} total):\n", all.size());
                for (const auto& cmd : all) {
                    output += fmt::format("  {} - {}\n", cmd.name, cmd.description);
                }
                return CommandResult(true, output);
            }
        }
    );
    
    // STATUS command
    cmd_registry_cap->RegisterCommand(
        "status",
        "Display graph execution status",
        "status",
        [graph_capability, logger](const auto& args) -> CommandResult {
            if (!graph_capability) {
                return CommandResult(false, "Graph capability not available");
            }
            
            bool stopped = graph_capability->IsStopped();
            std::string status = stopped ? "STOPPED" : "RUNNING";
            return CommandResult(true, fmt::format("Graph Status: {}", status));
        }
    );
    
    // ... additional built-in commands (pause, resume, etc.)
    
    LOG4CXX_INFO(logger, "Registered {} built-in commands", 
                 cmd_registry_cap->GetAllCommands().size());
}
```

---

### 4. CustomPolicy Example

**File**: `docs/examples/custom_policy_with_commands.cpp`

**Demonstrates**:
- How to register custom commands from a policy
- Accessing other capabilities from command handlers
- Error handling and result reporting

```cpp
#include "graph/IExecutionPolicy.hpp"
#include "capabilities/CommandRegistryCapability.hpp"
#include "capabilities/GraphCapability.hpp"

class CustomMetricsPolicy : public graph::IExecutionPolicy {
public:
    virtual ~CustomMetricsPolicy() = default;
    
    bool OnInit(capabilities::GraphCapability& context) override {
        LOG4CXX_TRACE(custom_logger, "CustomMetricsPolicy::OnInit()");
        
        // Get CommandRegistryCapability
        auto cmd_registry = context.GetCapabilityBus()
            .Get<capabilities::CommandRegistryCapability>();
        
        if (!cmd_registry) {
            LOG4CXX_WARN(custom_logger, "CommandRegistryCapability not found");
            return true;  // Not fatal - continue without custom commands
        }
        
        // Register custom commands
        bool success = cmd_registry->RegisterCommand(
            "metrics_reset",
            "Reset all metrics counters",
            "metrics_reset",
            [this, &context](const std::vector<std::string>& args) {
                return OnMetricsResetCommand(context, args);
            }
        );
        
        if (success) {
            LOG4CXX_INFO(custom_logger, "Registered custom command: metrics_reset");
        } else {
            LOG4CXX_WARN(custom_logger, "Failed to register metrics_reset command");
        }
        
        return true;
    }

private:
    static auto custom_logger = log4cxx::Logger::getLogger("app.CustomMetricsPolicy");
    
    CommandResult OnMetricsResetCommand(
        capabilities::GraphCapability& context,
        const std::vector<std::string>& args) {
        
        try {
            // Access metrics capability if available
            auto metrics_cap = context.GetCapabilityBus()
                .Get<capabilities::MetricsCapability>();
            
            if (!metrics_cap) {
                return CommandResult(false, "MetricsCapability not available");
            }
            
            // Perform reset operation (pseudo-code)
            // metrics_cap->ResetAllMetrics();
            
            return CommandResult(true, "Metrics counters reset successfully");
            
        } catch (const std::exception& ex) {
            return CommandResult(false, 
                fmt::format("Error resetting metrics: {}", ex.what()));
        }
    }
};
```

---

## Implementation Checklist

### Code Changes
- [ ] Create `include/capabilities/CommandRegistryCapability.hpp`
- [ ] Update `include/policies/CommandPolicy.hpp`
  - [ ] Add CommandRegistryCapability registration
  - [ ] Add command_registry_cap_ member
  - [ ] Update OnInit() to register capability
  - [ ] Call RegisterBuiltinCommands()
- [ ] Refactor `include/ui/BuiltinCommands.hpp`
  - [ ] Remove Dashboard* parameter
  - [ ] Add CommandRegistryCapability parameter
  - [ ] Update all command registrations
- [ ] Create `src/ui/BuiltinCommands.cpp` (if not exists)
  - [ ] Implement RegisterBuiltinCommands function
  - [ ] Implement each built-in command handler

### Testing
- [ ] Unit tests for CommandRegistryCapability
  - [ ] RegisterCommand() success/failure
  - [ ] ExecuteCommand() with valid/invalid commands
  - [ ] GetAllCommands() returns correct list
  - [ ] Thread safety under concurrent access
- [ ] Integration tests
  - [ ] CommandPolicy registers capability in OnInit()
  - [ ] Other policies can register custom commands
  - [ ] Command execution flows through capability
  - [ ] Built-in commands work as expected
- [ ] Regression tests
  - [ ] Existing CommandRegistry tests still pass
  - [ ] Dashboard still receives command results
  - [ ] CommandPolicy thread handling works

### Documentation
- [ ] Update API documentation
- [ ] Create example: CustomPolicy with commands
- [ ] Update migration guide for existing code
- [ ] Add doxygen comments to all public methods

### Code Review Checklist
- [ ] Thread safety verified (mutex protection)
- [ ] No circular dependencies
- [ ] Proper error handling
- [ ] Exception safety (RAII, strong guarantees)
- [ ] Memory management (shared_ptr correct usage)
- [ ] Logging at appropriate levels
- [ ] Documentation complete and accurate

---

## Migration Path for Existing Code

### Current Usage (CommandPolicy)

```cpp
// Before: Direct access to registry_
class CommandPolicy : public graph::IExecutionPolicy {
private:
    std::shared_ptr<CommandRegistry> registry_;  // Private member
};

// Usage in ExecuteCommand():
if (!registry_) {
    dashboard_capability_->AddLog("[ERROR] Command registry not initialized");
}
```

### New Usage (Phase 1)

```cpp
// After: Via capability
class CommandPolicy : public graph::IExecutionPolicy {
private:
    std::shared_ptr<capabilities::CommandRegistryCapability> cmd_registry_cap_;
};

// Usage in ExecuteCommand():
auto cmd_registry = context.GetCapabilityBus()
    .Get<capabilities::CommandRegistryCapability>();
if (!cmd_registry) {
    dashboard_capability_->AddLog("[ERROR] Command registry not available");
}
```

---

## Backward Compatibility

**Phase 1 does NOT break existing code**:

1. CommandRegistry interface unchanged
2. CommandPolicy still manages command execution
3. DashboardCapability still works unchanged
4. Only addition is new capability registration

**Future Migration** (Phase 2+):
- CommandPolicy command thread logic can move to CommandProcessorCapability
- Command parsing can be abstracted
- These changes can be optional/backward-compatible

---

## Performance Considerations

### Overhead
- **Mutex lock per command registration**: Negligible (happens at startup)
- **Capability lookup**: O(1) with type_index map
- **Command execution**: No additional overhead vs current approach

### Optimization Opportunities
- Cache capability pointers in policies (avoid repeated lookups)
- Batch command registration if registering many commands
- Use lock_guard with minimal critical sections

### Metrics
Current implementation should show:
- Command registration: < 1ms
- Command execution: Unchanged from CommandRegistry
- Capability bus lookup: < 1μs

---

## Risk Analysis

| Risk | Severity | Probability | Mitigation |
|------|----------|-------------|-----------|
| Deadlock from mutex | Medium | Low | Use lock_guard, verify no nested locks |
| Capability not registered | Low | Medium | Check capability availability before use |
| Breaking existing policies | Medium | Medium | Maintain backward compatibility |
| Thread safety issues | High | Low | Comprehensive mutex protection |

---

## Success Criteria

Phase 1 is complete when:

1. ✅ CommandRegistryCapability compiles and links
2. ✅ CommandPolicy registers capability in OnInit()
3. ✅ Other policies can register commands via capability
4. ✅ All existing command functionality preserved
5. ✅ Unit tests pass (new + existing)
6. ✅ Integration tests pass (command execution flow)
7. ✅ No performance degradation vs current approach
8. ✅ Thread safety verified under load
9. ✅ Documentation complete

---

## Timeline Estimate

| Task | Estimate | Dependencies |
|------|----------|--------------|
| Design review | 1 day | - |
| Implementation | 2-3 days | Design |
| Unit testing | 1-2 days | Implementation |
| Integration testing | 1-2 days | Unit tests |
| Documentation | 1 day | All tests pass |
| **Total** | **6-9 days** | - |

---

## References

- Current CommandRegistry: `include/ui/CommandRegistry.hpp`
- Current CommandPolicy: `include/policies/CommandPolicy.hpp`
- Current BuiltinCommands: `include/ui/BuiltinCommands.hpp`
- CapabilityBus: `include/graph/CapabilityBus.hpp`
- IExecutionPolicy: `include/graph/IExecutionPolicy.hpp`

---

## Appendix: Full CommandRegistryCapability Header

[See Section 1 above for complete header file]
