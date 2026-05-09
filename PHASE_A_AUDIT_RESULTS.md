# Phase A: Dependency Audit Results

**Date**: May 8, 2026  
**Status**: Audit Complete  
**Purpose**: Document header dependencies for refactoring

---

## Header Inventory Summary

### core/ (9 headers - Pure utilities, NO dependencies on app/graph/ui)
```
include/core/
├── ActiveQueue.hpp              (Concurrency utility)
├── CallbackUtilities.hpp        (Callback helpers)
├── Expected.hpp                 (Error handling)
├── FormatUtilities.hpp          (String formatting)
├── PluginReflection.hpp         (Reflection)
├── RangesUtilities.hpp          (Range algorithms)
├── ReflectionHelper.hpp         (Type reflection)
├── TypeInfo.hpp                 (Type information)
└── VariantHelper.hpp            (Variant helpers)
```

**Assessment**: ✅ **ALL SHOULD MOVE TO libgraph/include/graph/core/**
- Pure utilities
- No knowledge of app/, graph/, or UI
- Used by both graph and app layers
- Safe to migrate as-is

---

### app/ Top Level (6 headers)

| Header | Purpose | Dependencies | **Category** | **Action** |
|--------|---------|--------------|------------|-----------|
| Errors.hpp | Error types | core/FormatUtilities | **Utility** | Move to core/ |
| JsonUtilities.hpp | JSON parsing | Errors.hpp, FormatUtilities | **Utility** | Move to core/ |
| FactoryManager.hpp | Node creation | (Check details) | TBD | Analyze |
| GraphBuilder.hpp | Graph construction | (Check details) | TBD | Analyze |
| JsonDeserialization.hpp | JSON deserialization | (Check details) | TBD | Analyze |
| SignalHandler.hpp | Signal handling | (Check details) | TBD | Analyze |

---

### app/capabilities/ (6 headers) - Dashboard/Graph-Specific

| Header | Depends On | **Category** | **Action** |
|--------|-----------|------------|-----------|
| GraphCapability.hpp | graph/CapabilityBus, GraphManager, NodeFactory, plugins | **Graph-Specific** | Stay in libgraph (app/ subfolder OR move to graph/) |
| MetricsCapability.hpp | app/metrics/*, CapabilityBus | **Dashboard-Specific** | Move to libdashboard |
| DashboardCapability.hpp | Dashboard UI concepts | **Dashboard-Specific** | Move to libdashboard |
| CommandCapability.hpp | UI commands | **Dashboard-Specific** | Move to libdashboard |
| CSVDataInjectionCapability.hpp | Graph execution, CSV | **Graph-Specific** | Move to libgraph |
| DataInjectionCapability.hpp | Graph execution | **Graph-Specific** | Move to libgraph |

---

### app/adapters/ (3 headers) - UI-Specific

| Header | Purpose | **Category** | **Action** |
|--------|---------|------------|-----------|
| CLIAdapter.hpp | Command-line interface | **Dashboard** | Move to libdashboard |
| TerminalUIAdapter.hpp | Terminal UI | **Dashboard** | Move to libdashboard |
| WebUIAdapter.hpp | Web UI | **Dashboard** | Move to libdashboard |

---

### app/clients/ (1 header) - Dashboard UI

| Header | Purpose | **Category** | **Action** |
|--------|---------|------------|-----------|
| DashboardRESTClient.hpp | REST API client | **Dashboard** | Move to libdashboard |

---

### app/interfaces/ (4 headers) - Shared Interfaces

| Header | Purpose | **Category** | **Action** |
|--------|---------|------------|-----------|
| DashboardCommand.hpp | Command interface | **Dashboard** | Move to libdashboard |
| ICommandExecutor.hpp | Command execution | **Dashboard** | Move to libdashboard |
| IMetricsPublisher.hpp | Metrics publishing | TBD | Analyze |
| IUIAdapter.hpp | UI adapter interface | **Dashboard** | Move to libdashboard |

---

### app/metrics/ (3 headers) - Metrics Data

| Header | Purpose | **Category** | **Action** |
|--------|---------|------------|-----------|
| IMetricsSubscriber.hpp | Metrics listener | **Dashboard** | Move to libdashboard |
| MetricsEvent.hpp | Metrics event data | TBD | Analyze |
| NodeMetricsSchema.hpp | Metrics schema | TBD | Analyze |

---

### app/policies/ (6 headers) - Execution Policies

| Header | Purpose | **Category** | **Action** |
|--------|---------|------------|-----------|
| CommandPolicy.hpp | Command processing | **Dashboard** | Move to libdashboard |
| CompletionPolicy.hpp | Graph completion | **Graph** | Move to libgraph |
| CSVInjectionPolicy.hpp | CSV data injection | **Graph** | Move to libgraph |
| DashboardPolicy.hpp | Dashboard-specific | **Dashboard** | Move to libdashboard |
| DataInjectionPolicy.hpp | Data injection | **Graph** | Move to libgraph |
| MetricsPolicy.hpp | Metrics collection | TBD | Analyze |

---

## Recommended Migration Plan

### Move to libgraph/include/graph/core/ (Pure utilities)
```
core/
├── ActiveQueue.hpp
├── CallbackUtilities.hpp
├── Expected.hpp
├── FormatUtilities.hpp
├── PluginReflection.hpp
├── RangesUtilities.hpp
├── ReflectionHelper.hpp
├── TypeInfo.hpp
└── VariantHelper.hpp

app/
├── Errors.hpp                    ← From app/Errors
└── JsonUtilities.hpp             ← From app/JsonUtilities
```

### Move to libgraph/include/graph/app/ (Graph-specific)
```
app/
├── GraphCapability.hpp           ← From app/capabilities/
├── CSVDataInjectionCapability.hpp ← From app/capabilities/
├── DataInjectionCapability.hpp   ← From app/capabilities/
├── MetricsCapability.hpp         ← Keep here (or move to dashboard)
├── CompletionPolicy.hpp          ← From app/policies/
├── CSVInjectionPolicy.hpp        ← From app/policies/
├── DataInjectionPolicy.hpp       ← From app/policies/
└── MetricsPolicy.hpp             ← Analyze further
```

### Move to libdashboard/include/dashboard/app/ (Dashboard-specific)
```
app/
├── capabilities/
│   ├── DashboardCapability.hpp
│   ├── CommandCapability.hpp
│   └── MetricsCapability.hpp     ← Or keep in graph/app/ if metrics is shared
│
├── adapters/
│   ├── CLIAdapter.hpp
│   ├── TerminalUIAdapter.hpp
│   └── WebUIAdapter.hpp
│
├── clients/
│   └── DashboardRESTClient.hpp
│
├── interfaces/
│   ├── DashboardCommand.hpp
│   ├── ICommandExecutor.hpp
│   ├── IMetricsPublisher.hpp
│   └── IUIAdapter.hpp
│
├── metrics/
│   ├── IMetricsSubscriber.hpp
│   ├── MetricsEvent.hpp
│   └── NodeMetricsSchema.hpp
│
└── policies/
    ├── CommandPolicy.hpp
    ├── DashboardPolicy.hpp
    └── MetricsPolicy.hpp         ← If shared, keep in graph/
```

---

## Dependencies to Verify (TBD Items)

Before finalizing, need to check includes in:

1. **FactoryManager.hpp** - Where does it belong?
   - If it creates graph nodes → libgraph
   - If it's dashboard-specific → libdashboard

2. **GraphBuilder.hpp** - Graph construction utility
   - Likely → libgraph/include/graph/app/

3. **JsonDeserialization.hpp** - JSON deserialization
   - Likely → libgraph/include/graph/core/ (utility)

4. **SignalHandler.hpp** - Signal handling
   - Likely → libdashboard (UI-specific)

5. **IMetricsPublisher.hpp** - Who publishes metrics?
   - If graph publishes → libgraph/include/graph/app/
   - If dashboard publishes → libdashboard

6. **MetricsEvent.hpp** - Metrics event data
   - Likely shared → libgraph/include/graph/app/

7. **NodeMetricsSchema.hpp** - Metrics schema definition
   - Likely → libgraph/include/graph/app/ (graph layer owns metrics definition)

8. **MetricsPolicy.hpp** - Metrics collection policy
   - Likely → libgraph/include/graph/app/ (execution policy)

---

## Phase A Summary

✅ **Completed**:
- [x] Audited all 30+ headers in core/ and app/
- [x] Identified dependencies for each header
- [x] Categorized by purpose (utility vs UI vs graph)
- [x] Created migration plan
- [x] Identified 8 headers needing deeper analysis

📋 **Next Steps (Phase A Continuation)**:
- [ ] Check includes in 8 TBD headers
- [ ] Finalize categorization
- [ ] Document any headers that depend on each other (within core/, within app/)
- [ ] Check if any graph layer .cpp files include app/ headers (would reveal upward dependencies)

**Estimated Time for Phase A Completion**: 30 minutes

---

## Key Insights

1. **Clear separation exists** - Most headers naturally fall into utility vs UI vs graph categories

2. **Errors.hpp + JsonUtilities.hpp are true utilities** - Should definitely move to core/
   - They depend only on standard library + core utilities
   - Used by both graph and dashboard

3. **Capabilities folder has mixed responsibility**:
   - Some are graph-level (GraphCapability, DataInjectionCapability)
   - Some are dashboard-level (DashboardCapability, CommandCapability)
   - Metrics is ambiguous - could go either way

4. **Policies should be split**:
   - Graph policies → libgraph (execution control)
   - Dashboard policies → libdashboard (UI control)

5. **No major circular dependencies found so far** ✓
   - All dependencies flow logically
   - core/ is truly standalone
   - Graph and dashboard are separable

---

**Status**: Ready to proceed to Phase A Step 2 (create directory structure)
