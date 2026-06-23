# Command Registry & Dashboard Abstraction - Quick Reference

**Status**: Architecture Analysis Complete  
**Updated**: May 10, 2026

---

## Problem Summary

```
Current State (Tightly Coupled):
┌─────────────────────────────────────┐
│ CommandPolicy                       │
├─────────────────────────────────────┤
│ private:                            │
│  - registry_: CommandRegistry       │
│  - command_thread_: thread          │
└──────────────┬──────────────────────┘
               │ creates
               ├──► CommandRegistry ──► GenerateHelpText(Dashboard*)
               │                           ❌ UI DEPENDENCY
               └──► DashboardCapability
                       ❌ NO LINK TO REGISTRY

Other Policies:
  ❌ Cannot extend commands
  ❌ Cannot customize command processing
  ❌ No registry access
```

---

## Solution Architecture (Proposed)

### Current → Target Evolution

**Current**:
```
CommandPolicy
  └─ CommandRegistry (private)
       └─ Commands map
            └─ Handler functions

❌ Problems:
  - Not accessible to other policies
  - Dashboard dependency in GenerateHelpText()
  - Command processing tied to CommandPolicy
  - No processor abstraction
```

**Phase 1 Target**:
```
CapabilityBus
  ├─ CommandRegistryCapability ✅ PUBLIC
  │   └─ CommandRegistry (shared)
  │        └─ Commands map
  │             └─ Handler functions
  │
  └─ [Other Capabilities]

✅ Benefits:
  - Any policy can register commands
  - CommandRegistry is UI-agnostic
  - Built-in command standardization
  - Foundation for Phase 2 processor
```

---

## Key Components

### 1. CommandRegistryCapability (Phase 1)

**Location**: `include/capabilities/CommandRegistryCapability.hpp`

**Responsibility**: Expose CommandRegistry through CapabilityBus

**Interface**:
```cpp
class CommandRegistryCapability {
    // Registration
    bool RegisterCommand(const std::string& name, 
                        const std::string& description,
                        const std::string& usage,
                        CommandHandler handler);
    
    // Execution
    CommandResult ExecuteCommand(const std::string& name,
                                const std::vector<std::string>& args);
    
    // Discovery
    bool HasCommand(const std::string& name) const;
    std::vector<CommandInfo> GetAllCommands() const;
    const CommandInfo* GetCommandInfo(const std::string& name) const;
};
```

**Thread Safety**: ✅ Full (mutex-protected)

---

### 2. CommandProcessorCapability (Phase 2)

**Location**: `include/capabilities/CommandProcessorCapability.hpp`

**Responsibility**: Abstract command parsing and processing

**Interface**:
```cpp
class ICommandProcessor {
    virtual CommandResult ProcessCommand(const std::string& raw) = 0;
    virtual CommandResult ProcessCommand(const std::string& name,
                                        const std::vector<std::string>& args) = 0;
};

class CommandProcessorCapability {
    std::shared_ptr<ICommandProcessor> processor;
    void SetProcessor(std::shared_ptr<ICommandProcessor> p);
};
```

**Benefits**: 
- ✅ Move command parsing out of CommandPolicy
- ✅ Allow custom processors
- ✅ Enable async execution patterns

---

### 3. CommandOutputCapability (Phase 3)

**Location**: `include/capabilities/CommandOutputCapability.hpp`

**Responsibility**: Abstraction for command output destination

**Interface**:
```cpp
class ICommandOutput {
    virtual void WriteMessage(const std::string& msg) = 0;
    virtual void WriteError(const std::string& error) = 0;
    virtual void WriteHelp(const std::vector<CommandInfo>& cmds) = 0;
};

class CommandOutputCapability {
    std::shared_ptr<ICommandOutput> output;
    void SetOutput(std::shared_ptr<ICommandOutput> o);
};
```

**Benefits**:
- ✅ Remove Dashboard dependency from CommandRegistry
- ✅ Support multiple output types (console, file, queue, logger)

---

### 4. DashboardPresenterCapability (Phase 4)

**Location**: `include/capabilities/DashboardPresenterCapability.hpp`

**Responsibility**: UI abstraction for multiple implementations

**Interface**:
```cpp
class IDashboardPresenter {
    virtual bool Initialize(const CapabilityBus& bus) = 0;
    virtual void OnMetricsEvent(const MetricsEvent& event) = 0;
    virtual void OnLogMessage(const std::string& msg) = 0;
    virtual void Render() = 0;
    virtual bool IsRunning() const = 0;
    virtual void RequestStop() = 0;
};

class DashboardPresenterCapability {
    std::shared_ptr<IDashboardPresenter> presenter;
    void SetPresenter(std::shared_ptr<IDashboardPresenter> p);
};
```

**Benefits**:
- ✅ Enable multiple UI implementations (Terminal, Web, CLI, Headless)
- ✅ Decouple UI from business logic
- ✅ Support testing without UI

---

## Execution Flow Comparison

### Current Flow

```
1. CommandPolicy::OnInit()
   └─ Creates CommandRegistry (private, not exposed)

2. CommandPolicy::OnStart()
   └─ Spawns command processing thread
       └─ Dequeues from DashboardCapability
           └─ Calls ExecuteCommand()
               └─ Parses string manually
                   └─ Calls handler via registry

3. Other Policies
   ❌ Cannot register commands
   ❌ Cannot access registry
```

### Proposed Flow (Phase 1+)

```
1. CommandPolicy::OnInit()
   └─ Creates CommandRegistryCapability
       └─ Registers in CapabilityBus
           └─ RegisterBuiltinCommands()

2. Other Policies::OnInit()
   └─ Get CommandRegistryCapability from bus
       └─ RegisterCommand() for custom commands ✅

3. CommandPolicy::OnStart()
   └─ Spawns command processing thread
       └─ Dequeues from DashboardCapability
           └─ [Phase 2] Uses CommandProcessorCapability
               └─ Parses and validates
                   └─ Calls handler via registry
                       └─ Enqueues result via DashboardCapability ✅

4. DashboardPolicy::Run()
   └─ Updates UI from DashboardCapability queues ✅
```

---

## Usage Examples

### Example 1: Register Built-in Command (CommandPolicy)

```cpp
bool CommandPolicy::OnInit(capabilities::GraphCapability &context) {
    // Create and register capability
    auto cmd_registry_cap = std::make_shared<CommandRegistryCapability>();
    context.GetCapabilityBus()
        .Register<CommandRegistryCapability>(cmd_registry_cap);
    
    // Register help command
    cmd_registry_cap->RegisterCommand(
        "help",
        "Display available commands",
        "help [command_name]",
        [cmd_registry_cap](const auto& args) {
            if (args.empty()) {
                auto cmds = cmd_registry_cap->GetAllCommands();
                std::string output = "Available commands:\n";
                for (const auto& cmd : cmds) {
                    output += "  " + cmd.name + " - " + cmd.description + "\n";
                }
                return CommandResult(true, output);
            } else {
                const auto* info = cmd_registry_cap->GetCommandInfo(args[0]);
                if (!info) return CommandResult(false, "Command not found");
                return CommandResult(true, 
                    fmt::format("Usage: {}\n{}", info->usage, info->description));
            }
        }
    );
    
    return true;
}
```

### Example 2: Custom Policy with Commands

```cpp
class MetricsPolicy : public graph::IExecutionPolicy {
    bool OnInit(capabilities::GraphCapability& context) override {
        auto cmd_registry = context.GetCapabilityBus()
            .Get<CommandRegistryCapability>();
        
        if (cmd_registry) {
            cmd_registry->RegisterCommand(
                "metrics_dump",
                "Dump current metrics to file",
                "metrics_dump [filename]",
                [this](const auto& args) {
                    std::string filename = args.empty() ? "metrics.json" : args[0];
                    DumpMetricsToFile(filename);
                    return CommandResult(true, 
                        fmt::format("Metrics dumped to {}", filename));
                }
            );
        }
        return true;
    }
};
```

### Example 3: Access All Commands (for autocompletion)

```cpp
// In Dashboard::HandleAutocompletion()
auto cmd_registry = context.GetCapabilityBus()
    .Get<CommandRegistryCapability>();

if (!cmd_registry) return;

auto all_cmds = cmd_registry->GetAllCommands();

// Filter by prefix (C++20 ranges)
auto matching = all_cmds
    | std::views::filter([prefix](const auto& cmd) {
        return app::ranges::StartsWithI(cmd.name, prefix);
    })
    | std::ranges::to<std::vector>();

// Display completions
for (const auto& cmd : matching) {
    DisplayCompletion(cmd.name);
}
```

---

## Phase Implementation Roadmap

```
┌─────────────────────────────────────────────────────────────┐
│ Phase 1: CommandRegistryCapability                          │
│ ✅ Expose registry through capability bus                    │
│ ✅ Allow custom command registration from policies           │
│ Timeline: 6-9 days                                           │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ Phase 2: CommandProcessorCapability                         │
│ ✅ Abstract command parsing and execution                    │
│ ✅ Move thread logic from CommandPolicy                      │
│ Timeline: 4-6 days                                           │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ Phase 3: CommandOutputCapability                            │
│ ✅ Remove Dashboard dependency from CommandRegistry          │
│ ✅ Support multiple output types                             │
│ Timeline: 3-4 days                                           │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ Phase 4: DashboardPresenterCapability                       │
│ ✅ Decouple Dashboard from policies                          │
│ ✅ Enable multiple UI implementations                        │
│ Timeline: 5-7 days                                           │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│ Phase 5: Thread Safety & Synchronization                    │
│ ✅ Ensure safe concurrent capability access                 │
│ ✅ Add backpressure handling                                 │
│ Timeline: 2-3 days                                           │
└─────────────────────────────────────────────────────────────┘

Total Estimated Timeline: 4-5 weeks (inclusive)
```

---

## Dependency Graph

```
Current Dependencies (Problematic):
CommandRegistry ──► Dashboard (via GenerateHelpText)
         ▲
         │
    CommandPolicy
         ▲
         │
    GraphExecutor

Proposed Dependencies (Clean):
CommandRegistryCapability ──┐
                            ├─► CapabilityBus
DashboardCapability ────────┤
                            │
DashboardPresenterCapability┘

CommandRegistry (UI-agnostic)
    └─► [No UI dependencies]
```

---

## Migration Checklist for Phase 1

### For Existing Code

- [ ] No changes required for most code
- [ ] CommandPolicy behavior unchanged (backward compatible)
- [ ] DashboardCapability still works as before
- [ ] Existing command execution paths unchanged

### For Extending Code

- [ ] Add CommandRegistryCapability reference to policy
- [ ] Call `context.GetCapabilityBus().Get<CommandRegistryCapability>()`
- [ ] Register custom commands in OnInit()
- [ ] Document command interface expectations

### For Testing

- [ ] Create CommandRegistryCapability unit tests
- [ ] Test command registration from multiple policies
- [ ] Verify thread safety under load
- [ ] Regression test all existing commands

---

## Quick Decision Matrix

| Need | Phase | Solution | Timeline |
|------|-------|----------|----------|
| Register custom commands | 1 | CommandRegistryCapability | NOW |
| Custom command processing | 2 | CommandProcessorCapability | +1 week |
| Remove Dashboard dependency | 3 | CommandOutputCapability | +2 weeks |
| Multiple UI implementations | 4 | DashboardPresenterCapability | +3 weeks |
| Concurrent safety guarantees | 5 | Synchronization layer | +4 weeks |

---

## Key Files Reference

### Analysis Documents
- **CAPABILITY_ABSTRACTION_ANALYSIS.md** - Comprehensive architecture analysis
- **PHASE_1_COMMANDREGISTRY_DESIGN.md** - Detailed Phase 1 design spec

### Current Code
- **include/ui/CommandRegistry.hpp** - Current registry implementation
- **include/policies/CommandPolicy.hpp** - Policy using registry
- **include/capabilities/DashboardCapability.hpp** - Current capability
- **include/graph/CapabilityBus.hpp** - Type-safe registry
- **include/graph/IExecutionPolicy.hpp** - Policy interface

### Implementation Files (To Create)
- **include/capabilities/CommandRegistryCapability.hpp** - Phase 1
- **src/capabilities/CommandRegistryCapability.cpp** - Phase 1
- **include/capabilities/CommandProcessorCapability.hpp** - Phase 2
- **include/capabilities/CommandOutputCapability.hpp** - Phase 3
- **include/capabilities/DashboardPresenterCapability.hpp** - Phase 4

---

## Success Metrics

### Phase 1 Completion
- ✅ CommandRegistryCapability compiles
- ✅ CommandPolicy registers capability
- ✅ One custom policy successfully registers commands
- ✅ All tests pass
- ✅ No performance regression

### Full Abstraction (Phase 1-4)
- ✅ Zero Dashboard dependencies in core capabilities
- ✅ Multiple UI implementations possible
- ✅ Custom command registration from any policy
- ✅ Testable without UI
- ✅ Documented with examples

---

## Common Questions

### Q: Will this break existing code?
**A**: Phase 1 is fully backward compatible. No existing code needs to change.

### Q: What if I don't register the capability?
**A**: Policies can check `if (cmd_registry)` and skip registration gracefully.

### Q: Can I use both old and new approaches?
**A**: Yes, during transition. CommandRegistry still works directly.

### Q: How does thread safety work?
**A**: Each capability has its own mutex for register/execute operations.

### Q: When do I need Phase 2?
**A**: When you want custom command processing logic or async execution.

### Q: Can I implement my own CommandOutputCapability?
**A**: Yes, and that's the whole point of Phase 3. Implement ICommandOutput.

---

## Next Steps

1. **Review** Analysis documents with team
2. **Approve** Phase 1 scope and timeline  
3. **Begin** Phase 1 implementation
4. **Validate** with integration tests
5. **Document** patterns and examples
6. **Plan** Phase 2 based on feedback

---

## Related Documentation

- [Architecture Analysis](CAPABILITY_ABSTRACTION_ANALYSIS.md)
- [Phase 1 Design Spec](PHASE_1_COMMANDREGISTRY_DESIGN.md)
- [Capability Bus Pattern](include/graph/CapabilityBus.hpp)
- [Execution Policy Interface](include/graph/IExecutionPolicy.hpp)
- [Command Registry](include/ui/CommandRegistry.hpp)
