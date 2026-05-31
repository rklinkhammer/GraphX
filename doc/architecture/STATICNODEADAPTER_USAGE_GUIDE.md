# StaticNodeAdapter Usage Guide
## Deduced Architecture Patterns from NodeFacade Ecosystem Analysis

**Date**: May 14, 2026  
**Status**: Architecture Reference Guide  
**Purpose**: Document correct usage patterns for StaticNodeAdapter and related wrapping mechanisms

---

## Executive Summary

The GraphX system has a sophisticated 3-layer wrapping architecture that bridges:
1. **C-compatible plugins** (NodeFacade) ↔ 
2. **Modern C++ abstractions** (NodeFacadeAdapter) ↔ 
3. **Graph integration** (NodeFacadeAdapterWrapper)

`StaticNodeAdapter` enables static C++ nodes to participate in this ecosystem, but has specific requirements for interface extraction and callback discovery.

---

## Architecture Overview

### The 3-Layer Wrapper Ecosystem

```
┌─────────────────────────────────────────────────────────────┐
│ Plugin/Dynamic Node Loading                                 │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  Plugin DLL/SO                                               │
│  ├─ NodeFacade (C struct, function pointers)                │
│  └─ Callbacks: GetAsICompletionCallback, GetAsIConfigurable │
│                                                               │
└─────────────────────────────────────────────────────────────┘
                            ↓
                   (via dlopen/dlsym)
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ StaticNodeAdapter::Adapt()                                  │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  Creates mock NodeFacade with lambda-bound function         │
│  pointers that delegate to actual INode methods             │
│                                                               │
│  Input:  shared_ptr<INode> (e.g., SourceTestNode)           │
│  Output: NodeFacadeAdapter (bridges C interface to C++)     │
│                                                               │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ NodeFacadeAdapterWrapper                                    │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  Wraps shared_ptr<NodeFacadeAdapter> to provide INode       │
│  interface for GraphManager::AddNode(shared_ptr<INode>)     │
│                                                               │
│  Enables interface extraction via GetAsNodeFacadeAdapter()  │
│  for CompletionPolicy and other infrastructure              │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Descriptions

### Component 1: NodeFacade (NodeFacade.hpp)

**What**: C-compatible interface struct with function pointers
**Where**: Static plugins export NodeFacade via C functions
**Pattern**: Allows C code to load and call C++ plugins

```cpp
struct NodeFacade {
    bool (*Init)(NodeHandle handle);
    bool (*Start)(NodeHandle handle);
    void (*Stop)(NodeHandle handle);
    void (*Join)(NodeHandle handle);
    // ... other lifecycle and metadata functions
    
    // Optional callbacks for interface discovery
    void* (*GetAsICompletionCallback)(...);
    void* (*GetAsIConfigurable)(...);
    // ... other interface pointers
};
```

**Key Insight**: Callbacks are used to return interface pointers (as `void*`) for discovery

---

### Component 2: NodeFacadeAdapter (NodeFacade.hpp)

**What**: Modern C++ wrapper implementing INodeFacade
**Where**: Wraps either plugin NodeFacade OR StaticNodeAdapter-created facade
**Purpose**: Unified access to node functionality regardless of source (plugin vs static)
**Key Pattern**: Extracts and stores interface pointers at construction time

```cpp
class NodeFacadeAdapter : public INodeFacade {
private:
    NodeHandle handle_;                     // Plugin handle (or nullptr for static)
    const NodeFacade* facade_;              // Function pointers
    
    // Extracted interface pointers (discovered at construction)
    std::shared_ptr<void> configurable_ptr_;
    std::shared_ptr<void> completion_callback_provider_ptr_;
    std::shared_ptr<void> metrics_callback_provider_ptr_;
    // ... others
    
    void ExtractInterfaces();  // Called in constructor
};
```

**Critical**: `ExtractInterfaces()` is called during construction and uses callbacks to discover what interfaces the node supports.

---

### Component 3: StaticNodeAdapter (StaticNodeAdapter.hpp)

**What**: Adapter that makes static C++ nodes look like plugins
**Where**: Used when you have a typed C++ node (e.g., `SourceTestNode`)
**Pattern**: Creates a mock NodeFacade with lambda-bound functions
**Returns**: NodeFacadeAdapter (same as what a plugin would produce)

#### How It Works

```cpp
// User provides static node
auto source = std::make_shared<SourceTestNode>();

// StaticNodeAdapter wraps it to look like a plugin
auto adapter = StaticNodeAdapter::Adapt(source, "SourceTestNode");
// Returns: NodeFacadeAdapter
```

#### Implementation Pattern (Deduced)

```cpp
class StaticNodeAdapter::StaticNodeInstance {
    std::shared_ptr<INode> node_;  // Actual node being adapted
    NodeFacade facade_;            // Mock facade with lambda bindings
    
    void BuildFacade() {
        // Bind Init lambda that delegates to node_->Init()
        facade_.Init = [](NodeHandle h) -> bool {
            auto* instance = static_cast<StaticNodeInstance*>(h);
            return instance->node_->Init();
        };
        
        // Similarly bind Start, Stop, Join, etc.
        // And provide callback functions that extract interfaces:
        
        facade_.GetAsICompletionCallback = [](NodeHandle h) -> void* {
            auto* instance = static_cast<StaticNodeInstance*>(h);
            auto completion_node = std::dynamic_pointer_cast<
                ICompletionCallback<CompletionSignal>>(instance->node_);
            return completion_node.get();  // Return interface pointer
        };
        
        // ... bind other interface callbacks similarly
    }
};
```

**Key Insight**: The callbacks MUST cast the node and return interface pointers for discovery to work.

---

### Component 4: NodeFacadeAdapterWrapper (NodeFacadeAdapterWrapper.hpp)

**What**: Wraps NodeFacadeAdapter to implement INode interface  
**Where**: Used when you need to add wrapped nodes to GraphManager
**Pattern**: `shared_ptr<NodeFacadeAdapter>` → `shared_ptr<INode>`

```cpp
class NodeFacadeAdapterWrapper : public INode {
private:
    std::shared_ptr<NodeFacadeAdapter> adapter_;
public:
    bool Init() override { return adapter_->Init(); }
    bool Start() override { return adapter_->Start(); }
    // ... delegate all INode methods to adapter_
};
```

**Enables**: Wrapped nodes can be discovered by `GetAsNodeFacadeAdapter()` for callback installation.

---

## Pattern Flows

### Flow 1: Plugin Loading (Full Ecosystem)

```
Plugin DLL
    ↓ (dlopen/dlsym)
NodeFacade struct (from plugin)
    ↓ (passed to constructor)
NodeFacadeAdapter (extracts interfaces via callbacks)
    ↓ (wrapped in shared_ptr)
NodeFacadeAdapterWrapper (adds INode interface)
    ↓ (added to graph)
GraphManager
    ↓ (during Init)
CompletionPolicy::InitCompletionCallbacks()
    ↓ (GetAsNodeFacadeAdapter → found → GetCompletionCallbackProviderPtr())
Callback provider installation ✓
```

### Flow 2: Static Node with Wrapping (Proposed for Stage 5.5a)

```
SourceTestNode (static C++ class)
    ↓ (shared_ptr)
StaticNodeAdapter::Adapt() 
    ↓ (creates mock facade, returns)
NodeFacadeAdapter (extracts interfaces via StaticNodeAdapter callbacks)
    ↓ (wrapped in shared_ptr)
NodeFacadeAdapterWrapper (adds INode interface)
    ↓ (added to graph via AddNode(shared_ptr<INode>))
GraphManager
    ↓ (during Init)
CompletionPolicy::InitCompletionCallbacks()
    ↓ (GetAsNodeFacadeAdapter → found → GetCompletionCallbackProviderPtr())
Callback provider installation ✓
```

### Flow 3: Static Node WITHOUT Wrapping (Current for Tests)

```
SourceTestNode (static C++ class)
    ↓ (AddNode<SourceTestNode>())
GraphManager stores as INode
    ↓ (no NodeFacadeAdapter wrapper)
CompletionPolicy::InitCompletionCallbacks()
    ↓ (GetAsNodeFacadeAdapter → nullptr → skipped ✗)
Callback provider NOT installed
    ↓ (SinkTestNode::SignalCompletion() has no provider)
IsCompletionSignaled() returns false ✗
```

---

## Edge Creation Challenge

### Issue: Type Mismatch with Wrapped Nodes

When using `AddNode<T>()` (typed version):
```cpp
auto source = graph->AddNode<SourceTestNode>();
auto sink = graph->AddNode<SinkTestNode>();
graph->AddEdge<SourceTestNode, 0, SinkTestNode, 0>(source, sink);  // ✓ Works
```

When using wrapped nodes with `AddNode(shared_ptr<INode>)` (non-typed):
```cpp
auto wrapper_source = std::make_shared<NodeFacadeAdapterWrapper>(...);
graph->AddNode(wrapper_source);  // Adds as INode, loses type info
// ✗ Can't use AddEdge<SourceTestNode, 0, ...> because source is now NodeFacadeAdapterWrapper
```

### Solutions

#### Option A: Manual Edge Registry Registration

Pre-register edge creators with EdgeRegistry:
```cpp
// In test setup
EdgeRegistry::Register<SourceTestNode, 0, SinkTestNode, 0>(
    "SourceTestNode", "SinkTestNode",
    [](GraphManager& g, size_t src_idx, size_t dst_idx, size_t buf) {
        auto src = std::dynamic_pointer_cast<SourceTestNode>(g.GetNode(src_idx));
        auto dst = std::dynamic_pointer_cast<SinkTestNode>(g.GetNode(dst_idx));
        g.AddEdge<SourceTestNode, 0, SinkTestNode, 0>(src, dst, buf);
        return true;
    });

// Then in topology
EdgeRegistry::CreateEdge(graph, "SourceTestNode", 0, "SinkTestNode", 0,
                        src_idx, dst_idx, 8);
```

#### Option B: Keep Nodes Unwrapped (Simplest for Now)

```cpp
// Create nodes normally (unwrapped)
auto source = graph->AddNode<SourceTestNode>();
auto sink = graph->AddNode<SinkTestNode>();

// Add edges normally
graph->AddEdge<SourceTestNode, 0, SinkTestNode, 0>(source, sink);

// Manually install callbacks AFTER graph construction (before executor Init)
auto completion_callback = std::make_shared<ICompletionCallback::CompletionNodeCallback>();
completion_callback->SetOnComplete([...] { /* signal completion */ });
sink->SetCallbackProvider(completion_callback.get());
```

---

## Recommendations for Stage 5.5a

### Immediate (Unblock Tests)

**Use Option B (Manual Callback Installation)**:
1. Keep nodes unwrapped: `AddNode<SourceTestNode>()`, `AddEdge<...>()`
2. Create helper function: `InstallCompletionCallbacks(graph)`
3. Call in test setup BEFORE executor Init
4. Document as temporary workaround

**Benefits**: 
- No complex wrapping
- Unblocks completion testing immediately
- No EdgeRegistry coordination needed
- Clear upgrade path documented

### Phase 2 (Long-term Architecture)

**Migrate to Option A (Full Wrapping)**:
1. Pre-register all test edge types with EdgeRegistry
2. Wrap test nodes in StaticNodeAdapter + NodeFacadeAdapterWrapper
3. Use EdgeRegistry::CreateEdge for all edge creation
4. CompletionPolicy discovers wrapped nodes automatically

**Benefits**:
- Unified plugin and static node architecture
- Better alignment with production systems
- No special test-only code paths
- Foundation for dynamic graph loading

---

## Reference Implementation

### Header: WrapperHelper.hpp

```cpp
#pragma once
#include "graph/GraphManager.hpp"
#include "graph/StaticNodeAdapter.hpp"
#include "graph/NodeFacadeAdapterWrapper.hpp"
#include "graph/ICompletionCallback.hpp"

namespace test {

/**
 * Wrap a static node to enable CompletionPolicy discovery
 */
template<typename NodeT>
std::shared_ptr<NodeT> WrapNodeForCompletion(std::shared_ptr<NodeT> node) {
    // Get adapter
    auto adapter = StaticNodeAdapter::Adapt(
        std::static_pointer_cast<graph::INode>(node),
        typeid(NodeT).name());
    
    // Wrap adapter in shared_ptr
    auto wrapped_adapter = std::make_shared<graph::NodeFacadeAdapter>(adapter);
    
    // Wrap in NodeFacadeAdapterWrapper (but still get original typed node)
    // NOTE: This is the tricky part - we need both the wrapper AND the original type
    // Solution: Return original node, but ensure it's been "marked" somehow
    
    return node;  // Still return typed node for AddEdge compatibility
}

}
```

**The Challenge**: We need the original typed node for `AddEdge<>()` templates, but also need the wrapping for callback discovery. This is why Option A (separate edge registration) is needed for wrapped approach.

---

## Implementation Files Reference

| File | Lines | Purpose |
|------|-------|---------|
| NodeFacade.hpp | 100-600 | NodeFacade struct + NodeFacadeAdapter class |
| StaticNodeAdapter.hpp | 20-180 | StaticNodeAdapter class definition |
| StaticNodeAdapter.cpp | 39-100 | StaticNodeInstance implementation |
| NodeFacadeAdapterWrapper.hpp | 50-150 | Wrapper class for INode compatibility |
| CompletionPolicy.cpp | 40-95 | Callback discovery logic (requires GetAsNodeFacadeAdapter) |
| GraphBuilder.cpp | 257-260 | Example of wrapping pattern |

---

## Conclusion

The correct way to use `StaticNodeAdapter`:

1. **For Static Nodes**: Creates NodeFacadeAdapter from INode
2. **For Callback Discovery**: Requires NodeFacadeAdapterWrapper wrapping  
3. **For Edge Compatibility**: Creates type mismatch requiring EdgeRegistry approach
4. **Recommendation**: Start simple (Option B), migrate to full wrapping (Option A) later

