# TestGraphTopologies: Data Flow & Completion Strategy

**Date**: May 12, 2026 (REVISED)  
**Status**: Architecture Document  
**Scope**: Data production, consumption, and completion notification patterns for GraphTopologies

---

## 1. Overview

The GraphTopologies execution model uses a **sink-driven completion pattern**:
- **Data Production**: Sources emit `Message<T>` to output port 0
- **Data Consumption**: Sinks consume messages via `Consume()` and track internal state
- **Completion Detection**: Sinks signal completion via `ICompletionCallback` when done
- **Automatic Monitoring**: `CompletionPolicy` monitors all completion callbacks
- **No Timeout Needed**: Graph exits cleanly when sinks signal completion

This is a **self-determining pattern** - sinks decide when they're finished based on their internal logic (message counter, timeout, state machine, etc.).

---

## 2. Current Architecture

### 2.1 Message Types

**One Core Message Type**:

**`graph::message::Message<T>`** (Data Messages)
- Wraps typed payload (e.g., `Message<int>`, `Message<DataRecord>`)
- Flows through data ports (0, 1, 2, ...)
- Represents actual computational data
- Consumed by sinks via `Consume(const Message<T>& msg, ...)`

**Note on `CompletionSignal`**:
- Exists for future use (DataProducerWithNotification pattern)
- **NOT used in current TestGraphTopologies approach**
- Current approach uses **callback-based completion** instead

### 2.2 Node Categories

```
┌─────────────────────────────────────────────────────────────┐
│                      Graph Nodes                            │
└─────────────────────────────────────────────────────────────┘
         ↓                    ↓                    ↓
    ┌─────────┐        ┌──────────────┐        ┌─────────┐
    │ Source  │        │ Interior     │        │  Sink   │
    │  Nodes  │        │   Nodes      │        │  Nodes  │
    └─────────┘        └──────────────┘        └─────────┘
```

**Source Nodes** (`SourceNodeBase<OutputTypes...>`)
- Produce messages to output port 0 (simple `Message<T>`)
- No responsibility for completion signals
- Examples: `SourceTestNode`

**Interior Nodes** (`InteriorNodeBase<Inputs..., Outputs...>`)
- Transform messages: consume from inputs, produce to outputs
- Pass-through transformations or complex processing
- Examples: `InteriorTestNode`, `TransformNode`

**Sink Nodes** (`SinkNodeBase<InputTypes...>`)
- Consume messages from input ports via `Consume()`
- **Implement `ICompletionCallback`** to signal completion
- Track internal state (counter, state machine, etc.)
- Call callback when done: `callback_provider_->OnComplete()`
- Examples: `SinkTestNode` (enhanced), custom sinks

**Special Merge/Split Nodes**
- `MergeNode<N, InputType, OutputType>`: Combines N inputs → 1 output
- `SplitNode<T, N>`: Replicates 1 input → N outputs

---

## 3. Data Production Strategy

### 3.1 Source Node Pattern

Sources follow a simple pattern:
- Inherit from `SourceNodeBase<Message>` (single output port)
- Implement standard lifecycle: `Init()`, `Start()`, `Stop()`, `Join()`
- Emit `Message<T>` to output port 0
- **No completion signal responsibility** - sinks decide completion

**Example**:
```cpp
class SourceTestNode : public NamedSourceNode<
    SourceTestNode,
    graph::message::Message> {  // Single output type
public:
    static constexpr char kDataPort[] = "Data";
    using Ports = std::tuple<
        PortSpec<0, Message, PortDirection::Output, kDataPort, ...>
    >;
    
    // Emit a message to output port 0
    void ProduceMessage(int value) {
        auto msg = graph::message::Message(value);
        this->template EmitToPort<0>(msg);
    }
};
```

**Characteristics**:
- ✅ Simple: one output port for data
- ✅ No completion signal emission
- ✅ Stateless from completion perspective
- ✅ Can be passive (test setup) or active (real data producer)

---

## 4. Data Consumption Strategy

### 4.1 Sink Node Pattern: Single Responsibility

Sinks have **ONE responsibility**: 
1. Consume messages and track internal state
2. When done, call callback to signal completion

**Sink Node with Completion Support**:
```cpp
class SinkTestNode : public NamedSinkNode<
    SinkTestNode,
    graph::message::Message>,
    public graph::ICompletionCallback<graph::message::Message> {  // NEW
    
public:
    static constexpr char kStatePort[] = "State";
    using Ports = std::tuple<
        PortSpec<0, Message, PortDirection::Input, kStatePort, ...>
    >;
    
    // Step 1: Consume messages and track state
    bool Consume(const Message& msg, 
                 std::integral_constant<std::size_t, 0>) override {
        messages_received_.push_back(msg);
        message_count_++;
        
        // Step 2: Check if done
        if (message_count_ >= EXPECTED_MESSAGES) {
            // Signal completion - THIS is how graph knows we're done
            if (this->HasCallbackProvider()) {
                auto callback = dynamic_cast<
                    graph::ICompletionCallback<Message>::CompletionNodeCallback*>(
                    this->GetCallbackProvider());
                callback->OnComplete();  // ← Tells CompletionPolicy we're done
            }
        }
        return true;  // Accept message
    }

private:
    std::vector<Message> messages_received_;
    std::atomic<size_t> message_count_{0};
    static constexpr size_t EXPECTED_MESSAGES = 100;
};
```

**Key Point**: 
- Sink **decides when to call callback** based on **its own logic**
- Counter, state machine, timeout, error condition, etc.
- One call to `callback->OnComplete()` signals **all done**

---

## 5. Completion Detection Strategy

### 5.1 CompletionPolicy (Automatic)

`GraphExecutor` automatically uses `CompletionPolicy` which:
1. Scans graph for nodes implementing `ICompletionCallback`
2. Installs callbacks on all such nodes
3. Waits for all callbacks to be invoked
4. Signals executor to stop when all complete

**No manual setup required** - it's built into `GraphExecutorBuilder`.

**Flow**:
```
┌────────────────────────────────────────────────┐
│ GraphExecutorBuilder::Build()                  │
│  Creates CompletionPolicy automatically        │
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ CompletionPolicy::OnInit()                     │
│  - Searches graph for ICompletionCallback nodes│
│  - Installs completion callback on each        │
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ Execution Runs                                 │
│  - Sources emit messages to port 0             │
│  - Sinks consume via Consume()                 │
│  - Sinks track internal state                  │
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ Sink Decides It's Done                         │
│  - Counter reached threshold, or               │
│  - State machine transitioned, or              │
│  - Error detected, or                          │
│  - User requested stop                         │
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ Sink Calls: callback->OnComplete()             │
│  (One per sink that implements ICompletionCallback)
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ CompletionPolicy Detects All Callbacks Called  │
│  - Checks if all expected callbacks fired      │
│  - Signals executor: execution_complete = true │
└────────────────────────────────────────────────┘
           ↓
┌────────────────────────────────────────────────┐
│ GraphExecutor::Run() Exits Cleanly             │
│  - No timeout required                         │
│  - All data processed                          │
│  - All callbacks invoked                       │
└────────────────────────────────────────────────┘
```

### 5.2 Example: Simple Counter-Based Completion

```cpp
TEST(GraphExecutor, CompletesWhenSinkDone) {
    // Build simple topology
    auto graph = BuildLinearSequentialTopology();
    
    // Create executor with automatic CompletionPolicy
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .Build();  // ← Includes CompletionPolicy automatically
    
    executor->Init();
    executor->Start();
    
    // Simulate data production (from any thread)
    auto source = graph->GetNodeByName<SourceTestNode>("source");
    for (int i = 0; i < 100; ++i) {
        source->ProduceMessage(i);
    }
    
    // Executor.Run() watches for completion callbacks
    // When sink receives 100 messages, it calls:
    //   callback->OnComplete()
    // CompletionPolicy sees all callbacks done, executor exits
    
    // No timeout needed - execution completes cleanly
    executor->Run();  // Blocks until completion signal received
    
    executor->Stop();
    executor->Join();
    
    // Verify
    auto sink = graph->GetNodeByName<SinkTestNode>("sink");
    EXPECT_EQ(sink->GetMessagesReceived(), 100);
}
```

---

## 6. How CompletionPolicy Works

### 6.1 Automatic Discovery

When you create a `GraphExecutor` via `GraphExecutorBuilder`:

```cpp
auto executor = graph::GraphExecutorBuilder()
    .WithGraphManager(graph)
    .Build();
```

The builder **automatically creates and installs** a `CompletionPolicy` that:
1. Discovers all nodes in graph implementing `ICompletionCallback`
2. Registers callbacks with each discovered node
3. Waits for all callbacks to be invoked
4. Signals executor when all are done

**No manual callback setup required** - it's automatic.

### 6.2 ICompletionCallback Interface

```cpp
template<typename DataType>
class ICompletionCallback {
public:
    // CompletionPolicy calls this to set callback handler
    virtual bool SetCallbackProvider(NodeCallback* provider) noexcept;
    
    // Check if callback is installed
    virtual bool HasCallbackProvider() const noexcept;
    
    // Get callback provider
    virtual NodeCallback* GetCallbackProvider() const noexcept;
};

class CompletionNodeCallback : public NodeCallback {
public:
    // Node calls this when done
    void OnComplete() noexcept {
        if (on_complete_callback_) {
            on_complete_callback_();  // Notifies CompletionPolicy
        }
    }
};
```

---

## 7. Test Pattern: Counter-Based Completion

**Simplest approach**: Sink counts messages and signals when done.

```cpp
class SimpleTestSink : public NamedSinkNode<
    SimpleTestSink,
    Message>,
    public ICompletionCallback<Message> {

public:
    static constexpr char kPort[] = "Input";
    using Ports = std::tuple<
        PortSpec<0, Message, PortDirection::Input, kPort, ...>
    >;
    
    SimpleTestSink(size_t expected_count = 100)
        : expected_count_(expected_count) {}
    
    bool Consume(const Message& msg,
                 std::integral_constant<std::size_t, 0>) override {
        message_count_++;
        
        // Check if we're done
        if (message_count_ >= expected_count_) {
            // Signal completion - CompletionPolicy will detect this
            if (this->HasCallbackProvider()) {
                auto cb = dynamic_cast<CompletionNodeCallback*>(
                    this->GetCallbackProvider());
                cb->OnComplete();  // ← One line - triggers executor exit
            }
        }
        return true;
    }
    
    size_t GetMessageCount() const { return message_count_; }

private:
    std::atomic<size_t> message_count_{0};
    size_t expected_count_;
};
```

**Usage in test**:
```cpp
TEST(GraphExecutor, SimpleSinkCompletion) {
    auto graph = BuildLinearSequentialTopology();
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .Build();  // CompletionPolicy automatic
    
    executor->Init();
    executor->Start();
    
    // Produce data
    auto source = graph->GetNodeByName<SourceTestNode>("source");
    for (int i = 0; i < 100; ++i) {
        source->ProduceMessage(i);
    }
    
    // Run blocks until sink's callback fires (after 100 messages)
    // No timeout needed - execution completes when sink decides
    executor->Run();  
    
    executor->Stop();
    executor->Join();
    
    auto sink = graph->GetNodeByName<SimpleTestSink>("sink");
    EXPECT_EQ(sink->GetMessageCount(), 100);
}
```

---

## 8. Timeout Strategy (Fallback Only)

**Timeouts are only needed if**:
- Sink doesn't implement `ICompletionCallback`
- Sink implements it but never calls callback
- Callback pointer not set by CompletionPolicy

**For TestGraphTopologies**: Don't use timeouts if sinks properly implement completion callbacks.

```cpp
// OLD WAY (Don't do this)
auto timeout = std::chrono::seconds(10);
EXPECT_TRUE(execution_cv.wait_for(lock, timeout, ...));

// NEW WAY (Do this)
// CompletionPolicy handles waiting automatically
// Your sink calls callback when done
// No manual timeout needed
executor->Run();  // Blocks until completion detected
```

---

## 9. TestGraphTopologies Implementation Strategy

### 9.1 What Changes (Minimal)

1. **SinkTestNode Enhancement** (add one interface):
   ```cpp
   class SinkTestNode : public NamedSinkNode<...>,
                        public ICompletionCallback<Message> {  // NEW
       // Add counter to Consume()
       // Add callback invocation when threshold reached
   };
   ```

2. **No changes to sources** - they just emit messages
3. **No changes to topology builders** - they stay the same
4. **No CompletionSignal flowing through graph** - not needed
5. **No CompletionAggregatorNode in topologies** - CompletionPolicy handles it

### 9.2 What Stays the Same

- Source nodes still produce `Message<T>` to port 0
- Interior nodes still transform messages
- Topology builders still use FluentGraphBuilder
- All edges are just data edges (no completion edges)

---

## 10. Summary: The Three Components (Revised)

| Component | Role | Responsibility |
|-----------|------|---|
| **Sources** | Produce data | Emit `Message<T>` to port 0 (that's all!) |
| **Sinks** | Consume data + signal done | Implement `Consume()` AND `ICompletionCallback`, call callback when done |
| **CompletionPolicy** | Monitor completion | Discover sinks, register callbacks, wait for all to complete (automatic) |

**Data Flow**: Source → Interior → Sink  
**Completion Flow**: Sink calls callback → CompletionPolicy detects → Executor exits

No completion signals, no extra nodes, no timeout management code.

---
