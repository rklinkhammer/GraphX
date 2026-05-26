# Capability & Policy Infrastructure Analysis
## Command Registry & Dashboard Abstraction

**Date**: May 10, 2026  
**Status**: Comprehensive Architecture Analysis  
**Focus**: Abstracting CommandRegistry and Dashboard Policy/Capability

---

## Executive Summary

The GraphX framework has a well-structured capability and policy infrastructure:

- **CapabilityBus**: Type-indexed registry for inter-component communication
- **Capabilities**: UI-agnostic components (GraphCapability, MetricsCapability, DashboardCapability, CommandCapability, DataInjectionCapability)
- **Policies**: Execution lifecycle hooks (MetricsPolicy, CommandPolicy, DashboardPolicy, DataInjectionPolicy)
- **CommandRegistry**: UI command handler registry with built-in commands
- **Dashboard**: Terminal UI using ncurses/ftxui

Currently, there are **tight coupling points** that limit extensibility:
1. CommandRegistry depends on Dashboard for some operations
2. CommandPolicy tightly binds to CommandRegistry implementation
3. DashboardPolicy has commented-out Dashboard code due to incomplete abstraction
4. CommandRegistry usage scattered in policies without clear abstraction

---

## Current Architecture

### 1. Capability Bus (Type-Safe Registry)

**File**: `include/graph/CapabilityBus.hpp`

```cpp
class CapabilityBus {
    template<typename CapabilityT>
    void Register(std::shared_ptr<CapabilityT> capability);
    
    template<typename CapabilityT>
    std::shared_ptr<CapabilityT> Get() const;
    
    template<typename CapabilityT>
    bool Has() const;
    
    void Clear();
};
```

**Characteristics**:
- Type-indexed registry using `std::type_index`
- Stored as `std::map<std::type_index, std::shared_ptr<void>>`
- Static cast on retrieval
- Thread-unsafe (no synchronization)

**Current Capabilities**:
1. **GraphCapability** (graph/capabilities/GraphCapability.hpp)
   - Stores GraphManager, PluginRegistry, NodeFactory
   - Maintains node names and edge descriptions
   - Provides capability bus reference

2. **MetricsCapability** (capabilities/MetricsCapability.hpp)
   - Metrics schema discovery
   - Metrics event publishing
   - Callback-based subscriber notification

3. **DashboardCapability** (capabilities/DashboardCapability.hpp)
   - Command queue (UI → Business Logic)
   - Log queue (Business Logic → UI)
   - DEPRECATED: No longer stores Dashboard reference

4. **CommandCapability** (capabilities/CommandCapability.hpp)
   - Command queue for processing
   - Result queue for responses
   - Simpler than DashboardCapability (split responsibility)

5. **DataInjectionCapability** (capabilities/DataInjectionCapability.hpp)
   - Data injection node discovery
   - Per-node injection queue management

---

### 2. Policies (Execution Lifecycle Hooks)

**File**: `include/graph/IExecutionPolicy.hpp`

```cpp
struct IExecutionPolicy {
    virtual bool OnInit(capabilities::GraphCapability& context) { return true; }
    virtual bool OnStart(capabilities::GraphCapability& context) { return true; }
    virtual bool OnRun(capabilities::GraphCapability& context) { return true; }
    virtual void OnStop(capabilities::GraphCapability& context) {}
    virtual void OnJoin(capabilities::GraphCapability& context) {}
};
```

**Execution Flow**:
```
GraphExecutor.Init()  → OnInit() for each policy
GraphExecutor.Start() → OnStart() for each policy
GraphExecutor.Run()   → OnRun() for each policy (optional)
GraphExecutor.Stop()  → OnStop() for each policy
GraphExecutor.Join()  → OnJoin() for each policy
```

**Policy Chain Pattern**:
- ExecutionPolicyChain links policies in sequence
- Each policy can enable/disable succeeding ones
- Policies have no control over order (managed by executor)

---

### 3. CommandRegistry & CommandPolicy

**File**: `include/ui/CommandRegistry.hpp` & `include/policies/CommandPolicy.hpp`

#### CommandRegistry

```cpp
struct CommandInfo {
    std::string name;
    std::string description;
    std::string usage;
    CommandHandler handler;  // std::function<CommandResult(const std::vector<std::string>&)>
};

class CommandRegistry {
    bool RegisterCommand(const std::string& name, const std::string& description, 
                        const std::string& usage, CommandHandler handler);
    CommandResult ExecuteCommand(const std::string& name, const std::vector<std::string>& args);
    std::vector<CommandInfo> GetAllCommands() const;
    bool HasCommand(const std::string& name) const;
    const CommandInfo* GetCommandInfo(const std::string& name) const;
    void GenerateHelpText(Dashboard* dashboard) const;
};
```

**Issues**:
- `GenerateHelpText()` takes `Dashboard*` parameter (UI dependency)
- No abstraction for command execution context
- Handler requires full argument parsing at call site

#### CommandPolicy

```cpp
class CommandPolicy : public graph::IExecutionPolicy {
    bool OnInit(capabilities::GraphCapability &context) override {
        registry_ = std::make_shared<CommandRegistry>();
        return true;
    }
    
    bool OnStart(capabilities::GraphCapability &context) override {
        dashboard_capability_ = context.GetCapabilityBus().Get<capabilities::DashboardCapability>();
        // Spawn command processing thread
        command_thread_ = std::thread([this]() {
            std::string command;
            while (dashboard_capability_->DequeueCommand(command)) {
                ExecuteCommand(command);
            }
        });
        return true;
    }
    
    void ExecuteCommand(const std::string& cmd);
};
```

**Issues**:
- CommandRegistry created but not exposed to other policies
- Command processing thread logic mixed with queue handling
- No standard for built-in command registration
- ExecuteCommand parses string but doesn't validate command exists

---

### 4. DashboardPolicy & DashboardCapability

**File**: `include/policies/DashboardPolicy.hpp` & `include/capabilities/DashboardCapability.hpp`

**Current State**: Mostly commented-out code due to incomplete abstraction

```cpp
class DashboardPolicy : public graph::IExecutionPolicy {
    bool OnInit(capabilities::GraphCapability &context) override {
        // Create DashboardCapability
        auto dashboard_capability = std::make_shared<capabilities::DashboardCapability>();
        context.GetCapabilityBus().Register<capabilities::DashboardCapability>(dashboard_capability);
        
        // TODO: Create Dashboard instance
        // dashboard_ = std::make_shared<Dashboard>(...);
        return true;
    }
    
    bool OnStart(capabilities::GraphCapability &) override {
        // TODO: Spawn Dashboard UI thread
        // dashboard_thread_ = std::thread([this]() { dashboard_->Run(); });
        return true;
    }
};
```

**Issues**:
- Dashboard class not properly integrated
- Command execution path unclear
- Metrics integration incomplete
- UI rendering decoupled from data updates

---

## Abstraction Opportunities

### Problem 1: CommandRegistry UI Dependency

**Current Issue**: 
```cpp
void GenerateHelpText(Dashboard* dashboard) const;  // ❌ Dashboard dependency
```

**Solution**: Create abstraction for output destination

```cpp
// New abstraction in capabilities/CommandOutputCapability.hpp
class ICommandOutput {
    virtual void WriteMessage(const std::string& msg) = 0;
    virtual void WriteError(const std::string& error) = 0;
    virtual void WriteHelp(const std::vector<CommandInfo>& commands) = 0;
};

class CommandRegistry {
    void GenerateHelpText(ICommandOutput& output) const;  // ✅ Generic
};
```

---

### Problem 2: CommandRegistry Not Accessible to Other Policies

**Current Issue**:
- CommandRegistry created in CommandPolicy but not registered
- Other policies can't discover or extend commands
- No built-in command standard

**Solution**: Create CommandRegistryCapability

```cpp
// capabilities/CommandRegistryCapability.hpp
class CommandRegistryCapability {
    std::shared_ptr<CommandRegistry> registry;
    
    void RegisterCommand(const std::string& name, ..., CommandHandler handler);
    CommandResult ExecuteCommand(const std::string& name, const std::vector<std::string>& args);
    const CommandRegistry& GetRegistry() const;
};

// CommandPolicy registers it:
bool CommandPolicy::OnInit(capabilities::GraphCapability &context) {
    auto cmd_reg_cap = std::make_shared<CommandRegistryCapability>();
    context.GetCapabilityBus().Register<CommandRegistryCapability>(cmd_reg_cap);
    return true;
}

// Other policies can use it:
class CustomPolicy : public IExecutionPolicy {
    bool OnInit(capabilities::GraphCapability &context) override {
        auto cmd_reg = context.GetCapabilityBus().Get<CommandRegistryCapability>();
        if (cmd_reg) {
            cmd_reg->RegisterCommand("custom", "Custom command", "custom [args]",
                [this](const auto& args) { return OnCustomCommand(args); });
        }
        return true;
    }
};
```

---

### Problem 3: Dashboard Policy Incomplete & Decoupled

**Current Issue**:
- Dashboard creation commented out
- Command execution path unclear
- Metrics integration partial
- No clear lifecycle management

**Solution**: Create DashboardPresenterCapability abstraction

```cpp
// capabilities/DashboardPresenterCapability.hpp
class IDashboardPresenter {
    virtual void Initialize(const MetricsCapability& metrics) = 0;
    virtual void OnMetricsEvent(const MetricsEvent& event) = 0;
    virtual void OnLogMessage(const std::string& message) = 0;
    virtual void OnCommandResult(const CommandResult& result) = 0;
    virtual void Render() = 0;
    virtual bool IsRunning() const = 0;
    virtual void RequestStop() = 0;
};

class DashboardPresenterCapability {
    std::shared_ptr<IDashboardPresenter> presenter;
    
    void SetPresenter(std::shared_ptr<IDashboardPresenter> p);
    std::shared_ptr<IDashboardPresenter> GetPresenter();
};
```

---

### Problem 4: Command Processing Logic Tightly Coupled to Threading

**Current Issue**:
```cpp
bool OnStart(capabilities::GraphCapability &context) override {
    command_thread_ = std::thread([this, ...]() {
        std::string command;
        while (dashboard_capability_->DequeueCommand(command)) {
            ExecuteCommand(command);
        }
    });
    return true;
}
```

**Solution**: Create abstraction for command processing

```cpp
// capabilities/CommandProcessorCapability.hpp
class ICommandProcessor {
    virtual CommandResult ProcessCommand(const std::string& raw_command) = 0;
    virtual CommandResult ProcessCommand(const std::string& name, 
                                         const std::vector<std::string>& args) = 0;
};

class CommandProcessorCapability {
    std::shared_ptr<ICommandProcessor> processor;
    void SetProcessor(std::shared_ptr<ICommandProcessor> p);
};

// Usage in policy:
class CommandPolicy : public IExecutionPolicy {
    bool OnStart(capabilities::GraphCapability &context) override {
        auto cmd_proc_cap = std::make_shared<CommandProcessorCapability>();
        auto proc = std::make_shared<DefaultCommandProcessor>(registry_);
        cmd_proc_cap->SetProcessor(proc);
        context.GetCapabilityBus().Register<CommandProcessorCapability>(cmd_proc_cap);
        
        auto dashboard_cap = context.GetCapabilityBus().Get<DashboardCapability>();
        command_thread_ = std::thread([cmd_proc_cap, dashboard_cap]() {
            std::string command;
            while (dashboard_cap->DequeueCommand(command)) {
                auto result = cmd_proc_cap->GetProcessor()->ProcessCommand(command);
                dashboard_cap->AddLog(result.message);
            }
        });
        return true;
    }
};
```

---

## Proposed Abstraction Architecture

### Layer 1: Core Capabilities (UI-Agnostic)

```
CapabilityBus (TypeIndex Registry)
    ├── GraphCapability (graph state, managers)
    ├── MetricsCapability (metrics discovery & events)
    ├── DataInjectionCapability (data injection nodes)
    ├── DashboardCapability (command & log queues)
    ├── CommandRegistryCapability (command registration & dispatch)
    ├── CommandProcessorCapability (command parsing & execution)
    ├── CommandOutputCapability (command output abstraction)
    └── DashboardPresenterCapability (UI abstraction)
```

### Layer 2: Policies (Lifecycle Management)

```
ExecutionPolicyChain
    ├── MetricsPolicy (metrics collection)
    ├── DataInjectionPolicy (data injection setup)
    ├── CommandPolicy (command registry & processor setup)
    ├── DashboardPolicy (presenter initialization & threading)
    └── CustomPolicies (application-specific)
```

### Layer 3: Implementations (Concrete)

```
UI Adapters (pluggable)
    ├── TerminalDashboard (ncurses/ftxui)
    ├── WebDashboard (HTTP/WebSocket)
    ├── CLIDashboard (REPL)
    └── NullDashboard (headless)

Command Processor Implementations
    ├── DefaultCommandProcessor (built-in commands)
    ├── CustomCommandProcessor (plugins)
    └── CompositeCommandProcessor (chaining)

Output Adapters
    ├── ConsoleOutput
    ├── FileOutput
    ├── QueueOutput
    └── LoggerOutput
```

---

## Implementation Roadmap

### Phase 1: Create Command Registry Capability ✅ RECOMMENDED FIRST

**Goal**: Make CommandRegistry accessible to all policies

**Steps**:
1. Create `capabilities/CommandRegistryCapability.hpp`
2. Wrap CommandRegistry in capability
3. Update CommandPolicy to register capability
4. Update BuiltinCommands to use capability-based approach
5. Add examples for custom command registration from other policies

**Impact**: Low risk, enables further abstractions

---

### Phase 2: Create Command Processor Abstraction

**Goal**: Decouple command parsing from execution

**Steps**:
1. Create `capabilities/CommandProcessorCapability.hpp`
2. Define `ICommandProcessor` interface
3. Implement `DefaultCommandProcessor` wrapping CommandRegistry
4. Move thread logic from CommandPolicy to CommandProcessor
5. Support command result callbacks

**Impact**: Enables async command execution, custom processors

---

### Phase 3: Create Command Output Abstraction

**Goal**: Remove Dashboard dependency from CommandRegistry

**Steps**:
1. Create `capabilities/CommandOutputCapability.hpp`
2. Define `ICommandOutput` interface
3. Update CommandRegistry to use ICommandOutput
4. Implement concrete outputs (console, log, queue, etc.)
5. Remove `GenerateHelpText(Dashboard*)` overload

**Impact**: CommandRegistry becomes UI-agnostic

---

### Phase 4: Create Dashboard Presenter Abstraction

**Goal**: Enable multiple UI implementations

**Steps**:
1. Create `capabilities/DashboardPresenterCapability.hpp`
2. Define `IDashboardPresenter` interface
3. Refactor Dashboard into TerminalDashboardPresenter
4. Update DashboardPolicy to use capability
5. Add support for pluggable presenters

**Impact**: Enables web, CLI, headless modes

---

### Phase 5: Thread Safety & Synchronization

**Goal**: Ensure safe concurrent access to capabilities

**Steps**:
1. Add mutex protection to CapabilityBus (optional, depends on usage)
2. Document thread safety guarantees for each capability
3. Add queue capacity limits and backpressure handling
4. Add metrics for queue sizes and processing delays

**Impact**: Production-ready concurrent access

---

## Abstraction Interfaces to Define

### 1. ICommandOutput

```cpp
namespace capabilities {
class ICommandOutput {
public:
    virtual ~ICommandOutput() = default;
    virtual void WriteMessage(const std::string& message) = 0;
    virtual void WriteError(const std::string& error) = 0;
    virtual void WriteWarning(const std::string& warning) = 0;
    virtual void WriteHelp(const std::vector<CommandInfo>& commands) = 0;
};
}
```

---

### 2. ICommandProcessor

```cpp
namespace capabilities {
class ICommandProcessor {
public:
    virtual ~ICommandProcessor() = default;
    
    // Raw command string processing
    virtual CommandResult ProcessCommand(const std::string& raw_command) = 0;
    
    // Pre-parsed command processing
    virtual CommandResult ProcessCommand(const std::string& name, 
                                         const std::vector<std::string>& args) = 0;
    
    // Query available commands
    virtual bool HasCommand(const std::string& name) const = 0;
    virtual const CommandInfo* GetCommandInfo(const std::string& name) const = 0;
    virtual std::vector<CommandInfo> GetAllCommands() const = 0;
};
}
```

---

### 3. IDashboardPresenter

```cpp
namespace capabilities {
class IDashboardPresenter {
public:
    virtual ~IDashboardPresenter() = default;
    
    // Lifecycle
    virtual bool Initialize(const graph::CapabilityBus& bus) = 0;
    virtual void Shutdown() = 0;
    
    // Event handlers
    virtual void OnMetricsEvent(const app::metrics::MetricsEvent& event) = 0;
    virtual void OnLogMessage(const std::string& message) = 0;
    virtual void OnCommandResult(const CommandResult& result) = 0;
    
    // Rendering & input
    virtual void Render() = 0;
    virtual bool IsRunning() const = 0;
    virtual void RequestStop() = 0;
};
}
```

---

## Benefits of Proposed Architecture

| Aspect | Current | Proposed |
|--------|---------|----------|
| **UI-Agnostic** | Partial (Dashboard in policy) | Complete (Presenter abstraction) |
| **Extensibility** | Hard (direct dependencies) | Easy (capability-based) |
| **Testability** | Difficult (tight coupling) | Simple (mock capabilities) |
| **Thread Safety** | Implicit (queue-based) | Explicit (documented) |
| **Custom Commands** | Requires policy modification | RegisterCommand() call |
| **Command Processing** | String parsing in policy | Pluggable processors |
| **Multiple UIs** | Not possible | Easy (swappable presenters) |

---

## Migration Guide

### For Existing Code

```cpp
// OLD: Direct CommandRegistry access
CommandPolicy policy;
policy.registry_->ExecuteCommand("help", {});

// NEW: Via capability bus
auto cmd_proc = context.GetCapabilityBus()
    .Get<CommandProcessorCapability>()
    ->GetProcessor();
cmd_proc->ProcessCommand("help", {});
```

### For Custom Policies

```cpp
class CustomPolicy : public IExecutionPolicy {
    bool OnInit(capabilities::GraphCapability& context) override {
        // Access command registry capability
        auto cmd_reg = context.GetCapabilityBus()
            .Get<CommandRegistryCapability>();
        
        if (cmd_reg) {
            cmd_reg->RegisterCommand("my_cmd", 
                "Custom command", 
                "my_cmd [args]",
                [this](const auto& args) { return OnMyCommand(args); });
        }
        return true;
    }
    
    CommandResult OnMyCommand(const std::vector<std::string>& args) {
        return CommandResult(true, "Executed");
    }
};
```

---

## Files to Create/Modify

### New Files
- `include/capabilities/CommandRegistryCapability.hpp`
- `include/capabilities/CommandProcessorCapability.hpp`
- `include/capabilities/CommandOutputCapability.hpp`
- `include/capabilities/DashboardPresenterCapability.hpp`
- `src/graph/DefaultCommandProcessor.cpp`
- `src/graph/CommandOutputAdapters.cpp`

### Modified Files
- `include/ui/CommandRegistry.hpp` (remove Dashboard dependency)
- `include/policies/CommandPolicy.hpp` (use capabilities)
- `include/policies/DashboardPolicy.hpp` (refactor Dashboard creation)
- `include/ui/BuiltinCommands.hpp` (capability-based registration)

### Deprecated
- Direct Dashboard parameter in CommandRegistry methods
- CommandPolicy's private ExecuteCommand() implementation

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Breaking existing APIs | Medium | High | Maintain compatibility layer |
| Thread safety issues | Low | High | Add synchronization where needed |
| Performance degradation | Low | Medium | Benchmark after changes |
| Incomplete migration | Medium | Medium | Phased rollout per phase |

---

## Conclusion

The proposed abstraction architecture:

1. ✅ **Decouples Dashboard** from CommandRegistry and policies
2. ✅ **Enables multiple UI implementations** via capability-based design
3. ✅ **Improves testability** through interface abstractions
4. ✅ **Simplifies policy extensibility** with capability registration
5. ✅ **Maintains backward compatibility** with existing code
6. ✅ **Scales to custom commands** across multiple policies

**Recommended Next Steps**:
1. Implement Phase 1 (CommandRegistryCapability)
2. Refactor BuiltinCommands to use new capability
3. Update CommandPolicy to showcase capability usage
4. Add integration tests for command execution flow
5. Proceed with Phases 2-4 based on requirements
