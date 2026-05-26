# GraphX Node System - Quick Reference

**Latest Update**: May 10, 2026

---

## Node Type Hierarchy

```
INode (abstract interface)
├── SourceNodeBase<TypeList<Outputs...>>
│   ├── SourceNode<Outputs...>
│   └── NamedSourceNode<NodeType, Outputs...>
│
├── SinkNodeBase<TypeList<Inputs...>>
│   ├── SinkNode<Inputs...>
│   └── NamedSinkNode<NodeType, Inputs...>
│
├── InteriorNodeBase<TypeList<Inputs...>, TypeList<Outputs...>>
│   ├── InteriorNode<InputList, OutputList>
│   └── NamedInteriorNode<NodeType, InputList, OutputList>
│
├── MergeNodeBase<N, CommonInput, OutputT>
│   └── MergeNode5<CommonInput, OutputT>  (and variants)
│
└── SplitNode<T, N>
    ├── SplitNode1<T>
    ├── SplitNode2<T>
    ├── SplitNode3<T>
    ├── SplitNode4<T>
    ├── SplitNode5<T>
    ├── SplitNode6<T>
    ├── SplitNode7<T>
    └── SplitNode8<T>
```

---

## Core Lifecycle Methods (INode Interface)

### State Machine
```
Uninitialized --[Init()]→ Initialized --[Start()]→ Started
                                           ↓
                                  [Stop()] → Stopped
                                           ↓
                                  [Join()] → Joined
```

### API
```cpp
// Query
LifecycleState GetLifecycleState() const

// Lifecycle control
bool Init()                                    // Setup, non-threaded
bool Start()                                   // Launch worker threads
void Stop()                                    // Request graceful shutdown
void Join()                                    // Wait for completion
bool JoinWithTimeout(std::chrono::milliseconds ms)

// Port introspection
std::span<const PortInfo> InputPorts() const
std::span<const PortInfo> OutputPorts() const
```

---

## Node-Specific Methods

### SourceNodeBase<TypeList<Outputs...>>
```cpp
// Production (from OutputFn<T>)
template <std::size_t PortID>
void Produce(const OutputType<PortID>& value)

// Port queries
int GetOutputPortCount() const → NOutputs
int GetInputPortCount() const → 0
std::size_t GetOutputPortQueueSize(std::size_t port_id) const

// Metrics
const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const
const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const
void EnableMetrics(bool enabled = true)
void DisableMetrics()
void ResetMetrics()

// Port type access (constexpr)
template <std::size_t PortID> using OutputType = ...
template <std::size_t PortID> using OutputPortType = ...
static constexpr auto output_table  // Port metadata
```

### SinkNodeBase<TypeList<Inputs...>>
```cpp
// Consumption (from InputFn<T>)
template <std::size_t PortID>
bool Consume(const InputType<PortID>& value, 
             std::integral_constant<std::size_t, PortID>)

// Port queries
int GetInputPortCount() const → NInputs
int GetOutputPortCount() const → 0

// Metrics (identical to SourceNodeBase)
const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const
const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const
void EnableMetrics(bool enabled = true)
void DisableMetrics()
void ResetMetrics()

// Port type access
template <std::size_t PortID> using InputType = ...
template <std::size_t PortID> using InputPortType = ...
static constexpr auto input_table
```

### InteriorNodeBase<TypeList<Inputs...>, TypeList<Outputs...>>
```cpp
// Transformation (from TransferFn<Inputs, Outputs>)
template <std::size_t InPortID, std::size_t OutPortID>
std::optional<OutputType<OutPortID>> Transfer(
    const InputType<InPortID>& value,
    std::integral_constant<std::size_t, InPortID>,
    std::integral_constant<std::size_t, OutPortID>)

// Port queries
int GetInputPortCount() const → NInputs
int GetOutputPortCount() const → NOutputs
std::size_t GetInputPortQueueSize(std::size_t port_id) const
std::size_t GetOutputPortQueueSize(std::size_t port_id) const

// Metrics (both directions)
const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const
const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const
const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const
const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const
void EnableMetrics(bool enabled = true)
void DisableMetrics()
void ResetMetrics()

// Port type access
template <std::size_t PortID> using InputType = ...
template <std::size_t PortID> using OutputType = ...
template <std::size_t PortID> using InputPortType = ...
template <std::size_t PortID> using OutputPortType = ...
static constexpr auto input_table
static constexpr auto output_table
```

### MergeNodeBase<N, CommonInput, OutputT>
```cpp
// Merge processing (user implements)
std::optional<OutputT> Process(
    const CommonInput& event,
    std::integral_constant<std::size_t, 0>)  // Output port (always 0)

// Consumption on each input (from ExpandInputPorts)
bool Consume(const CommonInput& value,
             std::integral_constant<std::size_t, PortID>)

// Production on output (from OutputFn)
void Produce(const OutputT& value)

// Port queries
int GetInputPortCount() const → N
int GetOutputPortCount() const → 1

// Metrics
const core::QueueMetrics* GetInputQueueMetrics(std::size_t port_id) const
const core::QueueMetrics* GetOutputQueueMetrics(std::size_t port_id) const
const ThreadMetrics* GetInputPortThreadMetrics(std::size_t port_id) const
const ThreadMetrics* GetOutputPortThreadMetrics(std::size_t port_id) const
void EnableMetrics(bool enabled = true)
void ResetMetrics()
```

### SplitNode<T, N>
```cpp
// Consumption on single input
bool Consume(const T& value, std::integral_constant<std::size_t, 0>)

// Production on all N outputs (implemented per specialization)
std::optional<T> Produce(std::integral_constant<std::size_t, I>)

// Port queries
int GetOutputCount() const → N
int GetInputCount() const → 1
```

---

## NodeFactory API

### Template Method (Compile-time)
```cpp
template <reflection::GraphNode NodeType, typename... Args>
std::shared_ptr<NodeType> CreateNode(Args&&... args)
```

### Dynamic Methods (Runtime)
```cpp
NodeFacadeAdapter CreateDynamicNode(const std::string& node_type_name)
NodeFacadeAdapter CreateNode(const std::string& node_type_name)
void Initialize()

bool IsNodeTypeAvailable(const std::string& node_type_name) const
std::vector<std::string> GetAvailableNodeTypes() const
bool IsInitialized() const

template <typename NodeType>
std::string GetNodeTypeInfo() const
```

### NodeFactoryRegistry Methods
```cpp
void Register(const std::string& type_name, NodeFactoryFunction factory)
NodeFacadeAdapter Create(const std::string& type_name)
bool IsAvailable(const std::string& type_name) const
std::vector<std::string> GetRegisteredTypes() const
```

---

## Port Metadata (Compile-time, consteval)

```cpp
struct PortInfo {
    std::size_t index;              // 0-based port ID
    std::string_view name;          // Port name
    std::string_view type_name;     // Type name
    PortDirection direction;        // Input or Output
    std::size_t queue_capacity;     // Max items in queue (0 = unlimited)
};

enum class PortDirection {
    Input,
    Output
};
```

### Accessing Port Metadata
```cpp
// Static constexpr (available at compile-time)
auto ports = std::span(MyNode::output_table);

// At runtime
auto node = std::make_shared<MyNode>();
auto ports = node->OutputPorts();
for (const auto& port : ports) {
    std::cout << "Port " << port.index << ": " << port.name << "\n";
}
```

---

## Metrics Structures

### Queue Metrics
```cpp
struct QueueMetrics {
    std::size_t total_enqueued;      // Total items ever enqueued
    std::size_t total_dequeued;      // Total items ever dequeued
    std::size_t current_size;        // Items currently in queue
    std::size_t peak_size;           // Maximum queue size ever observed
    std::size_t times_full;          // How many times queue was full
    std::size_t times_empty;         // How many times queue was empty
};
```

### Thread Metrics
```cpp
struct ThreadMetrics {
    uint64_t total_iterations;       // Main loop iterations
    uint64_t produce_calls;          // Produce() invocations
    uint64_t consume_calls;          // Consume() invocations
    uint64_t transfer_calls;         // Transfer() invocations
    uint64_t total_produce_time_ns;  // Cumulative produce time
    uint64_t total_consume_time_ns;  // Cumulative consume time
    uint64_t total_transfer_time_ns; // Cumulative transfer time
    uint64_t total_idle_time_ns;     // Time waiting for input
    bool thread_active;              // Is thread currently running?
};
```

---

## Common Usage Patterns

### Basic Source Node
```cpp
class MySource : public graph::SourceNode<int, double> {
};

auto node = std::make_shared<MySource>();
node->Init();
node->Start();

// From producer thread:
node->Produce<0>(42);        // int output on port 0
node->Produce<1>(3.14);      // double output on port 1

node->Stop();
node->Join();
```

### Basic Sink Node
```cpp
class MySink : public graph::SinkNode<int, std::string> {
    bool Consume(const int& val, std::integral_constant<std::size_t, 0>) override {
        std::cout << "Got int: " << val << "\n";
        return true;
    }
    bool Consume(const std::string& val, std::integral_constant<std::size_t, 1>) override {
        std::cout << "Got string: " << val << "\n";
        return true;
    }
};

auto node = std::make_shared<MySink>();
node->Init();
node->Start();
// Data arrives from connected source nodes
node->Stop();
node->Join();
```

### Basic Interior (Transformer) Node
```cpp
class MyTransform : public graph::InteriorNode<
    graph::TypeList<int, std::string>,
    graph::TypeList<double, bool>> {
    
    std::optional<double> Transfer(const int& val,
                                   std::integral_constant<std::size_t, 0>,
                                   std::integral_constant<std::size_t, 0>) override {
        return std::optional<double>(val * 3.14);
    }
    
    std::optional<bool> Transfer(const std::string& val,
                                 std::integral_constant<std::size_t, 1>,
                                 std::integral_constant<std::size_t, 1>) override {
        return std::optional<bool>(!val.empty());
    }
};

auto node = std::make_shared<MyTransform>();
node->Init();
node->Start();
node->Stop();
node->Join();
```

### Using NodeFactory
```cpp
auto factory = std::make_shared<graph::NodeFactory>(plugin_registry);
factory->Initialize();

// Compile-time type-safe creation
auto source = factory->CreateNode<MySourceNode>();

// Runtime dynamic creation
auto dynamic_node = factory->CreateNode("SomePluginNode");

// Type checking
if (factory->IsNodeTypeAvailable("CustomNode")) {
    auto node = factory->CreateNode("CustomNode");
}
```

### Metrics Collection
```cpp
auto node = std::make_shared<MySourceNode>();
node->Init();
node->Start();

// Produce some data...
for (int i = 0; i < 100; ++i) {
    node->Produce<0>(i);
}

std::this_thread::sleep_for(std::chrono::milliseconds(100));

// Query metrics
auto queue_metrics = node->GetOutputQueueMetrics(0);
if (queue_metrics) {
    std::cout << "Enqueued: " << queue_metrics->total_enqueued << "\n";
    std::cout << "Peak size: " << queue_metrics->peak_size << "\n";
}

auto thread_metrics = node->GetOutputPortThreadMetrics(0);
if (thread_metrics) {
    std::cout << "Iterations: " << thread_metrics->total_iterations << "\n";
    std::cout << "Active: " << (thread_metrics->thread_active ? "yes" : "no") << "\n";
}

node->Stop();
node->Join();
```

---

## Files Reference

| Component | Header | Implementation | Status |
|-----------|--------|----------------|--------|
| NodeFactory | NodeFactory.hpp | NodeFactory.cpp | ✅ Complete |
| NodeFactoryRegistry | NodeFactoryRegistry.hpp | — (header-only) | ✅ Complete |
| INode | INode.hpp | — (header-only) | ✅ Complete |
| SourceNodeBase | Nodes.hpp | — (template) | ✅ Complete |
| SinkNodeBase | Nodes.hpp | — (template) | ✅ Complete |
| InteriorNodeBase | Nodes.hpp | — (template) | ✅ Complete |
| MergeNodeBase | Nodes.hpp | — (template) | ✅ Complete |
| SplitNode | SplitNode.hpp | — (template) | ✅ Complete |
| Lifecycle Mixin | Lifecycle.hpp | — (template) | ✅ Complete |
| NamedNodes | NamedNodes.hpp | — (template) | ✅ Complete |
| NodeFacade | NodeFacade.hpp | — (C struct) | ✅ Complete |

---

## Key Concepts

### Compile-time vs Runtime
- **Compile-time**: Node type, port counts, port types
- **Runtime**: Port metadata, lifecycle state, metrics

### Thread Safety
- **Not thread-safe**: Init(), Start() - call before concurrent access
- **Thread-safe**: Produce/Consume/Transfer - called from port threads
- **Thread-safe**: Metrics queries, Stop(), Join() - can call from any thread

### Port IDs
- **Assigned at compile-time**: Sequential 0, 1, 2, ...
- **Type-safe**: Use `std::integral_constant<std::size_t, N>` in methods
- **Runtime**: Use `std::size_t port_id` for metrics queries

### Queue Behavior
- **Per-port queues**: Each input/output has independent queue
- **Unbounded by default**: `queue_capacity = 0` in PortInfo
- **Thread-backed**: Automatic thread spawning per port during Start()

---

## Performance Characteristics

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| CreateNode (template) | O(1) | Compile-time |
| CreateNode (dynamic) | O(n) | Linear search in registry |
| Produce/Consume | O(1) amortized | Lock-free enqueue |
| Transfer | O(1) amortized | Processes one input item |
| GetMetrics | O(1) | Direct pointer return |
| JoinWithTimeout | O(n) | Timeout/port ratio |

---

## Debugging Tips

### Lifecycle Deadlocks
```cpp
// Check state before operations
if (node->GetLifecycleState() != LifecycleState::Started) {
    // Node not ready
}
```

### Queue Backlogs
```cpp
// Monitor queue sizes
auto metrics = node->GetOutputQueueMetrics(0);
if (metrics->current_size > 1000) {
    std::cerr << "Queue backup detected\n";
}
```

### Thread Stalls
```cpp
// Check thread activity
auto thread_metrics = node->GetOutputPortThreadMetrics(0);
if (!thread_metrics->thread_active) {
    std::cerr << "Thread not running\n";
}
```

### Timeout Tuning
```cpp
// Set timeout based on thread metrics
auto per_port_ms = total_timeout_ms / node->GetOutputPortCount();
bool success = node->JoinWithTimeout(std::chrono::milliseconds(per_port_ms));
```

---

**For detailed information, see**:
- [NODEFACTORY_COMPREHENSIVE_ANALYSIS.md](NODEFACTORY_COMPREHENSIVE_ANALYSIS.md) - Full API reference
- [NODEFACTORY_CPP26_TEST_PLAN.md](NODEFACTORY_CPP26_TEST_PLAN.md) - Testing and C++26 enhancements
