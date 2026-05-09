# Phase C: Fix Include Paths in Source Files - COMPLETE ✅

**Date**: May 8, 2026  
**Status**: Complete  
**Duration**: ~10 minutes  
**Source Files Updated**: 48 files

---

## What Was Done

### libgraph/src/ (16 .cpp files)
**Transformations**:
- `#include "core/..."` → `#include "graph/core/..."` (utilities in graph layer)
- `#include "app/..."` → `#include "graph/app/..."` (graph-specific in app folder)

**Example**:
```cpp
// Before
#include "app/policies/MetricsPolicy.hpp"
#include "core/ActiveQueue.hpp"

// After
#include "graph/app/policies/MetricsPolicy.hpp"
#include "graph/core/ActiveQueue.hpp"
```

**Files Updated**:
- GraphConfigParser.cpp, GraphExecutor.cpp, GraphExecutorBuilder.cpp
- JsonDynamicGraphLoader.cpp, NodeFacade.cpp, NodeFactory.cpp
- And 10 more...

### libdashboard/src/ (9 .cpp files)
**Transformations**:
- `#include "core/..."` → `#include "graph/core/..."` (refers to graph's utilities)
- `#include "app/..."` → `#include "dashboard/..."` (refers to dashboard's layer)

**Example**:
```cpp
// Before
#include "app/JsonUtilities.hpp"
#include "core/FormatUtilities.hpp"

// After
#include "dashboard/JsonUtilities.hpp"
#include "graph/core/FormatUtilities.hpp"
```

**Files Updated**:
- JsonUtilities.cpp, BuiltinCommands.cpp, Dashboard.cpp
- CommandRegistry.cpp, FactoryManager.cpp, GraphBuilder.cpp
- And more...

### libnodes/src/ (23 .cpp files)
**Transformations**:
- `#include "core/..."` → `#include "graph/core/..."` (graph layer utilities)
- `#include "app/..."` → `#include "graph/app/..."` (graph layer features)

**Example**:
```cpp
// Before
#include "app/capabilities/GraphCapability.hpp"
#include "core/VariantHelper.hpp"

// After
#include "graph/app/capabilities/GraphCapability.hpp"
#include "graph/core/VariantHelper.hpp"
```

**Files Updated**:
- CSVDataInjectionManager.cpp, EKFCore.cpp
- Multiple estimators and adapters...

---

## Verification Results

### ✅ libgraph/src/ (16 files)
- No remaining `#include "core/..."`
- No remaining `#include "app/..."`
- All references correctly prefixed with `graph/`

### ✅ libdashboard/src/ (9 files)
- No remaining `#include "core/..."` (now `graph/core/`)
- No remaining `#include "app/"` (now `dashboard/`)
- All external refs correctly point to `graph/`

### ✅ libnodes/src/ (23 files)
- No remaining `#include "core/..."`
- No remaining `#include "app/..."`
- All graph dependencies correctly prefixed

---

## Files Changed Summary

| Project | Count | Status |
|---------|-------|--------|
| libgraph/src | 16 | ✅ Updated |
| libdashboard/src | 9 | ✅ Updated |
| libnodes/src | 23 | ✅ Updated |
| **Total** | **48** | ✅ All Fixed |

---

## Consistency Check

Now **both header and source includes are aligned**:

**Headers (Phase B)** + **Sources (Phase C)** = **Unified Include Paths**

```cpp
// All files now use consistent paths

// Graph layer
#include "graph/core/ActiveQueue.hpp"
#include "graph/app/capabilities/GraphCapability.hpp"

// Dashboard layer
#include "dashboard/capabilities/MetricsCapability.hpp"
#include "graph/core/FormatUtilities.hpp"  // External dependency
```

---

## Git History

```
c39c4af Phase C Complete: Fix include paths in source files
f24a3ca Add Phase B summary documentation
cea82da Phase B Complete: Fix include paths in migrated headers
```

---

## Next Steps: Phase D

**Phase D: Refactor IExecutionPolicy Interface**

This phase addresses the **critical circular dependency issue** where graph layer can "see" dashboard concepts.

### Changes Required:
1. Create `libgraph/include/graph/core/ExecutionCallback.hpp`
   - New interface: `IExecutionCallback`
   - Pure virtual methods: `OnGraphStarted()`, `OnNodeCompleted()`, `OnMetricUpdated()`, etc.
   - No dashboard knowledge

2. Update `IExecutionPolicy.hpp`
   - Change from depending on `GraphCapability`
   - Use `IExecutionCallback*` instead
   - Maintains pure execution abstraction

3. Create `ExecutionCallbackAdapter` in libdashboard
   - Bridges `IExecutionCallback` → `GraphCapability`
   - Keeps graph layer completely decoupled from dashboard

4. Update `GraphExecutor.cpp`
   - Pass callback to policies instead of capability

### Expected Impact:
- ✅ libgraph becomes completely dashboard-independent
- ✅ Clear dependency flow: dashboard → graph (no upward refs)
- ✅ All 230+ tests should still pass
- ✅ System architecture fully enforced

### Estimated Time:
- Design review: 10 min
- Implementation: 45 min
- Testing: 15 min
- Total: ~70 minutes

---

**Status**: ✅ Ready for Phase D (Dependency Inversion Refactoring)
