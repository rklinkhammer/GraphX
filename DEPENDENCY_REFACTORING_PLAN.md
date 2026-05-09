# Dependency Refactoring Plan

**Date Created**: May 8, 2026  
**Status**: Planning Document (Pre-Implementation)  
**Priority**: High - Blocks compilation in Stage 4+

---

## Executive Summary

During Stage 3 source migration, we discovered 4 critical dependency violations that prevent compilation:

| Issue | Severity | Root Cause | Impact |
|-------|----------|-----------|--------|
| Core utilities scattered | HIGH | Mixed into app/ folder | Incomplete header organization |
| app/ headers mixed | HIGH | Single folder, 3 purposes | Upward dependencies in graph → app |
| Graph depends on Dashboard | **CRITICAL** | GraphCapability in IExecutionPolicy | Breaks layering model |
| Unresolved dependencies | HIGH | Missing header copies | Compilation failures |

---

## Architectural Model

### Target Layering (Clean Dependency Graph)

```
┌─────────────────────────────────────────┐
│         gdashboard-app (executable)     │
└────────────────┬────────────────────────┘
                 │ depends on
                 ▼
┌─────────────────────────────────────────┐
│      libdashboard (UI framework)        │
│  - MetricsPanel, CommandWindow          │
│  - Dashboard lifecycle                  │
│  - GraphCapability (UI callbacks)       │
└────────────────┬────────────────────────┘
                 │ depends on
                 ▼
┌─────────────────────────────────────────┐
│   libnodes (Node implementations)       │
│  - Avionics, CSV, DSP nodes             │
│  - Estimators, adapters                 │
└────────────────┬────────────────────────┘
                 │ depends on
                 ▼
┌─────────────────────────────────────────┐
│    libgraph (Pure execution engine)     │
│  - INode, GraphExecutor, Policies       │
│  - Edges, Registry, Message pools       │
│  - NO UI/Dashboard dependencies         │
└────────────────┬────────────────────────┘
                 │ depends on
                 ▼
┌─────────────────────────────────────────┐
│      core (Pure utilities)              │
│  - FormatUtilities, CallbackUtilities   │
│  - TypeInfo, Reflection helpers         │
│  - NO graph/nodes/ui dependencies       │
└─────────────────────────────────────────┘
```

**Key Rules**:
- ✓ Dependencies flow DOWN (app → graph → core)
- ✗ NO upward dependencies (graph should not know about ui)
- ✓ core is completely independent
- ✓ Each layer depends only on layers below

---

## Issue 1: Core Utilities Scattered

### Current State

```
dashboard/include/
  ├── core/                    ← Pure utilities (9 files)
  │   ├── CallbackUtilities.hpp
  │   ├── FormatUtilities.hpp  ← Used by app/Errors.hpp
  │   ├── ReflectionHelper.hpp
  │   └── ... (6 more)
  │
  └── app/                     ← Mixed: utilities + UI-specific
      ├── Errors.hpp          ← Uses core/FormatUtilities
      ├── JsonUtilities.hpp   ← Pure utility
      └── GraphCapability.hpp ← UI-specific (execution callback)
```

### Problem

- `core/` exists but isn't properly isolated
- `app/Errors.hpp` reaches "down" to core (correct)
- But `core/` and `app/` utilities are both needed by graph layer
- Migration copied both, creating unclear boundaries

### Solution

**Step 1.1**: Create dedicated `core/` package in libgraph
```
libgraph/include/graph/core/
  ├── CallbackUtilities.hpp
  ├── FormatUtilities.hpp    ← Add if missing
  ├── ReflectionHelper.hpp
  ├── TypeInfo.hpp
  ├── VariantHelper.hpp
  ├── RangesUtilities.hpp
  ├── PluginReflection.hpp
  ├── Expected.hpp
  └── ActiveQueue.hpp
```

**Step 1.2**: Identify utility-only headers from app/

```cpp
// UTILITIES (graph needs these - move to core/)
- Errors.hpp           // Error type definitions
- JsonUtilities.hpp    // JSON helpers
- FormatUtilities.hpp  // String formatting

// UI-SPECIFIC (dashboard only - keep in libdashboard/)
- GraphCapability.hpp  // Dashboard callback interface
- ExecutionPolicy.hpp  // (if UI-specific)
- MetricsListener.hpp  // (if dashboard-specific)
```

**Step 1.3**: Move utility headers to core

```bash
# In libgraph
cp dashboard/include/app/Errors.hpp → include/graph/core/
cp dashboard/include/app/JsonUtilities.hpp → include/graph/core/
cp dashboard/include/core/FormatUtilities.hpp → include/graph/core/

# Update includes in moved headers
app/Errors.hpp:
  - #include "core/FormatUtilities.hpp"  
  + #include "graph/core/FormatUtilities.hpp"
```

**Step 1.4**: Update source includes

```bash
# In all .cpp files using Errors.hpp
cd libgraph/src
find . -name "*.cpp" -exec sed -i '' \
  's|#include "app/Errors.hpp"|#include "graph/core/Errors.hpp"|g' {} \;

cd libnodes/src
find . -name "*.cpp" -exec sed -i '' \
  's|#include "app/JsonUtilities.hpp"|#include "graph/core/JsonUtilities.hpp"|g' {} \;
```

**Verification**:
```bash
# No app/ includes in graph layer
grep -r "#include \"app/" libgraph/include/ libgraph/src/

# All core/ imports have graph/ prefix
grep -r "#include \"core/" libgraph/include/ libgraph/src/
```

---

## Issue 2: app/ Headers Mixed Responsibilities

### Current State

```cpp
// After Stage 3 migration
libgraph/include/graph/app/
  ├── Errors.hpp              // Utility - should be in core/
  ├── JsonUtilities.hpp       // Utility - should be in core/
  └── GraphCapability.hpp     // UI-specific - should be in libdashboard/

libdashboard/include/app/
  // Currently EMPTY - needs to be populated
```

### Problem

- Utilities mixed with UI-specific interfaces
- Both copied to libgraph (wrong: upward dependency)
- libdashboard gets Capabilities but lost utility headers
- Creates confusion about what each layer needs

### Solution

**Step 2.1**: Separate responsibilities

```
UTILITIES (pure, no dependencies)
  ├── Errors.hpp
  ├── JsonUtilities.hpp
  ├── FormatUtilities.hpp
  └── TypeInfo helpers
  → Destination: libgraph/include/graph/core/

UI-SPECIFIC (dashboard concepts)
  ├── GraphCapability.hpp
  ├── MetricsCapability.hpp
  ├── Execution policies (UI-specific variants)
  └── Callback interfaces
  → Destination: libdashboard/include/app/capabilities/
```

**Step 2.2**: Update libdashboard includes

```bash
# In libdashboard/include/app/
# Remove utilities (they're now in core/)
rm Errors.hpp JsonUtilities.hpp

# Create capability subdirectories
mkdir -p libdashboard/include/app/capabilities/
mkdir -p libdashboard/include/app/policies/

# Move UI-specific headers
cp dashboard/include/app/GraphCapability.hpp → libdashboard/include/app/capabilities/
```

**Step 2.3**: Update references in libdashboard

```cpp
// Before
#include "app/Errors.hpp"
#include "app/GraphCapability.hpp"

// After
#include "graph/core/Errors.hpp"           // From libgraph
#include "app/capabilities/GraphCapability.hpp"  // Local
```

**Verification**:
```bash
# graph/ should NOT include anything from app/
grep -r "#include \"app/" libgraph/include/ libgraph/src/

# dashboard/ should not have utilities
ls libdashboard/include/app/Errors.hpp 2>/dev/null && echo "ERROR: Still exists!"
```

---

## Issue 3: Graph Layer Depends on Dashboard (CRITICAL)

### Current State

```cpp
// libgraph/include/graph/IExecutionPolicy.hpp
#include "graph/app/capabilities/GraphCapability.hpp"

class IExecutionPolicy {
    virtual void OnGraphStart(GraphCapability* cap) = 0;
    virtual void OnNodeExecute(GraphCapability* cap) = 0;
};
```

### Problem

- IExecutionPolicy is graph-layer interface
- GraphCapability is dashboard/UI abstraction
- **Graph library now depends on dashboard concepts** ✗
- Violates layering: Dashboard should depend on Graph, not vice versa
- Prevents libgraph from being compiled independently

### Root Cause

In monolithic structure, GraphCapability was used for callbacks:
```
GraphExecutor → calls → IExecutionPolicy → calls → GraphCapability
                                           ↑
                                      Points to UI/Dashboard
```

### Solution: Dependency Inversion

**Step 3.1**: Create abstract callback in graph layer

```cpp
// libgraph/include/graph/core/ExecutionCallback.hpp
#pragma once

namespace graph {

// Pure virtual callback - NO dashboard dependencies
class IExecutionCallback {
public:
    virtual ~IExecutionCallback() = default;
    
    // These are graph-level events, not dashboard-specific
    virtual void OnGraphStarted() = 0;
    virtual void OnGraphPaused() = 0;
    virtual void OnGraphStopped() = 0;
    virtual void OnNodeStarted(const std::string& node_id) = 0;
    virtual void OnNodeCompleted(const std::string& node_id) = 0;
    virtual void OnMetricUpdated(const std::string& node_id, 
                                 const std::string& metric_name, 
                                 double value) = 0;
};

} // namespace graph
```

**Step 3.2**: Update IExecutionPolicy to use callback

```cpp
// libgraph/include/graph/IExecutionPolicy.hpp - BEFORE
class IExecutionPolicy {
    virtual void OnGraphStart(GraphCapability* cap) = 0;  // ✗ Dashboard concept
};

// libgraph/include/graph/IExecutionPolicy.hpp - AFTER
class IExecutionPolicy {
    virtual void OnGraphStart(IExecutionCallback* callback) = 0;  // ✓ Graph concept
};
```

**Step 3.3**: Remove dashboard includes from graph layer

```bash
# Search for GraphCapability imports
grep -r "GraphCapability" libgraph/include/ libgraph/src/

# Remove all dashboard-specific includes
sed -i '' '/GraphCapability/d' libgraph/include/graph/IExecutionPolicy.hpp
sed -i '' '/app\/capabilities/d' libgraph/include/graph/*.hpp
```

**Step 3.4**: Implement dashboard-side adapter

```cpp
// libdashboard/include/app/capabilities/ExecutionCallbackAdapter.hpp
#pragma once
#include "graph/core/ExecutionCallback.hpp"  // ✓ Depends on graph
#include "GraphCapability.hpp"  // Dashboard-specific

namespace app {
namespace capabilities {

// Dashboard adapts graph's callback to its GraphCapability
class ExecutionCallbackAdapter : public graph::IExecutionCallback {
private:
    GraphCapability* dashboard_cap_;
    
public:
    ExecutionCallbackAdapter(GraphCapability* cap) 
        : dashboard_cap_(cap) {}
    
    void OnGraphStarted() override {
        dashboard_cap_->OnGraphStateChanged("RUNNING");
    }
    
    void OnNodeCompleted(const std::string& node_id) override {
        dashboard_cap_->OnNodeMetricsUpdate(node_id, {});
    }
    // ... other overrides
};

} // namespace capabilities
} // namespace app
```

**Step 3.5**: Update GraphExecutor to accept callback

```cpp
// libgraph/src/graph/GraphExecutor.cpp - BEFORE
void GraphExecutor::Execute(GraphCapability* cap) {  // ✗ Dashboard type
    policy_->OnGraphStart(cap);
}

// libgraph/src/graph/GraphExecutor.cpp - AFTER
void GraphExecutor::Execute(IExecutionCallback* callback) {  // ✓ Graph type
    policy_->OnGraphStart(callback);
}
```

**Verification**:
```bash
# graph/ should have NO dashboard dependencies
grep -r "capabilities\|Capability" libgraph/include/ libgraph/src/ | \
  grep -v "graph/core/ExecutionCallback"

# Should return empty if successful
```

---

## Issue 4: Unresolved Header Dependencies

### Current State

```
Missing during compilation:
- core/FormatUtilities.hpp (referenced by Errors.hpp)
- Some header transitively includes files not migrated
```

### Solution

**Step 4.1**: Audit all app/ headers

```bash
# Find all headers in app/ that reference other headers
cd dashboard/include/app
for f in *.hpp; do
    echo "=== $f ==="
    grep "#include \"" "$f" | grep -v system
done
```

**Step 4.2**: Trace dependency chain

```
Errors.hpp
  → core/FormatUtilities.hpp        ✓ Already copied
  → core/TypeInfo.hpp               ✓ Already copied
  
JsonUtilities.hpp
  → nlohmann/json.hpp               ✓ External (handled)
  → core/RangesUtilities.hpp        ✓ Already copied
```

**Step 4.3**: Copy missing headers to libgraph/include/graph/core/

```bash
# Verify all core/ headers exist
core_headers=(
    "ActiveQueue.hpp"
    "CallbackUtilities.hpp"
    "Expected.hpp"
    "FormatUtilities.hpp"
    "PluginReflection.hpp"
    "RangesUtilities.hpp"
    "ReflectionHelper.hpp"
    "TypeInfo.hpp"
    "VariantHelper.hpp"
)

for header in "${core_headers[@]}"; do
    if [ ! -f "libgraph/include/graph/core/$header" ]; then
        echo "Missing: $header"
        cp "dashboard/include/core/$header" "libgraph/include/graph/core/"
    fi
done
```

---

## Implementation Roadmap

### Phase A: Prepare (1-2 hours)

- [ ] Audit all app/ and core/ headers for dependencies
- [ ] Document which headers need to move/copy
- [ ] Create new directory structure in libgraph/include/graph/core/
- [ ] Create new directories in libdashboard/include/app/

### Phase B: Migrate Headers (2-3 hours)

- [ ] Copy all core/ headers to libgraph/include/graph/core/
- [ ] Copy utility headers (Errors, JsonUtilities) to libgraph/include/graph/core/
- [ ] Keep UI-specific headers in libdashboard/include/app/
- [ ] Create ExecutionCallback abstraction in libgraph/include/graph/core/

### Phase C: Update Includes (2-3 hours)

- [ ] Update all #include paths in libgraph (core/ → graph/core/)
- [ ] Update all #include paths in libgraph (app/* → graph/core/)
- [ ] Update all #include paths in libdashboard
- [ ] Update all #include paths in libnodes

### Phase D: Refactor Interfaces (2-4 hours)

- [ ] Replace GraphCapability parameters with IExecutionCallback
- [ ] Update IExecutionPolicy to use new callback
- [ ] Update all policy implementations
- [ ] Create ExecutionCallbackAdapter in libdashboard

### Phase E: Test Compilation (1-2 hours)

- [ ] Build libgraph independently
- [ ] Build libnodes independently
- [ ] Build libdashboard independently
- [ ] Build gdashboard-app
- [ ] Run full test suite

**Total Estimated Time**: 8-14 hours

---

## Implementation Order

### Stage-by-Stage Approach

**Stage 4a** (Part of Stage 4 - Test Migration):
1. Copy all missing core/ headers ✓
2. Create ExecutionCallback interface ✓
3. Create ExecutionCallbackAdapter ✓
4. Separate app/ headers into utilities vs UI-specific ✓

**Stage 4b** (Part of Stage 4 - Test Migration):
5. Update IExecutionPolicy to use IExecutionCallback
6. Update GraphExecutor implementation
7. Update all policy implementations in libnodes

**Stage 4c** (After Stage 4 - Tests pass):
8. Run full compilation test
9. Update test files if needed
10. Verify circular dependencies eliminated

---

## Validation Checklist

### Header Organization

- [ ] `libgraph/include/graph/core/` contains ONLY utilities
- [ ] `libgraph/include/graph/` contains NO app/ includes
- [ ] `libgraph/src/` contains NO app/ or ui/ includes
- [ ] `libdashboard/include/app/` contains NO utilities
- [ ] `libdashboard/include/app/` contains callback/policy interfaces

### Dependency Direction

- [ ] `libgraph` depends on ONLY: `core/`
- [ ] `libnodes` depends on: `libgraph`, `core/`
- [ ] `libdashboard` depends on: `libgraph`, `core/`
- [ ] NO reverse dependencies exist

### Compilation

- [ ] `libgraph` compiles independently: `cd build && make graph`
- [ ] `libnodes` compiles independently: `cd build && make nodes`
- [ ] `libdashboard` compiles independently: `cd build && make dashboard`
- [ ] All tests pass: `ctest`

### Code Quality

- [ ] No compiler warnings
- [ ] No unused includes
- [ ] No forward declarations needed (clean boundaries)
- [ ] All public interfaces documented

---

## Rollback Strategy

If refactoring causes issues, we can revert to pre-refactoring state:

```bash
# Before starting any Phase B-D changes
git tag -a before-dependency-refactor -m "Before refactoring"

# If needed, revert
git revert before-dependency-refactor
```

---

## Expected Outcomes

### Before Refactoring
```
Compilation errors:
✗ circular includes between graph and app
✗ Missing headers in libgraph
✗ Upward dependencies (graph depends on dashboard)
```

### After Refactoring
```
Compilation success:
✓ libgraph compiles independently
✓ libnodes compiles with libgraph
✓ libdashboard compiles with both
✓ gdashboard-app compiles with all three
✓ Clean layering: app → nodes → graph → core
```

### Code Quality Improvements
- Clear separation of concerns
- No circular dependencies
- Each layer independently testable
- Easier future maintenance and extension

---

## Notes

- ExecutionCallback is in `graph/core/` (belongs to graph layer)
- GraphCapability stays in `app/` (belongs to dashboard)
- Dashboard implements adapter pattern (standard dependency inversion)
- No functionality changes - just organizational refactoring
- All tests should pass without modifications (only #includes may change)

---

## Questions & Decisions

**Q**: Should ExecutionCallback be callback-style or listener-style?  
**A**: Currently callback (one object called). Could change to listener list if multiple consumers needed.

**Q**: Where should execution policy implementations live?  
**A**: In libnodes (they're specific to graph execution strategies). Stay there.

**Q**: Can we test this incrementally?  
**A**: Yes - start with core/ utilities, then callback abstraction, then policy refactoring.

**Q**: Will this break the original dashboard?  
**A**: No - only GraphX project is affected. Original dashboard/build continues working.

---

**Created**: May 8, 2026  
**Next Review**: After Phase A (Audit) is complete  
**Prepared By**: Copilot Agent (Modularization Task)
