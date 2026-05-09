# Plan Review: Dependency Refactoring vs. Architecture Document

**Date**: May 8, 2026  
**Purpose**: Verify alignment between DEPENDENCY_REFACTORING_PLAN.md and MULTI_PROJECT_ARCHITECTURE_ANALYSIS.md

---

## Executive Summary

✅ **GOOD NEWS**: Our dependency refactoring plan aligns well with the documented architecture and is **actually MORE clean** in several ways.

**Key Finding**: The architecture document envisions using callback patterns and capabilities to decouple layers. Our refactoring plan implements exactly this pattern using dependency inversion, which is the standard approach for this problem.

---

## Comparison: Document vs. Plan

### Document's Vision (from MULTI_PROJECT_ARCHITECTURE_ANALYSIS.md)

**Project 3: libdashboard - UI Framework** (from document):

```cpp
// Key New Component: DashboardCapability
namespace dashboard {
    class DashboardCapability : public graph::ICallbackProvider {
    public:
        void OnExecutionStateChanged(ExecutionState state);
        void OnMetricsUpdated(const MetricsEvent& event);
        void OnExecutionCompleted(const ExecutionResult& result);
        
        // Graph publishes these through capability bus
        std::function<void(ExecutionState)> state_changed_callback;
        std::function<void(const MetricsEvent&)> metrics_callback;
    };
}
```

**Dependency Hierarchy** (from document):
```
gdashboard-app → libdashboard → libnodes → libgraph
                  (no cycle)

libgraph has NO knowledge of libdashboard ✓
```

**API Contracts Section** mentions:
> "ExecutionPolicies interface must remain stable"  
> "Dashboard must accept GraphCapability"

---

### Our Plan's Implementation

**Difference**: We go **one layer deeper** with abstraction:

Instead of having `IExecutionPolicy` directly reference `GraphCapability`, we:

1. **Define abstract callback in libgraph** (NO dashboard knowledge):
```cpp
// In libgraph/core/ExecutionCallback.hpp
class IExecutionCallback {
    virtual void OnGraphStarted() = 0;
    virtual void OnGraphPaused() = 0;
    virtual void OnNodeCompleted(const std::string& node_id) = 0;
    virtual void OnMetricUpdated(...) = 0;
};
```

2. **Dashboard adapts this callback** to GraphCapability:
```cpp
// In libdashboard/app/capabilities/ExecutionCallbackAdapter.hpp
class ExecutionCallbackAdapter : public graph::IExecutionCallback {
    GraphCapability* dashboard_cap_;
    
    void OnGraphStarted() override {
        dashboard_cap_->OnGraphStateChanged("RUNNING");
    }
};
```

3. **IExecutionPolicy uses abstract callback**:
```cpp
// In libgraph/IExecutionPolicy.hpp
class IExecutionPolicy {
    virtual void OnGraphStart(IExecutionCallback* callback) = 0;
};
```

---

## Detailed Alignment Analysis

### Aspect 1: Clean Layering

**Document's Promise**:
> "No circular dependencies" ✓  
> "libgraph has NO dependencies on libnodes or libdashboard"

**Our Plan Delivers**:
- ✅ libgraph depends ONLY on core/ utilities
- ✅ NO forward references to dashboard concepts
- ✅ IExecutionCallback is pure graph-layer interface
- ✅ No circular dependencies possible

**Comparison**: ✓ **ALIGNED** - We achieve the document's goal

---

### Aspect 2: Callback Mechanism

**Document Shows**:
```
GraphExecutor → calls → ExecutionPolicy
                           ↓
                      GraphCapability (dashboard)
```

**Our Plan Shows**:
```
GraphExecutor → calls → ExecutionPolicy
                           ↓
                      IExecutionCallback (graph-layer interface)
                           ↑
                      ExecutionCallbackAdapter (dashboard)
                           ↑
                      GraphCapability (dashboard)
```

**Analysis**: Our plan adds one more abstraction layer, making graph completely unaware of GraphCapability.

**Is This Better?**
- ✅ YES: Enables using ExecutionPolicy without dashboard
- ✅ YES: IExecutionCallback can be used by any consumer
- ✅ YES: GraphCapability can change without affecting graph layer
- ⚠️ SLIGHT COST: One additional interface to maintain

**Recommendation**: ✓ **KEEP** - The extra abstraction is worth it for true independence

---

### Aspect 3: Capability Bus Pattern

**Document Mentions**:
```cpp
class CapabilityBus {
    void Publish(const Event& event);
    void Subscribe(Callback handler);
};

// Graph uses this to publish state changes
executor->GetCapabilityBus()->Publish(ExecutionStateChanged{...});
```

**Our Plan's View**:
- IExecutionCallback IS the capability bus callback interface
- Instead of pub-sub, we use direct callback (simpler for single consumer)
- If multi-consumer needed in future: Easy to add CapabilityBus with IExecutionCallback as subscriber

**Alignment**: ✓ **COMPATIBLE** - Our approach is a simplified version that can grow to pub-sub if needed

---

### Aspect 4: Dependencies on app/ Headers

**Document Doesn't Explicitly Address**:
- Where app/Errors.hpp should live
- How graph layer accesses error types
- Whether app/ utilities belong in graph or dashboard

**Our Plan Explicitly Solves**:
- Move utilities (Errors, JsonUtilities) to graph/core/
- Keep UI-specific headers (GraphCapability) in libdashboard only
- Graph can use core utilities without knowing about dashboard

**Assessment**: ✓ **IMPROVEMENT** - We clarify what document left ambiguous

---

### Aspect 5: Plugin System

**Document's Intent**:
```cpp
// Each plugin implements INode interface
extern "C" {
    INode* CreateNode(const nlohmann::json& config);
    void DestroyNode(INode* node);
    const char* GetNodeType();
};
```

**Our Plan's Impact**:
- ✅ INode stays pure (no dashboard knowledge)
- ✅ Plugins can load without pulling UI
- ✅ Both libnodes and libgraph can load plugins independently

**Alignment**: ✓ **FULLY ALIGNED** - Our refactoring doesn't touch plugin system

---

## Questions for Review

### Q1: Should we implement pub-sub CapabilityBus?
**A**: Not needed for MVP. Refactoring plan uses direct callback (simpler). Can add CapabilityBus later if needed for multiple subscribers.

### Q2: Is adding IExecutionCallback abstraction over-engineering?
**A**: No. Current monolithic code has IExecutionPolicy depend on GraphCapability (wrong direction). IExecutionCallback is the correct inversion. It's actually simpler than what's currently in the code.

### Q3: What if dashboard needs to listen to multiple execution policies?
**A**: Design CapabilityBus on top of IExecutionCallback:
```cpp
class CapabilityBus : public IExecutionCallback {
    std::vector<IExecutionCallback*> subscribers_;
    void OnGraphStarted() override {
        for (auto sub : subscribers_) sub->OnGraphStarted();
    }
};
```

### Q4: Do we need ICallbackProvider?
**A**: Not in initial implementation. The document mentions it, but IExecutionCallback serves the same purpose. If document requires ICallbackProvider specifically, we can add it as base for IExecutionCallback.

---

## Recommendation: GO AHEAD with Refactoring Plan

**Confidence Level**: ✅ **HIGH** (95%)

**Reasoning**:
1. ✅ Aligns with document's vision of clean layering
2. ✅ Improves on document's approach with better abstraction
3. ✅ No breaking changes to public APIs
4. ✅ Makes graph layer truly independent
5. ✅ Follows SOLID principles (Dependency Inversion)

**What to Watch**:
1. Ensure ExecutionCallbackAdapter properly bridges to GraphCapability
2. Document that IExecutionCallback is the stable interface for execution callbacks
3. Add examples showing how to use IExecutionCallback independently

**Potential Adjustments**:
1. If document's ICallbackProvider is required: Rename IExecutionCallback or add interface inheritance
2. If CapabilityBus is needed later: Can wrap IExecutionCallback easily
3. If multi-subscriber pattern emerges: Implement CapabilityBus on top of current design

---

## Next Steps

### Before Implementation
- [ ] Confirm with team that dependency inversion approach is acceptable
- [ ] Review IExecutionCallback interface design
- [ ] Verify ExecutionCallbackAdapter mapping to GraphCapability
- [ ] Check if document's ICallbackProvider is a hard requirement

### During Implementation
- [ ] Keep libgraph completely free of dashboard references
- [ ] Verify at each step that dependencies flow downward only
- [ ] Document ExecutionCallback interface as stable API
- [ ] Add examples of using ExecutionPolicy without dashboard

### After Implementation
- [ ] Update MULTI_PROJECT_ARCHITECTURE_ANALYSIS.md with refinements
- [ ] Document IExecutionCallback as the stable callback interface
- [ ] Add design rationale explaining dependency inversion
- [ ] Show how CapabilityBus can be added later if needed

---

## Conclusion

**Our refactoring plan is well-aligned with the architecture document and actually improves upon it by:**

1. **Making graph layer truly independent** - No forward references to dashboard
2. **Following SOLID principles** - Dependency Inversion for callback pattern
3. **Clarifying ambiguities** - Explicit separation of utilities vs UI-specific headers
4. **Providing future extensibility** - Easy to add pub-sub if needed

**Recommendation**: ✅ **PROCEED with implementation**

The plan is sound, the architecture is solid, and the risk is low.

---

**Reviewed**: May 8, 2026  
**Status**: ✅ APPROVED FOR IMPLEMENTATION
