# Advanced Test Nodes Specification

This document specifies three complex test nodes for the Advanced Nodes test suite. Each node needs your review for **correctness before implementation**.

---

## 1. InteriorTestNode (Transformation Node)

**Purpose**: Tests `InteriorNodeBase<TypeList<Input...>, TypeList<Output...>>` with a simple transformation.

**Inheritance**:
```cpp
class InteriorTestNode 
    : public graph::InteriorNodeBase<
        graph::TypeList<graph::PortSpec<
            0, ::graph::message::Message, 
            graph::PortDirection::Input, 
            "Input", 
            graph::PayloadList<int>>>,
        graph::TypeList<graph::PortSpec<
            0, ::graph::message::Message, 
            graph::PortDirection::Output, 
            "Output", 
            graph::PayloadList<int>>>>,
      public graph::NamedType<InteriorTestNode>
```

**Port Definition**:
- **Input Port 0** (`"Input"`): `Message<int>`
- **Output Port 0** (`"Output"`): `Message<int>`

**Transfer Implementation**:
```cpp
// Override Transfer<Input0, Output0>
int Transfer(const int& input_value, 
             std::integral_constant<std::size_t, 0>,  // input port
             std::integral_constant<std::size_t, 0>) override {  // output port
    return input_value * 2;  // Simple doubling transformation
}
```

**Key Characteristics**:
- Simple 1-to-1 transformation: doubles input integer
- Synchronous processing (Transfer method is called per message)
- Tests compile-time port reflection for both inputs and outputs
- Tests NodeLifecycleMixin behavior with interior (transform) semantics

**Review Checklist**:
- [ ] Inheritance pattern matches `InteriorNodeBase<TypeList<...>, TypeList<...>>`?
- [ ] Input port spec has PortDirection::Input and ID=0?
- [ ] Output port spec has PortDirection::Output and ID=0?
- [ ] Transfer signature matches: `int Transfer(const int&, std::integral_constant<0>, std::integral_constant<0>)`?
- [ ] NamedType<InteriorTestNode> CRTP pattern present?

---

## 2. MergeTestNode (Multi-Input to Single Output)

**Purpose**: Tests `MergeNode<N, InputType, OutputT, Derived>` with 2 input ports merging to 1 output.

**Inheritance**:
```cpp
class MergeTestNode 
    : public graph::MergeNode<2, ::graph::message::Message, ::graph::message::Message, MergeTestNode>,
      public graph::NamedType<MergeTestNode>
```

**Port Definition**:
```cpp
static constexpr char kInput0[] = "In0";
static constexpr char kInput1[] = "In1";
static constexpr char kOutput[] = "Out";

using Ports = std::tuple<
    graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kInput0,
        graph::PayloadList<int>>,
    graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Input, kInput1,
        graph::PayloadList<int>>,
    graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutput,
        graph::PayloadList<int>>
>;
```

**Process Implementation**:
```cpp
// MergeNode calls Process(event, std::integral_constant<0>) for the single output
std::optional<::graph::message::Message> Process(
    const ::graph::message::Message& input,
    std::integral_constant<std::size_t, 0>) override {
    // Return the input as-is (simple pass-through merge)
    return input;
}
```

**Key Characteristics**:
- 2 input ports (`In0`, `In1`), 1 output port (`Out`)
- All messages are `Message<int>`
- Receives messages from either input via unified internal queue
- Process method returns `std::optional<Message>` (output produced or not)
- MergeThreadFunc dequeues and processes in background
- Tests thread-safe merging with internal unified queue

**Review Checklist**:
- [ ] Inheritance from `MergeNode<2, ...>` correct?
- [ ] Input port 0 and 1 both have PortDirection::Input?
- [ ] Output port 0 has PortDirection::Output?
- [ ] Process signature: `std::optional<Message> Process(const Message&, std::integral_constant<0>)`?
- [ ] Return type is `std::optional`?
- [ ] Ports tuple includes all 3 port specs (2 input + 1 output)?

---

## 3. SplitTestNode (Single Input to Multi-Output)

**Purpose**: Tests `SplitNode<T, N>` with 1 input port splitting to 2 output ports.

**Inheritance**:
```cpp
class SplitTestNode
    : public graph::SplitNode<::graph::message::Message, 2>
```

**Port Definition**:
```cpp
static constexpr char kInput[] = "In";
static constexpr char kOutput0[] = "Out0";
static constexpr char kOutput1[] = "Out1";

using Ports = std::tuple<
    graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Input, kInput,
        graph::PayloadList<int>>,
    graph::PortSpec<0, ::graph::message::Message, graph::PortDirection::Output, kOutput0,
        graph::PayloadList<int>>,
    graph::PortSpec<1, ::graph::message::Message, graph::PortDirection::Output, kOutput1,
        graph::PayloadList<int>>
>;
```

**Consume Implementation**:
```cpp
// Override Consume from SinkNode<Message>
// SplitNode will call Consume with integral_constant<0> (only input port)
bool Consume(const ::graph::message::Message& msg, 
             std::integral_constant<std::size_t, 0>) override {
    // SplitNode<T, 2> base class has:
    //   core::ActiveQueue<Message> input_queue_[2];
    // Distribute to both output queues
    bool success = true;
    success &= input_queue_[0].Enqueue(msg);
    success &= input_queue_[1].Enqueue(msg);
    return success;
}
```

**Key Characteristics**:
- 1 input port (`In`), 2 output ports (`Out0`, `Out1`)
- All messages are `Message<int>`
- Input is a SinkNode (receives from graph edges)
- Output queues are array: `input_queue_[0]`, `input_queue_[1]` (parent class members)
- Each input message is copied to both output queues
- Tests fan-out/replication behavior

**Review Checklist**:
- [ ] Inheritance from `SplitNode<Message, 2>` correct?
- [ ] Input port 0 has PortDirection::Input?
- [ ] Output ports 0 and 1 have PortDirection::Output?
- [ ] Consume signature: `bool Consume(const Message&, std::integral_constant<0>)`?
- [ ] Access to `input_queue_[0]` and `input_queue_[1]` from parent class?
- [ ] Ports tuple includes all 3 port specs (1 input + 2 output)?

---

## Summary for Review

| Node | Base Class | Inputs | Outputs | Key Test |
|------|-----------|--------|---------|----------|
| **InteriorTestNode** | `InteriorNodeBase<TypeList<...>>` | 1 | 1 | Synchronous transformation |
| **MergeTestNode** | `MergeNode<2, ...>` | 2 | 1 | Unified queue merging + thread |
| **SplitTestNode** | `SplitNode<T, 2>` | 1 | 2 | Message replication |

---

## Questions for User Review

1. **InteriorTestNode**: Should the Transform operation stay as doubling, or use a different transformation?
2. **MergeTestNode**: Should it test merging with modification (e.g., sum the integer values), or simple pass-through?
3. **SplitTestNode**: Should both outputs receive identical messages, or test selective routing?
4. **Ports Tuple**: Should output ports be included in the Ports tuple for metadata, or only input/output ports mentioned in base?

Once you approve these specifications, I'll implement:
- All 3 test node header definitions in `libgraph/test/include/test/AdvancedTestNodes.hpp`
- Comprehensive test suite in `libgraph/test/unit/test_advanced_nodes.cpp`
- Each node will have 2-3 unit tests covering initialization, message passing, and lifecycle
- Total: ~8-10 tests, estimated 100% pass rate
