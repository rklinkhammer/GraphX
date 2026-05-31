# TestGraphTopologies: Implementation Guide for Sink Completion (REVISED)

**Date**: May 12, 2026 (CORRECTED)  
**Status**: Developer Guide  
**Purpose**: Add `ICompletionCallback` support to sink nodes for automatic completion detection

---

## Quick Summary

**Goal**: Enable sinks to signal completion so `CompletionPolicy` can automatically detect when graph work is done.

**Implementation**: ONE SIMPLE CHANGE - make SinkTestNode implement `ICompletionCallback<Message>`

**Why**: `CompletionPolicy` automatically monitors all completion callbacks and exits executor when all are invoked.

---

## What Changed from Earlier Design

**OLD (Incorrect - from this morning)**:
- Add CompletionSignal as second output from sources
- Route completion signals through graph edges
- CompletionAggregatorNode collects multiple signals
- Complex multi-port topology
- Multiple callback setup steps

**NEW (Correct)**:
- Sinks implement one interface: `ICompletionCallback`
- Sinks decide when done based on their own logic (counter, state, etc.)
- Sinks call callback once: `callback->OnComplete()`
- `CompletionPolicy` automatically monitors all callbacks
- Simple: 3-5 new lines of code

---

## Implementation: Modify SinkTestNode (3 lines)

### Current SinkTestNode

```cpp
// libgraph/test/include/test/AdvancedTestNodes.hpp

class SinkTestNode : public graph::NamedSinkNode<
    SinkTestNode, 
    ::graph::message::Message> {
public: 
    static constexpr char kStatePort[] = "State";
    using Ports = std::tuple<
        graph::PortSpec<0, ::graph::message::Message, 
                       graph::PortDirection::Input, kStatePort,
                       graph::PayloadList<int>>
    >;
        
    SinkTestNode() : graph::NamedSinkNode<...>() {
        SetName("TestLogger2");
    } 

    virtual ~SinkTestNode() = default;

    bool Consume(const ::graph::message::Message& msg, 
                 std::integral_constant<std::size_t, 0>) override {
        (void)msg;
        return true;
    }
};
```

### Enhanced SinkTestNode (Add Interface + 3 Methods)

```cpp
// libgraph/test/include/test/AdvancedTestNodes.hpp

class SinkTestNode : public graph::NamedSinkNode<
    SinkTestNode, 
    ::graph::message::Message>,
    public graph::ICompletionCallback<::graph::message::Message> {  // ← NEW
public: 
    static constexpr char kStatePort[] = "State";
    using Ports = std::tuple<
        graph::PortSpec<0, ::graph::message::Message, 
                       graph::PortDirection::Input, kStatePort,
                       graph::PayloadList<int>>
    >;
    
    // Constructor with optional completion threshold
    explicit SinkTestNode(size_t expected_messages = 0) 
        : graph::NamedSinkNode<SinkTestNode, ::graph::message::Message>(),
          expected_message_count_(expected_messages) {
        SetName("SinkTestNode");
    } 

    virtual ~SinkTestNode() = default;

    bool Consume(const ::graph::message::Message& msg, 
                 std::integral_constant<std::size_t, 0>) override {
        (void)msg;
        message_count_++;  // ← NEW: Track count
        
        // ← NEW: Signal completion when threshold reached
        if (expected_message_count_ > 0 && 
            message_count_ >= expected_message_count_) {
            SignalCompletion();
        }
        
        return true;
    }
    
    // ← NEW: Query message count (for test verification)
    size_t GetMessageCount() const { 
        return message_count_; 
    }
    
    // ← NEW: Setter for expected count (for dynamic configuration)
    void SetExpectedMessageCount(size_t count) {
        expected_message_count_ = count;
    }

private:
    // ← NEW: Helper to invoke callback
    void SignalCompletion() {
        if (this->HasCallbackProvider()) {
            auto cb = dynamic_cast<
                graph::ICompletionCallback<::graph::message::Message>
                    ::CompletionNodeCallback*>(
                this->GetCallbackProvider());
            if (cb) cb->OnComplete();
        }
    }
    
    std::atomic<size_t> message_count_{0};          // ← NEW
    size_t expected_message_count_{0};              // ← NEW
};
```

**What's Different**:
1. Class declaration: Add `public ICompletionCallback<Message>`
2. Constructor: Accept expected_message_count parameter
3. Consume(): Increment counter, check threshold, call SignalCompletion()
4. Methods: Add GetMessageCount(), SetExpectedMessageCount(), SignalCompletion()
5. Members: Add message_count_ and expected_message_count_

---

## That's It!

When you build executor:

```cpp
auto executor = graph::GraphExecutorBuilder()
    .WithGraphManager(graph)
    .Build();  // ← Automatically creates CompletionPolicy
```

The `CompletionPolicy` **automatically**:
1. ✅ Finds all nodes implementing `ICompletionCallback` (SinkTestNode, etc.)
2. ✅ Registers callbacks on them
3. ✅ Waits for all callbacks to fire
4. ✅ Signals executor to exit when all complete

**You don't write any callback code.** Policy does it for you.

---

## Usage Example: Complete Test

```cpp
TEST(GraphExecutor, LiniarTopologySinkCompletion) {
    // Use existing topology (NO CHANGES)
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::LinearSequential);
    
    // Configure sink with expected message count
    auto sink = graph->GetNodeByName<SinkTestNode>("sink");
    sink->SetExpectedMessageCount(100);
    
    // Build executor with auto CompletionPolicy
    auto executor = graph::GraphExecutorBuilder()
        .WithGraphManager(graph)
        .Build();  // ← Creates CompletionPolicy automatically
    
    executor->Init();
    executor->Start();
    
    // Produce data (from main thread, background, wherever)
    auto source = graph->GetNodeByName<SourceTestNode>("source");
    for (int i = 0; i < 100; ++i) {
        source->ProduceMessage(i * 42);
    }
    
    // Run blocks until sink's callback fires (after 100 messages)
    // CompletionPolicy watches for it automatically
    // No timeout, no manual callback code, no condition variables
    executor->Run();
    
    executor->Stop();
    executor->Join();
    
    // Verify results
    EXPECT_EQ(sink->GetMessageCount(), 100);
}
```

---

## Alternative Completion Logic

Instead of just counting messages, use any logic you want:

### Example 1: Timeout-Based

```cpp
bool Consume(const Message& msg,
             std::integral_constant<std::size_t, 0>) override {
    last_message_time_ = std::chrono::steady_clock::now();
    message_count_++;
    
    // If 2 seconds with no messages, mark complete
    auto elapsed = std::chrono::steady_clock::now() - last_message_time_;
    if (elapsed > std::chrono::seconds(2)) {
        SignalCompletion();  // Tell executor we're done
    }
    return true;
}
```

### Example 2: State Machine

```cpp
enum State { WAITING, PROCESSING, DONE };

bool Consume(const Message& msg,
             std::integral_constant<std::size_t, 0>) override {
    if (state_ == WAITING && msg.HasStartMarker()) {
        state_ = PROCESSING;
    } else if (state_ == PROCESSING && msg.HasEndMarker()) {
        state_ = DONE;
        SignalCompletion();  // ← One line triggers exit
    }
    return true;
}
```

### Example 3: Error Detection

```cpp
bool Consume(const Message& msg,
             std::integral_constant<std::size_t, 0>) override {
    if (msg.IsError()) {
        error_count_++;
        if (error_count_ >= MAX_ERRORS) {
            SignalCompletion();  // ← Abort on too many errors
        }
    }
    return true;
}
```

---

## Key Architecture Points

```
Source ──(Port 0: Message)──> Interior ──> Sink
                                            │
                                  Consume() + Counter
                                            │
                                    If threshold reached:
                                    callback->OnComplete()
                                            │
                                   CompletionPolicy detects
                                            │
                                   GraphExecutor exits
```

**No completion signals. No extra nodes. No complex routing.**

---

## Complete Implementation Example

Here's the full modified SinkTestNode:

```cpp
// libgraph/test/include/test/AdvancedTestNodes.hpp

class SinkTestNode : public graph::NamedSinkNode<
    SinkTestNode, 
    ::graph::message::Message>,
    public graph::ICompletionCallback<::graph::message::Message> {
    
public: 
    static constexpr char kStatePort[] = "State";
    using Ports = std::tuple<
        graph::PortSpec<0, ::graph::message::Message, 
                       graph::PortDirection::Input, kStatePort,
                       graph::PayloadList<int>>
    >;
    
    explicit SinkTestNode(size_t expected_count = 0)
        : graph::NamedSinkNode<SinkTestNode, ::graph::message::Message>(),
          expected_message_count_(expected_count) {
        SetName("SinkTestNode");
    } 

    virtual ~SinkTestNode() = default;

    bool Consume(const ::graph::message::Message& msg, 
                 std::integral_constant<std::size_t, 0>) override {
        (void)msg;
        message_count_++;
        
        if (expected_message_count_ > 0 && 
            message_count_ >= expected_message_count_) {
            SignalCompletion();
        }
        return true;
    }
    
    size_t GetMessageCount() const { 
        return message_count_; 
    }
    
    void SetExpectedMessageCount(size_t count) {
        expected_message_count_ = count;
    }

private:
    void SignalCompletion() {
        if (this->HasCallbackProvider()) {
            auto cb = dynamic_cast<
                graph::ICompletionCallback<::graph::message::Message>
                    ::CompletionNodeCallback*>(
                this->GetCallbackProvider());
            if (cb) cb->OnComplete();
        }
    }
    
    std::atomic<size_t> message_count_{0};
    size_t expected_message_count_{0};
};
```

---

## Testing Strategy

### Test 1: Verify Callback Fires

```cpp
TEST(SinkTestNode, CallsCompletionCallback) {
    auto sink = std::make_shared<SinkTestNode>(10);  // Expect 10 messages
    
    bool callback_fired = false;
    auto callback = std::make_shared<
        graph::ICompletionCallback<Message>::CompletionNodeCallback>();
    callback->SetOnComplete([&]() {
        callback_fired = true;
    });
    
    sink->SetCallbackProvider(callback.get());
    
    // Send 10 messages
    for (int i = 0; i < 10; ++i) {
        sink->Consume(Message(i), std::integral_constant<size_t, 0>());
    }
    
    EXPECT_TRUE(callback_fired);
    EXPECT_EQ(sink->GetMessageCount(), 10);
}
```

### Test 2: Multi-Sink Completion

```cpp
TEST(GraphExecutor, MultiSinkCompletion) {
    auto graph = test::TopologyBuilder::BuildTopology(
        test::TopologyType::SplitSimple);  // 1 source → split → 2 sinks
    
    auto sink1 = graph->GetNodeByName<SinkTestNode>("sink1");
    auto sink2 = graph->GetNodeByName<SinkTestNode>("sink2");
    
    sink1->SetExpectedMessageCount(100);
    sink2->SetExpectedMessageCount(100);
    
    auto executor = GraphExecutorBuilder()
        .WithGraphManager(graph)
        .Build();
    
    executor->Init();
    executor->Start();
    
    auto source = graph->GetNodeByName<SourceTestNode>("source");
    for (int i = 0; i < 100; ++i) {
        source->ProduceMessage(i);
    }
    
    // Executor exits when BOTH sinks complete
    executor->Run();
    
    executor->Stop();
    executor->Join();
    
    EXPECT_EQ(sink1->GetMessageCount(), 100);
    EXPECT_EQ(sink2->GetMessageCount(), 100);
}
```

---

## Implementation Checklist

- [ ] **Modify SinkTestNode** in AdvancedTestNodes.hpp
  - [ ] Add `: public ICompletionCallback<Message>` to inheritance
  - [ ] Add constructor parameter `size_t expected_count = 0`
  - [ ] Add member: `std::atomic<size_t> message_count_{0}`
  - [ ] Add member: `size_t expected_message_count_{0}`
  - [ ] Increment message_count_ in Consume()
  - [ ] Check threshold and call SignalCompletion()
  - [ ] Add GetMessageCount() method
  - [ ] Add SetExpectedMessageCount() method
  - [ ] Add private SignalCompletion() helper

- [ ] **Create test** for sink completion
  - [ ] Test that callback fires when threshold reached
  - [ ] Test multi-sink completion in topologies
  - [ ] Test executor exits cleanly

- [ ] **Verify no other changes needed**
  - [ ] Topologies unchanged
  - [ ] Sources unchanged
  - [ ] Interior nodes unchanged
  - [ ] Graph builders unchanged

---

## Key Points to Remember

1. ✅ **CompletionPolicy is automatic** - built into GraphExecutorBuilder
2. ✅ **Only sink needs interface** - `ICompletionCallback<Message>`
3. ✅ **Callback is one-liner** - `callback->OnComplete()`
4. ✅ **No timeout code** - policy handles waiting
5. ✅ **No completion signals** - use counter/state/whatever
6. ✅ **No extra nodes** - no aggregator needed
7. ✅ **No edge changes** - all data edges stay same
8. ✅ **Topologies unchanged** - existing builders work as-is

---

## Timeline

- **Modify SinkTestNode**: 10 minutes
- **Create test**: 5 minutes
- **Build and verify**: 2 minutes
- **Total**: ~15 minutes

---

**Document Version**: 2.0 (Corrected)  
**Last Updated**: May 12, 2026  
**Status**: READY FOR IMMEDIATE IMPLEMENTATION
