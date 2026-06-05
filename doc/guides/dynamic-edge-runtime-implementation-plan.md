# Dynamic Edge Runtime Implementation Plan

## Purpose

Define a concrete implementation plan for replacing JSON-only `EdgeRegistry` pre-registration with runtime descriptor-based edge construction, while preserving the existing typed `GraphManager::AddEdge<...>()` path for native C++ graphs.

## Goal

Make JSON graph configs self-contained for edge construction.

Target outcome:

1. JSON graph loading resolves edges from node and port descriptors at runtime.
2. No `EdgeRegistration::Register<SrcNode, SrcPort, DstNode, DstPort>()` step is required for JSON-driven graphs.
3. Native C++ graph construction continues to use templated `AddEdge<...>()` unchanged.

## Non-Goals

1. Do not remove the existing typed edge path in this effort.
2. Do not replace compile-time edge validation for native graphs.
3. Do not require code generation or runtime C++ compilation.
4. Do not redesign node execution or plugin loading beyond the port and edge surfaces needed for runtime connections.

## Current Constraint

The current architecture already supports runtime node discovery, but not runtime edge transport connection.

Existing state:

1. Nodes expose runtime metadata and descriptors through `NodeFacadeAdapter` and node descriptor APIs.
2. Ports expose runtime metadata through `IPortFunction`.
3. `GraphManager` already stores edges behind `IEdgeBase`, so it can hold a new `DynamicEdge` implementation.
4. Edge creation still depends on compile-time queue types through `IPortFunction::GetQueueIfType<T>()` and templated `GraphManager::AddEdge<...>()`.

Root blocker:

1. Ports do not yet expose an erased runtime connect operation.

## Proposed Architecture

### Dual Edge Model

GraphX should support two edge construction paths:

1. Native C++ graphs:
   - `GraphManager::AddEdge<SrcNode, SrcPort, DstNode, DstPort>(...)`
   - maximum compile-time type safety
2. JSON and plugin graphs:
   - `GraphManager::AddDynamicEdgeExpected(...)`
   - runtime descriptor validation
   - no pre-registration step

### Runtime Flow

```text
JSON edge
  -> resolve source node
  -> resolve source output port
  -> resolve destination node
  -> resolve destination input port
  -> validate direction
  -> validate payload and transport compatibility
  -> create DynamicEdge
  -> store as IEdgeBase in GraphManager
```

## Required Design Changes

### First Implementation Slice

The first implementation slice should avoid widening `INode`.

Reason:

1. `GraphBuilder::Build()` already preserves `std::vector<std::shared_ptr<NodeFacadeAdapter>> nodes_` before wrapping nodes for `GraphManager` registration.
2. `GraphBuilder::WireEdges(...)` currently resolves JSON edges from that same build-local node set and only falls back to `EdgeRegistry` for the final typed connection step.
3. `NodeFacadeAdapter` already exposes stable runtime port metadata through `GetInputPortMetadata()`, `GetOutputPortMetadata()`, and `GetDescriptor()`.

Implication:

1. Phase 1 runtime port lookup can be implemented on `NodeFacadeAdapter` first.
2. `INode` widening can be postponed until the runtime edge path is proven.
3. This reduces churn across native node implementations and keeps the first migration local to the JSON graph path.

### 1. Introduce Runtime Port Descriptor and Handle

Add a stable runtime-facing port model.

Suggested types:

```cpp
struct RuntimePortDescriptor {
    std::size_t id;
    std::string name;
    PortDirection direction;
    std::string payload_type;
    std::string transport_type;
};

struct RuntimePortHandle {
    std::size_t node_index;
    RuntimePortDescriptor descriptor;
    IPortFunction* port;
};
```

Notes:

1. Use owned `std::string` fields instead of `std::string_view` in the public handle to avoid lifetime hazards.
2. `node_index` should be stored for `GraphManager` metadata and error reporting.
3. `IPortFunction*` is non-owning and valid only while the containing node remains alive.

### 2. Add Runtime Port Lookup APIs

Add runtime lookup by id or name on the node-facing abstraction used by graph construction.

Preferred surface:

1. Phase 1: expose on `NodeFacadeAdapter`.
2. Phase 2 or later: evaluate promoting the API to `NodeFacadeAdapterWrapper` or `INode` if native graphs also need the runtime path.

Suggested APIs:

```cpp
std::expected<RuntimePortHandle, GraphError>
GetOutputPortHandle(std::string_view name_or_id, std::size_t node_index);

std::expected<RuntimePortHandle, GraphError>
GetInputPortHandle(std::string_view name_or_id, std::size_t node_index);
```

Resolution rules:

1. If the token parses as an integer, resolve as port id first.
2. Otherwise resolve as port name.
3. Ambiguous or missing matches should return structured errors.
4. The returned handle should be built from adapter metadata only in phase 1; no runtime queue binding is required yet.

Phase 1 implementation notes:

1. `NodeFacadeAdapter::GetInputPortMetadata()` and `GetOutputPortMetadata()` are already sufficient to produce `RuntimePortDescriptor` values.
2. If `IPortFunction*` is not available yet at the adapter layer, phase 1 may return a partially populated handle with `port == nullptr` and treat it as a lookup-only object.
3. The actual transport binding can be completed in phase 2 when erased `ConnectTo(...)` is added.

### 3. Add Erased Runtime Connection to `IPortFunction`

This is the critical change.

Current limitation:

1. `GetQueueIfType<T>()` still requires compile-time `T`.

Minimum viable addition:

```cpp
class IPortFunction {
public:
    virtual std::string_view GetTransportTypeName() const = 0;

    virtual std::expected<void, GraphError>
    ConnectTo(IPortFunction& destination, std::size_t capacity) = 0;
};
```

Implementation model:

1. `PortFunction<P>` implements the erased entrypoint.
2. `ConnectTo(...)` checks runtime payload compatibility.
3. `ConnectTo(...)` creates or binds the underlying queue/transport using the concrete `P::type` internally.

This preserves compile-time transport instantiation inside the port implementation while moving dispatch to the port boundary.

### 4. Add Runtime Compatibility Validation

Introduce a reusable compatibility validator before edge creation.

Suggested validation rules:

1. Source port must be `PortDirection::Output`.
2. Destination port must be `PortDirection::Input`.
3. Payload types must match exactly.
4. Transport types must match exactly.
5. Buffer size must be within supported bounds.

Suggested API:

```cpp
std::expected<void, GraphError>
ValidateDynamicEdgeCompatibility(
    const RuntimePortHandle& source,
    const RuntimePortHandle& destination,
    std::size_t capacity);
```

This validator should produce JSON-facing configuration errors that are better than today's `NoCreatorRegistered` failures.

### 5. Implement `DynamicEdge`

Add a new edge type that implements `IEdgeBase` and owns the runtime connection.

Suggested shape:

```cpp
class DynamicEdge final : public IEdgeBase {
public:
    DynamicEdge(RuntimePortHandle source,
                RuntimePortHandle destination,
                std::size_t capacity);

    bool Init() override;
    bool Start() override;
    void Stop() override;
    void Join() override;
    bool JoinWithTimeout(std::chrono::milliseconds timeout_ms) override;

private:
    RuntimePortHandle source_;
    RuntimePortHandle destination_;
    std::size_t capacity_;
};
```

Expected behavior:

1. `Init()` validates and binds the runtime ports.
2. `Start()` delegates to the underlying transport if needed.
3. Queue and thread metrics should be exposed through `IEdgeBase` in the same shape as typed edges where practical.

### 6. Add `GraphManager::AddDynamicEdgeExpected(...)`

Add a second edge creation entrypoint alongside the typed template path.

Suggested API:

```cpp
struct DynamicEdgeConfig {
    std::size_t source_node_index;
    std::size_t source_port_id;
    std::size_t dest_node_index;
    std::size_t dest_port_id;
    std::size_t capacity;
};

std::expected<IEdgeBase&, GraphError>
AddDynamicEdgeExpected(const DynamicEdgeConfig& config);
```

Requirements:

1. Store `DynamicEdge` in the existing `edges_` container as `std::unique_ptr<IEdgeBase>`.
2. Populate edge metadata for visualization and metrics output.
3. Keep lifecycle and shutdown semantics aligned with existing edges.

### 7. Route JSON Graph Construction Through Dynamic Edges

Update `GraphBuilder` and the JSON loading path so JSON edges no longer use `EdgeRegistry::CreateEdgeExpected(...)`.

New path:

1. Build nodes as today.
2. Resolve source and destination node wrappers.
3. Resolve runtime ports by id or name.
4. Validate compatibility.
5. Call `GraphManager::AddDynamicEdgeExpected(...)`.

Preserve existing typed path for:

1. Fluent/static graphs.
2. Existing test helpers that intentionally exercise typed edges.

## Suggested Implementation Phases

## Current Progress (2026-06-05)

1. Phase 1 has started with a lookup-only runtime port model.
2. `RuntimePortDescriptor`, `RuntimePortHandle`, and `RuntimePortLookupError` now exist in `libgraph/include/graph/RuntimePort.hpp`.
3. `NodeFacadeAdapter` now supports `GetInputPortHandle(...)` and `GetOutputPortHandle(...)` using existing adapter metadata.
4. Phase 1 currently returns lookup-only handles with `port == nullptr`; erased runtime transport binding remains future work for phase 2.
5. Focused validation passed:
    - `cmake --build build -j4`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortLookupTest.*'`
6. Phase 2 has started with an erased compatibility-and-capacity binding seam on `IPortFunction`.
7. `IPortFunction` now exposes `GetTransportTypeName()` and `ConnectTo(...)`.
8. `PortFunction<P>` now implements runtime direction validation, payload compatibility validation, and shared capacity binding for matching output/input pairs.
9. Focused phase 2 validation passed:
    - `cmake --build build -j4`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortConnectionTest.*'`
10. Phase 3 skeleton is now in place.
11. `ValidateDynamicEdgeCompatibility(...)` now validates runtime handles before edge creation.
12. `DynamicEdge` now exists as an `IEdgeBase` implementation with lifecycle and metadata hooks.
13. `GraphManager::AddDynamicEdgeExpected(...)` now stores a dynamic edge in the existing edge container and records edge metadata.
14. Focused phase 3 validation passed:
    - `cmake --build build -j4`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.*'`
15. Phase 4 migration has started and the JSON edge wiring path now calls `GraphManager::AddDynamicEdgeExpected(...)` from `GraphBuilder::WireEdges(...)`.
16. `GraphBuilder` no longer calls `EdgeRegistry::CreateEdgeExpected(...)` when wiring JSON-loaded edge configs.
17. Runtime port lookup now returns non-null `IPortFunction*` bindings backed by owned descriptor adapters.
18. `DynamicEdge::Init()` now exercises runtime `ConnectTo(...)` for JSON-wired edges when handles are resolved.
19. Runtime lookup now prefers queue-backed runtime port bindings created by plugin ABI callbacks and falls back to descriptor adapters when unavailable.
20. `NodeFacade` now exposes runtime port create/destroy callbacks for input/output ports, and `NodeFacadeAdapter` consumes them during handle lookup.
21. `IPortFunction` now supports runtime `TransferTo(...)`, and `DynamicEdge` now runs a transfer loop that moves payloads from source output queues to destination input queues.
22. Dynamic-edge runtime metrics now track transfer activity and backpressure/rejection events during runtime pumping.
23. `GraphManager::GetMetrics()` now rolls up dynamic-edge counters into graph aggregates (`graph_total_enqueued`, `graph_total_dequeued`, `total_queue_time_ns`, `backpressure_events`, and processed totals).
24. Graph lifecycle timing metrics (`init_time_ns`, `start_time_ns`, `execution_time_ns`) are now populated in `GraphManager` for dynamic-edge runs; `GetMetrics()` updates live execution time while running.
25. Graph aggregate thread/process timing now includes per-edge thread metrics (`total_transfer_time_ns`, idle/wait time, and live `peak_active_threads`) via `GetMetrics()`.
26. Dynamic-edge executable correctness has been tightened:
    - descriptor-only fallback ports (`runtime.descriptor`) are now rejected during `DynamicEdge::Init()` for executable graphs
    - `DynamicEdge::JoinWithTimeout(...)` now enforces timeout semantics instead of always blocking
    - `DynamicEdge::GetQueueSize()` now reports destination runtime queue depth
    - transient `TransferTo(...)` failures are treated as retryable backpressure unless the error is structurally fatal
27. Focused migration validation passed:
    - `cmake --build build -j4`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortLookupTest.*:RuntimePortConnectionTest.*:DynamicEdgeTest.*:JsonDynamicGraphLoaderExpectedTest.*:GraphExecutorBuilderPoliciesTest.*:GraphExecutorPolicyFailuresTest.*:GraphExecutorLifecycleTest.*:*TopologyNodesUseInstanceNamesNotTypes*'`
    - `./libgraph/test/test_libgraph_integration --gtest_filter='*GraphTopology_ProducerToSinks*'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortLookupTest.*:RuntimePortConnectionTest.*:DynamicEdgeTest.*'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortLookupTest.*:RuntimePortConnectionTest.*:DynamicEdgeTest.*:GraphExecutorLifecycleTest.ConstructAndDestructWithEveryTopology:GraphExecutorLifecycleTest.InitStopJoinWithEveryTopology'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='RuntimePortConnectionTest.TransferToMovesPayloadFromOutputToInput:DynamicEdgeTest.DynamicEdgeTransfersPayloadAtRuntime'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.GraphManagerDynamicEdgeMetricsTrackRuntimeTransfer'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.GraphManagerMetricsAggregateDynamicEdgeCounters'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.GraphManagerLifecycleTimingMetricsTrackRuntimeExecution'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.GraphManagerAggregatesDynamicEdgeThreadTimingMetrics'`
    - `./libgraph/test/test_libgraph_unit --gtest_filter='DynamicEdgeTest.DynamicEdgeInitRejectsDescriptorOnlyFallbackPorts:DynamicEdgeTest.DynamicEdgeJoinWithTimeoutRespectsDeadline:DynamicEdgeTest.DynamicEdgeQueueSizeReflectsDestinationDepth:DynamicEdgeTest.DynamicEdgeTransientTransferFailureDoesNotStopEdge'`

### Phase 0: Guardrails and Baseline

1. Add characterization tests around current JSON edge failure modes.
2. Add tests for descriptor lookup by port name and id.
3. Add snapshot coverage for `DisplayGraphJSON()` and `DisplayGraphDOT()` on existing graphs so metadata regressions are caught.

Exit criteria:

1. Current typed path remains green.
2. JSON path has tests that clearly define today's failure points.

### Phase 1: Runtime Port Model

1. Add `RuntimePortDescriptor` and `RuntimePortHandle`.
2. Add runtime port lookup on `NodeFacadeAdapter`.
3. Add tests for successful and failed lookup.

Exit criteria:

1. JSON builder can resolve source and destination ports without creating edges yet.

Detailed tasks:

1. Add `RuntimePortDescriptor` and `RuntimePortHandle` in a new `RuntimePort.hpp`.
2. Add `NodeFacadeAdapter::GetInputPortHandle(...)` and `GetOutputPortHandle(...)` declarations.
3. Implement lookup by numeric id using `PortMetadataC::index`.
4. Implement lookup by name using `PortMetadataC::port_name`.
5. Add explicit error mapping for `PortNotFound`, `PortDirectionMismatch`, and malformed port token cases.
6. Add unit tests covering:
    - lookup by id success
    - lookup by name success
    - missing port by id
    - missing port by name
    - wrong direction lookup

Suggested first target files:

1. `libgraph/include/graph/RuntimePort.hpp`
2. `libgraph/include/graph/NodeFacade.hpp`
3. `libgraph/src/graph/NodeFacade.cpp`
4. `libgraph/test/unit/test_runtime_port_lookup.cpp`

### Phase 2: Erased Port Connection

1. Add `GetTransportTypeName()` and `ConnectTo(...)` to `IPortFunction`.
2. Implement the new API in `PortFunction<P>`.
3. Add transport/payload compatibility tests.

Exit criteria:

1. Two runtime port handles can be validated and connected without `EdgeRegistry`.

Current state:

1. The validation-and-binding seam is implemented.
2. The current implementation binds capacity and validates output->input direction plus payload equality.
3. Runtime queue ownership or dynamic transport transfer is not implemented yet.
4. This is sufficient to support the next step: a reusable compatibility validator and a first `DynamicEdge` skeleton.

### Phase 3: DynamicEdge and GraphManager Integration

1. Implement `DynamicEdge`.
2. Add `GraphManager::AddDynamicEdgeExpected(...)`.
3. Ensure `IEdgeBase` metrics and metadata surfaces work for dynamic edges.

Exit criteria:

1. A hand-built runtime graph can add a dynamic edge and execute successfully.

Current state:

1. The compatibility validator is implemented.
2. `DynamicEdge` exists and satisfies the current `IEdgeBase` contract.
3. `GraphManager::AddDynamicEdgeExpected(...)` stores dynamic edges and records graph metadata.
4. The current `DynamicEdge` is still a skeleton: it validates and binds at init time, but it does not yet perform runtime data transfer.
5. The next step is to route `GraphBuilder::WireEdges(...)` through the new path once runtime port handles include actual `IPortFunction*` bindings.

### Phase 4: JSON Builder Migration

1. Update `GraphBuilder::WireEdges(...)` for JSON graphs to use dynamic edges.
2. Remove `EdgeRegistry` dependency from the JSON-only path.
3. Improve config error messages for invalid direction, payload mismatch, transport mismatch, and missing ports.

Exit criteria:

1. JSON graphs build without prior edge registration.

Current state:

1. `GraphBuilder::WireEdges(...)` now resolves runtime port handles and routes JSON edges through `GraphManager::AddDynamicEdgeExpected(...)`.
2. Runtime handles now bind queue-backed `IPortFunction` adapters via plugin callbacks (with descriptor fallback), so `DynamicEdge::Init()` performs runtime `ConnectTo(...)` on resolved ports.
3. Runtime transfer semantics are now in place through `IPortFunction::TransferTo(...)` and the `DynamicEdge` transfer thread.
4. Dynamic-edge metrics/backpressure parity has started: transfer activity, rejection, and peak depth are now tracked at runtime.
5. Dynamic-edge queue-time and graph aggregate rollups are now wired through `EdgeMetrics` and `GraphManager::GetMetrics()`.
6. Graph lifecycle timing parity is now wired for runtime dynamic-edge flows (`Init`, `Start`, run window via `Stop`).
7. Graph-level thread/process-time rollups now include dynamic-edge transfer+idle/wait contributions and active thread peaks.
8. Remaining phase 4 work is hardening transport ownership/lifecycle semantics and validating these rollups against broader mixed typed-edge + dynamic-edge scenarios.
9. JSON edge schemas are still effectively index-oriented in the current loader path; named-port-first JSON ergonomics remain future work.

### Phase 5: Cleanup and Documentation

1. Update JSON graph documentation and examples.
2. Document the split between typed edges and dynamic edges.
3. Re-scope `EdgeRegistry` docs so it is clearly a typed/native graph facility.

Exit criteria:

1. User-facing docs no longer imply that JSON edges must be pre-registered.

## File-Level Impact

Primary implementation surfaces:

1. `libgraph/include/graph/IPortFunction.hpp`
2. `libgraph/include/graph/PortFunction.hpp`
3. `libgraph/include/graph/INode.hpp` or `libgraph/include/graph/NodeFacadeAdapterWrapper.hpp`
4. `libgraph/include/graph/NodeFacade.hpp`
5. `libgraph/include/graph/EdgeFacade.hpp`
6. `libgraph/include/graph/GraphManager.hpp`
7. `libgraph/src/graph/GraphBuilder.cpp`
8. `libgraph/src/graph/JsonDynamicGraphLoader.cpp`

Likely new files:

1. `libgraph/include/graph/RuntimePort.hpp`
2. `libgraph/include/graph/DynamicEdge.hpp`
3. `libgraph/src/graph/DynamicEdge.cpp`
4. `libgraph/src/graph/RuntimePortValidation.cpp`

Primary tests:

1. `libgraph/test/unit/test_dynamic_edge.cpp`
2. `libgraph/test/unit/test_runtime_port_lookup.cpp`
3. `libgraph/test/unit/test_graph_builder_json_dynamic_edges.cpp`
4. updates to `libgraph/test/unit/test_json_dynamic_graph_loader.cpp`

## Recommended Error Model

Prefer structured errors over boolean failure.

Suggested error categories:

1. `NodeNotFound`
2. `PortNotFound`
3. `PortDirectionMismatch`
4. `PayloadTypeMismatch`
5. `TransportTypeMismatch`
6. `CapacityInvalid`
7. `RuntimeConnectFailed`

These should bubble up to JSON graph build failures with node and port names included.

## Validation Strategy

### Unit Gates

1. Runtime port lookup by id and by name.
2. Direction mismatch rejection.
3. Payload mismatch rejection.
4. Transport mismatch rejection.
5. Dynamic edge lifecycle parity with typed edges.

### Integration Gates

1. Build a JSON graph without calling any edge registration helper.
2. Execute classic JSON topologies through the normal lifecycle.
3. Verify graph display/metrics still report edges correctly.

### Regression Gates

1. Existing typed graph tests must remain green.
2. `EdgeRegistry` unit tests must remain green.
3. Plugin-backed JSON loading must remain green.

## Risks

1. Runtime descriptor compatibility may be weaker than current compile-time guarantees if payload and transport identities are underspecified.
2. Queue and transport semantics may diverge across backends unless transport compatibility is modeled explicitly.
3. Widening `INode` can create churn across many node implementations if done too early.
4. Edge metrics parity may be incomplete if `DynamicEdge` does not reuse the same queue instrumentation model as typed edges.

## Recommended Execution Order

1. Implement runtime port descriptors and lookup on `NodeFacadeAdapter` first.
2. Add erased `ConnectTo(...)` on `IPortFunction` second.
3. Add `DynamicEdge` and `GraphManager::AddDynamicEdgeExpected(...)` third.
4. Switch JSON `WireEdges(...)` to dynamic edge creation fourth.
5. Leave `EdgeRegistry` and typed `AddEdge<...>()` unchanged for native graphs.

## Definition of Done

1. JSON graphs no longer require edge pre-registration.
2. A malformed JSON edge reports a descriptor-based validation error, not `NoCreatorRegistered`.
3. Native typed graph construction remains unchanged and fully passing.
4. Dynamic edges appear in graph displays and metrics output.
5. Documentation clearly distinguishes native typed graphs from runtime JSON graphs.

## Open Design Decisions

1. Should runtime port lookup be added to `INode`, `NodeFacadeAdapterWrapper`, or a new adapter interface?
2. Should payload and transport compatibility be strict string equality or a stronger canonical type identity?
3. Should `DynamicEdge` own a dedicated runtime queue abstraction, or should `IPortFunction::ConnectTo(...)` hide queue ownership internally?
4. Should the JSON format prefer port names, port ids, or allow both with names as the primary surface?
