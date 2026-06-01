# CUDA and SYCL Graph Nodes and Thin Data Movement Implementation Plan

## Objective

Add high-performance GPU computation and data movement to GraphX using thin abstractions that preserve existing libgraph node and capability patterns and avoid heavyweight framework layers.

Primary backend language in this document is CUDA, with a SYCL variant that mirrors the same core structure for non-NVIDIA hardware.

## Scope

In scope:

1. CUDA capability interfaces and default implementations
2. SYCL capability interfaces and default implementations
3. Thin buffer and token payload model for host and device movement
4. A minimal, high-throughput CUDA node set (ingress, transfer, compute, egress, sync)
5. A matching SYCL node set with equivalent contracts and lifecycle behavior
6. Metrics and diagnostics integration with existing node metrics model
7. Single-process multi-GPU execution support (sharding, peer movement, collectives)
8. Unit, integration, and performance test coverage and CI entry points

Out of scope (initial phase):

1. Full deep-learning runtime integration (TensorRT and ONNX runtime wrappers)
2. Multi-node distributed orchestration (cross-host scheduling and transport)
3. Automatic graph-level kernel fusion planner

## Design Principles

1. Thin layers only: keep runtime calls visible and explicit in capability backends and nodes
2. Tokenized movement: pass small metadata views on edges, never copy large buffers in messages
3. Explicit synchronization: execution state in payloads, no implicit device-wide sync
4. Pool-first memory strategy: pinned host pools, device pools, and reuse contracts
5. Backend parity: CUDA and SYCL keep equivalent graph-facing contracts
6. Incremental adoption: no breakage to existing CPU-only nodes and pipelines

## Existing GraphX Hooks Used

1. Capability registration and lookup: libgraph/include/graph/CapabilityBus.hpp
2. Node lifecycle and typed ports: libgraph/include/graph/Nodes.hpp
3. Optional capability defaults and discovery:
   - libgraph/include/graph/DefaultCapabilityBus.hpp
   - libgraph/include/graph/CapabilityDiscovery.hpp
4. Message envelope tuned for small payloads: libgraph/include/graph/Message.hpp

## Proposed Artifacts

### 1. CUDA Capability Interfaces

Create interfaces under libgraph/include/cuda/capabilities/:

1. ICudaContextCapability
   - device selection
   - stream and event create and destroy
2. ICudaMemoryPoolCapability
   - device allocate and release (async-capable)
   - pinned host allocate and release
3. ICudaTransferCapability
   - H2D, D2H, D2D async copy
4. ICudaKernelCapability
   - kernel registration and launch API
5. ICudaTelemetryCapability
   - timing and event measurements and counters
6. ICudaCollectiveCapability
   - all-reduce, all-gather, reduce-scatter for single-process multi-GPU

### 1b. SYCL Capability Interfaces

Create interfaces under libgraph/include/sycl/capabilities/:

1. ISyclContextCapability
   - platform and device selection
   - queue and event lifecycle
2. ISyclMemoryPoolCapability
   - USM device, shared, and host allocation and release
3. ISyclTransferCapability
   - host-device, device-host, and device-device async copy
4. ISyclKernelCapability
   - kernel registration and launch API
5. ISyclTelemetryCapability
   - event timing and queue-level counters
6. ISyclCollectiveCapability
   - backend-appropriate collectives for single-process multi-GPU

### 2. Thin Payload Types (Shared Contract)

Create backend-neutral types under libgraph/include/accel/types/ (or mirrored per backend with identical fields):

1. DeviceBufferView
   - device_ptr, bytes, dtype, shape and stride, device_id, execution_queue_id, ready_event
2. HostPinnedBufferView
   - host_ptr, bytes, dtype, shape and stride, allocator_id
3. BufferLease
   - pool_id, allocation_id, view, release policy
4. TransferTicket
   - src and dst view metadata, execution_queue_id, completion_event
5. KernelTicket
   - kernel_id, launch config, args metadata, execution_queue_id
6. DeviceShardDescriptor
   - global tensor metadata, shard index/count, owning device_id, offset and length
7. CollectiveTicket
   - collective kind, group_id, rank, world_size, completion_event

### 3. Initial CUDA Node Set

Create nodes under libgraph/include/cuda/nodes/ and libgraph/src/cuda/nodes/:

1. HostIngressPinnedSourceNode
   - Source node that emits HostPinnedBufferView
2. H2DAsyncNode
   - Interior node: HostPinnedBufferView -> DeviceBufferView
3. DeviceTransformNode
   - Interior node: DeviceBufferView -> DeviceBufferView (single kernel launch)
4. DeviceReduceNode
   - Interior node: DeviceBufferView -> DeviceBufferView (aggregation)
5. D2HAsyncNode
   - Interior node: DeviceBufferView -> HostPinnedBufferView
6. HostEgressSinkNode
   - Sink node for host consumption
7. StreamSyncNode
   - Control node for explicit event dependency edges
8. LeaseReleaseNode
   - Sink or control node for deterministic pool release and backpressure control
9. DeviceShardNode
   - Interior node: DeviceBufferView -> DeviceBufferView shards across GPUs
10. PeerCopyNode
   - Interior node: DeviceBufferView -> DeviceBufferView on peer GPU (P2P when available)
11. CollectiveReduceNode
   - Interior node: multi-GPU collective reduction with CollectiveTicket emission

### 3b. Initial SYCL Node Set (Structural Match)

Create nodes under libgraph/include/sycl/nodes/ and libgraph/src/sycl/nodes/ with equivalent semantics:

1. HostIngressPinnedSourceNodeSycl
2. H2DAsyncNodeSycl
3. DeviceTransformNodeSycl
4. DeviceReduceNodeSycl
5. D2HAsyncNodeSycl
6. HostEgressSinkNodeSycl
7. QueueSyncNodeSycl
8. LeaseReleaseNodeSycl
9. DeviceShardNodeSycl
10. PeerCopyNodeSycl
11. CollectiveReduceNodeSycl

Naming can be unified later by selecting backend via capability injection to avoid duplicated node names.

## SYCL Variant and Structural Parity

Yes, the SYCL variant should match the fundamental CUDA structure at the graph contract level.

Parity mapping:

1. cudaStream_t -> sycl::queue
2. cudaEvent_t -> sycl::event
3. cudaMemcpyAsync -> queue.memcpy with event dependency chaining
4. cudaMallocAsync or pool API -> USM allocators with pooling strategy
5. Kernel launch triple (grid, block, shared memory) -> nd_range and local range configuration

Expected differences (kept behind thin backend capability layers):

1. Queue ordering and dependency nuances between runtimes
2. USM capability variance across implementations and devices
3. Profiling and timing semantic differences across SYCL runtimes
4. Peer-to-peer capability and collective backend differences by vendor stack

## Multi-GPU Support Model (Single Process)

Execution model:

1. One process controls N devices
2. Each device has one or more execution queues or streams
3. Shards are explicit payloads via DeviceShardDescriptor
4. Cross-device movement uses PeerCopyNode (P2P preferred, staged fallback)
5. Cross-device aggregation uses CollectiveReduceNode

Scheduling and ownership rules:

1. Device ownership is explicit in payload tokens
2. Queue or stream affinity remains explicit at node boundaries
3. No implicit cross-device migration
4. Backpressure is enforced per-device pool and globally at shard fan-in points

## Implementation Phases

## Phase 0: Foundation and Build Gating

Deliverables:

1. CMake option ENABLE_CUDA_GRAPH_NODES (default OFF)
2. CMake option ENABLE_SYCL_GRAPH_NODES (default OFF)
3. CUDA toolkit detection and compile gating
4. SYCL compiler and runtime detection and compile gating
5. CPU-only build unaffected

Acceptance criteria:

1. Default build and test path unchanged
2. Enabling CUDA or SYCL option compiles interface headers and stubs

## Phase 1: Capability Contracts and Basic Backends

Deliverables:

1. Capability interfaces for context, memory, transfer, kernel, telemetry
2. Default CUDA backend implementations using CUDA runtime APIs
3. Default SYCL backend implementations using SYCL APIs
4. Registration in capability bus bootstrap path

Acceptance criteria:

1. Capability lookup succeeds and fails predictably with clear diagnostics
2. Basic stream or queue, event, and allocation smoke tests pass for enabled backends

## Phase 2: Payload and Token Model

Deliverables:

1. DeviceBufferView, HostPinnedBufferView, BufferLease, TransferTicket, KernelTicket
2. Validation helpers (is valid, shape bytes sanity)
3. Backend tag and execution dependency handle support (stream or queue and event)
4. Lightweight serialization and log formatting for diagnostics

Acceptance criteria:

1. No dynamic payload copies in hot path message transport
2. Token structs remain trivially movable where possible

## Phase 3: Data Movement Nodes

Deliverables:

1. HostIngressPinnedSourceNode (CUDA and SYCL variants)
2. H2DAsyncNode (CUDA and SYCL variants)
3. D2HAsyncNode (CUDA and SYCL variants)
4. LeaseReleaseNode (shared behavior)
5. PeerCopyNode for cross-device movement (CUDA and SYCL variants)

Acceptance criteria:

1. Sustained async copy without global device sync in steady-state path
2. Correct event sequencing across at least 2 queues or streams
3. No leaks in pool accounting under repeated runs
4. Correct peer transfer fallback behavior when direct P2P is unavailable

## Phase 4: Compute Nodes

Deliverables:

1. DeviceTransformNode for unary kernel execution (CUDA and SYCL variants)
2. DeviceReduceNode for reduction path (CUDA and SYCL variants)
3. StreamSyncNode or QueueSyncNode for explicit event dependencies
4. DeviceShardNode for shard fan-out and shard metadata propagation

Acceptance criteria:

1. Correct compute output against CPU reference on representative workloads
2. Throughput scales with batch size and queue or stream count as expected
3. Shard fan-out and fan-in correctness validated across at least 2 GPUs

## Phase 5: Integration and Metrics

Deliverables:

1. End-to-end sample topology:
   - HostIngressPinnedSourceNode -> H2DAsyncNode -> DeviceTransformNode -> DeviceReduceNode -> D2HAsyncNode -> HostEgressSinkNode
2. Metrics integration with existing queue and thread metrics plus backend telemetry (CUDA or SYCL)
3. Diagnostic dumps for queue or stream, event, and pool usage
4. Multi-GPU sample topology with shard fan-out and collective fan-in

Acceptance criteria:

1. End-to-end topology deterministic and stable over long-run test
2. Metrics expose copy latency, kernel latency, queue depths, lease pressure
3. Metrics expose per-device utilization and cross-device transfer overhead

## Phase 5b: Multi-GPU Collectives

Deliverables:

1. CollectiveReduceNode (CUDA and SYCL variants)
2. Collective capability implementations and backend selection
3. Multi-GPU topology example:
   - DeviceShardNode -> DeviceTransformNode (per GPU) -> CollectiveReduceNode -> D2HAsyncNode

Acceptance criteria:

1. Collective results match CPU reference within tolerance
2. No deadlocks under repeated collective runs
3. End-to-end throughput improvement over single-GPU baseline for parallelizable workload

## Phase 6: Hardening and CI

Deliverables:

1. Unit tests for capabilities, payload validity, node behavior
2. Integration tests for graph topologies and failure handling
3. Optional performance baselines and regression thresholds
4. CI matrix extension for backend-specific runners when available
5. Optional multi-GPU test lane when suitable runners are available

Acceptance criteria:

1. New CUDA tests pass with ENABLE_CUDA_GRAPH_NODES=ON
2. New SYCL tests pass with ENABLE_SYCL_GRAPH_NODES=ON
3. Existing tests pass with both backend options OFF
4. Multi-GPU tests pass when MULTI_GPU_TESTS=ON and compatible hardware is available

## Test Strategy

### Unit tests

1. Capability contracts and error paths
2. Pool lease lifecycle correctness
3. Stream and queue event ordering and timeout behavior
4. Payload invariants and shape and byte checks

### Integration tests

1. H2D -> compute -> D2H correctness against CPU reference
2. Multi-stream or multi-queue overlap behavior
3. Backpressure behavior under constrained pool sizes
4. Recovery from kernel and copy errors without deadlock
5. Two-GPU shard and merge correctness
6. Collective correctness and timeout handling

### Performance tests

1. H2D bandwidth with pinned memory
2. Kernel throughput for representative kernels
3. End-to-end latency distribution (p50, p95, p99)
4. Pool churn and reuse efficiency
5. Backend parity checks to compare CUDA and SYCL throughput and latency trends on equivalent workloads
6. Multi-GPU scaling efficiency (1 GPU vs 2 GPU vs 4 GPU where available)

## Data Movement Abstraction (Thin Contract)

Contract rules:

1. Graph edges carry only token structs (views, leases, tickets)
2. Ownership is explicit via BufferLease and release node or RAII policy
3. Synchronization state is explicit via queue or stream and event handles
4. No hidden memcpy, no hidden synchronization in helper wrappers
5. Backend-specific primitives are confined to capability backends, not graph-facing payload contracts
6. Multi-GPU routing decisions are explicit in shard and collective tickets, not hidden in node internals

## Comparison with Professional GPU Frameworks

This architecture intentionally differs from large production GPU stacks in scope and layering.

Where this plan is similar:

1. Explicit async execution model (queues or streams plus events)
2. Memory pool and tokenized buffer ownership model
3. Separation of graph-facing contracts from backend runtime APIs
4. Multi-GPU sharding and collective primitives as first-class building blocks

Where this plan is different:

1. Lighter orchestration layer
   - Professional frameworks often include full runtime schedulers, admission control, autoscaling, and placement engines.
   - This plan keeps orchestration inside GraphX nodes and capabilities with minimal policy.
2. Less automatic optimization
   - Professional frameworks commonly provide graph rewrites, kernel fusion planners, runtime autotuning, and operator libraries.
   - This plan keeps optimization explicit and node-driven, with fewer hidden transforms.
3. Narrower distributed scope
   - Professional frameworks support robust multi-node transport and fault domains by default.
   - This plan currently targets single-process multi-GPU and leaves multi-node orchestration out of scope.
4. Smaller operational surface
   - Professional platforms include deep observability stacks, deployment controls, and mature debugging ecosystems.
   - This plan integrates with existing GraphX metrics and adds backend telemetry without introducing a platform control plane.

Trade-off summary:

1. Advantage: lower integration cost, simpler debugging, better transparency of data movement and sync semantics
2. Cost: fewer built-in optimizations and less out-of-the-box distributed operations tooling

## Orchestration Glossary

1. Runtime scheduler
   - The policy component that decides when ready work runs, what runs first, and how work is overlapped across devices and execution queues.
2. Admission control
   - The gate that decides whether new work is accepted now, delayed, or rejected based on pressure signals (queue depth, memory, latency budget).
3. Autoscaling
   - Dynamic capacity adjustment based on load (for this plan, typically activating more local devices, streams, or replicas; multi-node scaling is out of scope).
4. Placement engine
   - The policy component that maps work to concrete devices and queues, including shard placement and movement strategy.

## Scheduling Model in GraphX

A GraphX graph can count as a schedulable work item, and this should be the default model for the initial phase.

Recommended scheduling hierarchy:

1. Outer scheduler unit: Graph run request
   - The runtime schedules graph instances based on admission and placement policies.
2. Inner execution unit: Node readiness within the graph
   - Existing graph executor semantics drive node-level dependency execution.
3. Device execution unit: Kernel launches inside GPU nodes
   - GPU nodes issue one or more backend kernel submissions on assigned queue or stream.

Do we need a dedicated kernel-manager node?

1. Not required for initial delivery.
2. Kernel launch orchestration should remain inside DeviceTransformNode and DeviceReduceNode implementations plus backend capabilities.
3. Introduce a dedicated kernel-manager node only if one of these appears:
   - multiple kernels must be jointly scheduled across many graph branches with shared admission policy,
   - dynamic kernel batching or fusion decisions are needed at runtime,
   - kernel-level fairness and prioritization must be managed independently of node-level scheduling.

Initial decision for this plan:

1. Schedule at graph level.
2. Keep kernel scheduling local to GPU nodes and capabilities.
3. Re-evaluate need for kernel-manager node after Phase 5 performance and contention data.

## Professional Framework Integration Architecture

This section defines how GraphX can operate as a GPU-aware execution substrate under a professional framework control plane.

### Integration intent

1. Professional framework controls lifecycle, policy, and multi-tenant scheduling.
2. GraphX executes graph runs as deterministic GPU-aware work units.
3. GraphX retains thin data movement and explicit synchronization semantics.

### Scheduling boundary

1. Schedulable unit exposed to the framework: GraphRunRequest.
2. GraphRunRequest encapsulates:
   - graph_id and version,
   - input descriptors and SLO hints,
   - resource hints (device count, memory budget, priority class),
   - backend preference (CUDA or SYCL),
   - optional placement constraints.
3. GraphX internal scheduler executes node-ready work and delegates device work to backend capabilities.

### Control-plane and data-plane split

Control plane (professional framework):

1. Global admission control and tenant policy.
2. Placement policy for which host or process receives GraphRunRequest.
3. Capacity management and autoscaling decisions.
4. SLO policy, retries, and failover orchestration.

Data plane (GraphX runtime):

1. Graph-level execution and dependency resolution.
2. Queue or stream-level dispatch in GPU nodes.
3. Explicit transfer, synchronization, sharding, and collectives.
4. Per-run and per-device telemetry export.

### Proposed integration interfaces

1. IGraphWorkSchedulerAdapter
   - Submit(GraphRunRequest) -> RunHandle
   - Cancel(RunHandle)
   - QueryStatus(RunHandle)
2. IGraphPlacementHintProvider
   - Resolve placement capabilities and constraints for a graph version.
3. IGraphTelemetryExporter
   - Emits run-level and stage-level metrics for framework observability.
4. IGraphAdmissionFeedback
   - Returns immediate accept, defer, or reject signals with reason codes.

### Execution flow

1. Framework submits GraphRunRequest to GraphX adapter.
2. GraphX validates backend capabilities and resource hints.
3. GraphX admits or defers request based on local pressure policies.
4. GraphX executes graph with explicit queue or stream and event semantics.
5. GraphX publishes progress and telemetry to framework.
6. Framework applies global policy based on telemetry and status updates.

### Policy ownership model

1. Framework-owned policies:
   - tenant fairness,
   - global autoscaling,
   - cross-workload prioritization,
   - cross-host placement.
2. GraphX-owned policies:
   - node and edge execution semantics,
   - local queue and stream overlap strategy,
   - local pool pressure handling,
   - deterministic payload and synchronization contracts.

### Required additions for this integration mode

1. Stable GraphRunRequest schema and status model.
2. Adapter layer exposing submit, cancel, and status primitives.
3. Local admission hooks with explicit pressure reason codes.
4. Telemetry export mapping to framework metric namespaces.
5. Optional run-level preemption and cooperative cancellation semantics.

### Non-goals for first integration pass

1. Embedding a full global scheduler inside GraphX.
2. Replacing framework autoscaling or placement engines.
3. Introducing hidden graph rewrites that change node-level determinism.

### Acceptance criteria for professional-framework mode

1. GraphRunRequest can be submitted and tracked end-to-end.
2. Admission and defer behavior is deterministic under configurable pressure thresholds.
3. Telemetry provides enough signal for external scheduler decisions.
4. Cancellation is safe and leaves pools, queues, and events in consistent state.
5. Backend parity maintained for CUDA and SYCL in control-plane contracts.

## Required Graph Framework Updates

A full GraphX rewrite is not required. Targeted framework updates are required to support GPU backends cleanly.

Required updates:

1. Capability bootstrap and selection
   - Add backend-aware capability registration and discovery paths for CUDA and SYCL.
2. Build and feature gating
   - Add and validate CMake feature flags and backend detection paths for CUDA and SYCL.
3. Backend-neutral payload placement
   - Introduce or adopt shared accel payload types with execution and event metadata.
4. Node and topology support for multi-GPU
   - Ensure graph execution path supports shard fan-out and fan-in nodes with deterministic ordering semantics.
5. Metrics surface extensions
   - Extend existing queue or thread metrics to include backend timing and per-device utilization signals.

Recommended (not strictly required for first delivery):

1. Backend-agnostic node naming and factory conventions
2. Standardized error taxonomy for copy, launch, and collective failures
3. Conformance test harness for CUDA versus SYCL semantic parity
4. Optional multi-GPU CI lanes with capability-aware skip and fallback behavior

Not required for this phase:

1. Replacing core node lifecycle APIs
2. Replacing message transport primitives
3. Introducing a heavyweight orchestration service or scheduler

## Risk Register and Mitigations

1. Risk: Hidden synchronizations degrade throughput
   - Mitigation: ban global synchronization calls in hot-path nodes, enforce event-based sync
2. Risk: Memory leaks under exceptions and failures
   - Mitigation: lease accounting plus scoped release utilities plus failure tests
3. Risk: Capability lookup overhead in hot path
   - Mitigation: resolve once at Init, store raw or shared refs in node state
4. Risk: Build portability issues
   - Mitigation: strict CMake option gating and CPU-only compatibility tests
5. Risk: Backend behavior drift between CUDA and SYCL implementations
   - Mitigation: parity conformance tests for node semantics and payload contracts
6. Risk: SYCL runtime variability across vendors
   - Mitigation: define minimum required SYCL features and provide capability checks at Init
7. Risk: Multi-GPU load imbalance reduces scaling
   - Mitigation: shard policy hooks and per-device telemetry-driven tuning
8. Risk: Collective backend mismatch or unavailability
   - Mitigation: capability probing with explicit fallback and clear diagnostics

## Review Checklist

1. Are abstractions thin and explicit enough for performance work?
2. Do payload contracts avoid hidden copies and ownership ambiguity?
3. Are lifecycle and error semantics aligned with existing node patterns?
4. Are required metrics sufficient for tuning and debugging?
5. Is phased rollout safe for existing pipelines and CI?
6. Are CUDA and SYCL graph-level semantics equivalent where required?
7. Is multi-GPU behavior explicit, deterministic, and measurable?

## Proposed Milestone Breakdown

1. M1 (1 week): Phase 0 and Phase 1 (CUDA baseline and SYCL scaffolding)
2. M2 (1 week): Phase 2 and Phase 3 (data movement parity)
3. M3 (1 week): Phase 4 and Phase 5 baseline (compute parity plus multi-GPU sharding)
4. M4 (1 week): Phase 5b and Phase 6 hardening plus docs plus parity review

## Deliverables for Architecture Review

1. This implementation plan
2. Interface header draft set for capabilities and payload types
3. One pilot topology and benchmark plan
4. CUDA and SYCL parity matrix and updated acceptance criteria after review decisions
5. Multi-GPU conformance matrix (sharding, peer copy, collectives)

## Framework Comparison Matrix

Legend:

1. High = near-production parity for the category
2. Medium = meaningful support but incomplete production surface
3. Low = early-stage or largely planned

### Executive Summary Matrix

| Decision Dimension | GraphX Position | Relative Standing vs Professional Frameworks | Primary Gap to Close |
|---|---|---|---|
| Execution control and determinism | High explicit control (queues/events/tokens) | Stronger transparency, less automation | Add selective automation without losing explicit semantics |
| Backend portability | CUDA + SYCL planned | Competitive direction, lower maturity | Harden SYCL backend and vendor validation matrix |
| Single-process multi-GPU | Sharding/peer/collective architecture defined | Near competitive for scoped workloads | Collective parity and performance tuning across backends |
| Distributed multi-node operations | Out of scope in this phase | Behind production frameworks | Integrate with external distributed control plane |
| Kernel and operator ecosystem | Focused set for target paths | Behind broad framework libraries | Expand optimized operator coverage by workload priority |
| Graph optimization automation | Minimal by design | Behind frameworks with rewrite/fusion/autotune stacks | Add targeted fusion/overlap autotuning layers |
| Control-plane integration | Professional-framework adapter model defined | Good substrate pattern, early implementation maturity | Implement scheduler/admission/telemetry contracts end-to-end |
| Observability and operations | Existing metrics + planned telemetry export | Partial parity | Add runbook-grade diagnostics and policy-facing metrics |

Quick read:

1. GraphX is positioned as an execution substrate with strong explicit semantics.
2. Professional frameworks lead in automation depth and distributed operations breadth.
3. The most valuable next steps are backend hardening, collectives parity, and control-plane adapter completion.

### Architecture View (Control Plane vs Data Plane)

```mermaid
flowchart LR
   A[Professional Framework Scheduler] -->|GraphRunRequest| B[GraphX Runtime Adapter]
   B --> C[Graph-Level Executor]
   C --> D[GPU Nodes and Capabilities]
   D --> E[CUDA Backend]
   D --> F[SYCL Backend]
   D --> G[Transfers and Collectives]
   G --> H[Multi-GPU Devices]
   D --> I[Telemetry Export]
   I --> A
```

### Execution and Backend Comparison

| Category | GraphX (this plan) | PyTorch + Ecosystem | TensorFlow + XLA | JAX + XLA | ONNX Runtime + TensorRT | oneAPI SYCL Stack | Key GraphX Gap |
|---|---|---|---|---|---|---|---|
| GPU backend breadth | CUDA + SYCL planned | CUDA + ROCm + ecosystem backends | CUDA + ROCm + TPU paths | CUDA + TPU + ROCm (deployment dependent) | CUDA + TensorRT + ROCm providers | SYCL-focused across vendors | Backend maturity and production hardening |
| Graph execution model | Explicit node graph, thin runtime | Dynamic + compiled graph modes | Static + eager + compiled graph modes | Functional tracing + JIT | Provider-dispatched execution graph | Kernel/task DAG style execution | Advanced graph rewrite and optimization passes |
| Async execution and overlap | High (explicit queues/streams + events) | High | High | High | High | High | Runtime autotuning for overlap policies |
| Memory pooling and reuse | Medium-High (pool-first design) | High (caching allocators) | High | High | High | Medium-High | Cross-workload allocator policy and fragmentation control |
| Multi-GPU single-process | High (shard/peer/collective planned) | High | High | High | Medium-High | Medium | Backend-validated collective parity across CUDA/SYCL |
| Operator and kernel library depth | Low-Medium | Very High | Very High | High (XLA lowering ecosystem) | High | Medium | Broad optimized operator catalog |
| Automatic graph optimization | Low-Medium | High (Inductor/Triton ecosystem) | High (Grappler/XLA) | High (XLA) | High (provider-level optimizations) | Medium | Rewrite engine, fusion planner, autotuning loop |
| Portability across non-NVIDIA GPUs | Medium (SYCL variant planned) | Medium-High (ROCm path) | Medium-High | Medium | Medium | High | SYCL backend maturity and vendor validation matrix |
| Deterministic explicit data movement | High (core design goal) | Medium | Medium | Medium | Medium | Medium-High | Keep determinism while adding automation |

### Operations and Scale Comparison

| Category | GraphX (this plan) | PyTorch Platform Stacks | TensorFlow Platform Stacks | JAX Platform Stacks | ONNX Runtime Serving Stacks | DeepSpeed / Horovod | Key GraphX Gap |
|---|---|---|---|---|---|---|---|
| Admission control | Medium (adapter-based model) | High | High | High | High | Medium | Production policy adapter maturity |
| Autoscaling | Medium (external framework expected) | High | High | High | High | Medium | Tight integration with control-plane scale signals |
| Placement engine sophistication | Medium (hint-driven, explicit ownership) | High | High | High | High | Medium | Advanced placement heuristics and remapping |
| Multi-node distributed orchestration | Low (out of scope) | High | High | High | Medium | High (primary focus) | Distributed control plane and transport integration |
| Observability and runbook depth | Medium (metrics + telemetry plan) | High | High | Medium-High | High | High | End-to-end operational tooling and diagnostics depth |

### Summary Positioning

1. GraphX is strongest where explicit control, deterministic movement, and thin abstractions are required.
2. Professional frameworks are strongest in operator breadth, automated optimization, and full distributed operations.
3. The fastest path to parity is not to copy entire framework surfaces, but to keep GraphX as an execution substrate with strong control-plane integration and focused optimization layers.

### Priority Gap Closure Roadmap

1. Short term
   - Stabilize CUDA/SYCL backend parity and multi-GPU collectives.
   - Add production-grade scheduler adapter and telemetry exporter contracts.
2. Mid term
   - Introduce selective graph optimizations (fusion opportunities, overlap autotuning, placement hints).
   - Expand optimized kernel/operator coverage for target workloads.
3. Long term
   - Add optional distributed multi-node orchestration integration.
   - Build production operations surface (SLO policy hooks, richer diagnostics, fleet-level observability).

## Implementation Task Board

Status legend:

1. Not Started
2. In Progress
3. Blocked
4. Complete

### Milestone M1: Contracts and Scheduler Adapter

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| M1-1 | Freeze shared payload contracts | Not Started | DeviceBufferView, HostPinnedBufferView, BufferLease, TransferTicket, KernelTicket, DeviceShardDescriptor, CollectiveTicket headers | Contract review approved and no breaking changes for one sprint |
| M1-2 | Freeze capability interfaces | Not Started | CUDA and SYCL capability interface headers under include/cuda/capabilities and include/sycl/capabilities | Interface review approved with parity checklist signed |
| M1-3 | Define GraphRunRequest and status schema | Not Started | Request, RunHandle, RunStatus, reason-code definitions | Submit/cancel/status API stable and documented |
| M1-4 | Implement scheduler adapter skeleton | Not Started | IGraphWorkSchedulerAdapter implementation with submit/cancel/query paths | End-to-end request lifecycle passes smoke tests |
| M1-5 | Telemetry exporter contract baseline | Not Started | IGraphTelemetryExporter and initial metric mapping | Framework-facing telemetry contract validated |

### Milestone M2: First Vertical Slice (SYCL-first, then CUDA parity)

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| M2-1 | SYCL backend baseline | Not Started | Context, queue/event, memory pool, transfer, kernel capability implementations | Single-device SYCL smoke tests green |
| M2-2 | First SYCL pipeline | Not Started | HostIngressPinnedSourceNodeSycl -> H2DAsyncNodeSycl -> DeviceTransformNodeSycl -> D2HAsyncNodeSycl -> HostEgressSinkNodeSycl | Correctness vs CPU reference with deterministic outputs |
| M2-3 | CUDA backend parity for baseline slice | Not Started | CUDA implementations for same capability and node subset | Same topology and tests pass on CUDA |
| M2-4 | Cross-backend conformance harness v1 | Not Started | Shared test suite validating output/lifecycle/error parity | CUDA and SYCL parity report generated |

### Milestone M3: Multi-GPU Core

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| M3-1 | Implement DeviceShardNode variants | Not Started | CUDA and SYCL shard fan-out nodes | 2-GPU shard correctness tests pass |
| M3-2 | Implement PeerCopyNode variants | Not Started | P2P copy path + staged fallback path | Peer transfer and fallback tests pass |
| M3-3 | Implement CollectiveReduceNode variants | Not Started | Collective capability wiring and node implementations | Collective result parity vs CPU reference |
| M3-4 | Multi-GPU topology integration test | Not Started | Shard -> per-device transform -> collective -> host egress topology | End-to-end multi-GPU test stable under repetition |

### Milestone M4: Hardening, Operations, and CI

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| M4-1 | Admission/defer policy hooks | Not Started | Configurable local pressure thresholds and reason codes | Deterministic admission tests pass |
| M4-2 | Cancellation safety | Not Started | Cooperative cancellation path across nodes/capabilities | No leaks and clean state after cancellation stress tests |
| M4-3 | CI backend lanes | Not Started | CUDA/SYCL feature-gated CI jobs and optional multi-GPU lane | Required lanes green with predictable skip semantics |
| M4-4 | Metrics and diagnostics hardening | Not Started | Per-device utilization, transfer overhead, queue/stream/event diagnostics | Operations checklist complete for pilot deployment |

### Milestone M5: Optimization and Expansion

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| M5-1 | Overlap autotuning hooks | Not Started | Queue/stream overlap policy hooks and benchmark scripts | Measurable p95 latency improvement on target workload |
| M5-2 | Selective fusion opportunities | Not Started | Candidate fused kernels and gating heuristics | Throughput uplift without regression in determinism |
| M5-3 | Targeted operator expansion | Not Started | Prioritized kernel/operator backlog implementation | Coverage target reached for pilot workload set |

### Backlog and Dependency Notes

1. M1-1 and M1-2 must complete before M2 implementation starts.
2. M2 conformance harness is a prerequisite for M3 backend parity validation.
3. M3 collective delivery should not block M4 cancellation and telemetry hardening.
4. M5 work starts only after M4 CI stability criteria are met.

## Real-World Benchmark Workload Track (RF/EW)

Purpose:

1. Validate architecture decisions with production-like signal-processing pressure.
2. Compare CUDA and SYCL on a meaningful workload instead of synthetic microbenchmarks only.
3. Provide objective go or no-go data for optimization and multi-GPU strategy.

Primary candidate workload (initial):

1. Mini SAR range-Doppler processing chain
   - input I/Q burst ingest
   - windowing
   - range FFT
   - matched filtering
   - azimuth FFT
   - magnitude and log scaling

Secondary candidate workload (fallback):

1. EW pulse-processing chain
   - STFT
   - CFAR detection
   - pulse clustering and extraction

### GraphX Mapping (Mini SAR)

1. HostIngressPinnedSourceNode -> emits I/Q burst blocks
2. H2DAsyncNode -> staged host-to-device transfer
3. DeviceTransformNode (windowing kernel)
4. DeviceTransformNode or FFT node (range FFT)
5. DeviceTransformNode (matched filter)
6. DeviceTransformNode or FFT node (azimuth FFT)
7. DeviceTransformNode (magnitude and log scaling)
8. Optional DeviceShardNode and CollectiveReduceNode for multi-GPU decomposition
9. D2HAsyncNode -> host transfer
10. HostEgressSinkNode -> output persistence and validation

### KPI Set for Benchmark Decisions

1. End-to-end latency (p50, p95, p99)
2. Throughput (bursts or range-lines per second)
3. Effective H2D and D2H bandwidth
4. Per-stage GPU utilization and occupancy
5. Multi-GPU scaling efficiency (1 GPU vs 2 GPU vs 4 GPU where available)
6. Numerical error vs CPU reference pipeline

### Benchmark Task Board Additions

| ID | Task | Status | Deliverables | Exit Criteria |
|---|---|---|---|---|
| B1 | CPU reference pipeline | Not Started | Deterministic CPU mini-SAR (or EW fallback) reference implementation | Golden outputs generated for fixed datasets |
| B2 | Dataset and harness | Not Started | Reproducible benchmark dataset pack and run harness | Same inputs usable across CUDA and SYCL runs |
| B3 | Single-GPU CUDA benchmark path | Not Started | End-to-end CUDA benchmark topology integration | Meets baseline throughput and correctness thresholds |
| B4 | Single-GPU SYCL benchmark path | Not Started | End-to-end SYCL benchmark topology integration | Meets baseline throughput and correctness thresholds |
| B5 | Multi-GPU benchmark decomposition | Not Started | Shard strategy and collective integration for benchmark topology | Demonstrates positive scaling over single-GPU baseline |
| B6 | Cross-backend benchmark report | Not Started | CUDA vs SYCL comparison report with KPI tables and bottleneck analysis | Review sign-off on backend strategy and next optimizations |

### Milestone Alignment for Benchmark Track

1. M2: deliver B1 and B2, plus first single-GPU backend integration (B3 or B4).
2. M3: complete both single-GPU paths and multi-GPU decomposition (B3, B4, B5).
3. M4: publish cross-backend benchmark report and recommendations (B6).

### Acceptance Thresholds (Initial)

1. Correctness
   - relative error against CPU reference below agreed tolerance per stage and end-to-end output.
2. Stability
   - no deadlocks, leaks, or repeated-run instability across at least 100 consecutive benchmark runs.
3. Performance
   - measurable improvement from overlap and pooling strategy compared to non-overlapped baseline.
4. Portability
   - benchmark topology executes on both CUDA and SYCL paths with shared control-plane contracts.

## Detailed Implementation Plan

This section defines the concrete build order, deliverables, and review gates for implementation.

### Workstream A: Contracts and Build System

Objective:

1. Stabilize interfaces and compile-time feature control before backend implementation.

Steps:

1. Add and validate feature flags:
   - ENABLE_CUDA_GRAPH_NODES
   - ENABLE_SYCL_GRAPH_NODES
   - MULTI_GPU_TESTS
2. Add backend detection and capability macros in CMake.
3. Create shared payload headers under accel/types.
4. Create CUDA and SYCL capability interface headers.
5. Add static validation helpers for payload invariants.

Artifacts:

1. Build configuration updates and feature docs.
2. Payload contract headers.
3. Capability interface headers.

Exit criteria:

1. CPU-only build remains green when both backend flags are OFF.
2. Backend flags compile interface-only stubs without linking full implementations.
3. Contract review signed off and no breaking changes for one sprint.

### Workstream B: Scheduler Adapter and Control-Plane Contract

Objective:

1. Enable professional framework integration through stable GraphRunRequest semantics.

Steps:

1. Implement GraphRunRequest schema and run status model.
2. Implement IGraphWorkSchedulerAdapter submit/cancel/query status path.
3. Implement local admission feedback with explicit reason codes.
4. Implement telemetry export contract and baseline metric mapping.
5. Add cancellation propagation hooks to graph run lifecycle.

Artifacts:

1. Adapter implementation and tests.
2. Admission reason code catalog.
3. Telemetry contract and example exporter.

Exit criteria:

1. End-to-end GraphRunRequest lifecycle works in integration tests.
2. Deterministic accept/defer behavior under synthetic pressure tests.
3. Cancellation leaves queues, events, and pools in clean state.

### Workstream C: SYCL Baseline Vertical Slice

Objective:

1. Deliver first practical GPU execution path on available non-NVIDIA hardware.

Steps:

1. Implement SYCL context, queue/event, memory pool, transfer, and kernel capabilities.
2. Implement nodes:
   - HostIngressPinnedSourceNodeSycl
   - H2DAsyncNodeSycl
   - DeviceTransformNodeSycl
   - D2HAsyncNodeSycl
   - HostEgressSinkNodeSycl
3. Add CPU reference comparison harness for correctness.
4. Add first latency and throughput instrumentation.

Artifacts:

1. SYCL capability backends.
2. First end-to-end SYCL topology.
3. Correctness and baseline performance report.

Exit criteria:

1. Topology passes correctness checks against CPU reference.
2. No deadlocks or leaks over repeated run soak tests.
3. Telemetry fields populated and exported.

### Workstream D: CUDA Parity Slice

Objective:

1. Achieve feature and semantic parity with SYCL baseline.

Steps:

1. Implement CUDA capability backends for same vertical slice.
2. Implement corresponding CUDA nodes.
3. Run shared conformance harness across CUDA and SYCL.
4. Resolve differences in event ordering and transfer semantics.

Artifacts:

1. CUDA baseline topology.
2. Cross-backend conformance report.

Exit criteria:

1. Same topology and tests pass on CUDA and SYCL.
2. Output parity and lifecycle parity deviations documented and accepted.

### Workstream E: Multi-GPU Core

Objective:

1. Validate shard, peer movement, and collective functionality.

Steps:

1. Implement DeviceShardNode variants.
2. Implement PeerCopyNode with P2P and staged fallback.
3. Implement CollectiveReduceNode and collective capability backends.
4. Build 2-GPU reference topology and run scaling tests.

Artifacts:

1. Multi-GPU nodes and capability implementations.
2. 2-GPU topology integration tests.
3. Scaling and stability report.

Exit criteria:

1. 2-GPU correctness and stability tests pass.
2. Collective behavior deterministic under repeated runs.
3. Positive scaling trend over single-GPU baseline on target workload.

### Workstream F: RF/EW Benchmark Track

Objective:

1. Benchmark architecture with a realistic SAR/EW workload.

Steps:

1. Build deterministic CPU reference pipeline (Mini SAR primary, EW fallback).
2. Finalize dataset pack and reproducible benchmark harness.
3. Integrate benchmark topology for CUDA and SYCL.
4. Add multi-GPU decomposition and collective reduction path.
5. Generate cross-backend performance and correctness report.

Artifacts:

1. CPU golden outputs.
2. Benchmark harness and scripts.
3. CUDA/SYCL benchmark report with bottleneck analysis.

Exit criteria:

1. Benchmark runs on both backends with shared control-plane contract.
2. KPI deltas available for roadmap prioritization.
3. Multi-GPU benchmark path demonstrates measurable value where expected.

### Workstream G: Hardening and Release Readiness

Objective:

1. Move from prototype to production-ready integration quality.

Steps:

1. Extend CI lanes for backend-gated and optional multi-GPU tests.
2. Add failure-mode tests (allocation failure, collective unavailable, cancellation race).
3. Add diagnostics for queue/stream/event state and pool pressure.
4. Freeze error taxonomy for transfer, kernel, and collective failures.
5. Complete docs and operations notes.

Artifacts:

1. CI definitions and gating policies.
2. Hardening test suite.
3. Runbook-oriented diagnostics documentation.

Exit criteria:

1. Required CI lanes green and stable.
2. Operational diagnostics sufficient for first pilot deployment.
3. Release checklist fully complete.

## Implementation Checklist

### Contract and Build Checklist

1. ENABLE_CUDA_GRAPH_NODES implemented and documented.
2. ENABLE_SYCL_GRAPH_NODES implemented and documented.
3. MULTI_GPU_TESTS implemented and documented.
4. Shared payload contracts reviewed and approved.
5. CUDA capability interfaces reviewed and approved.
6. SYCL capability interfaces reviewed and approved.

### Scheduler and Integration Checklist

1. GraphRunRequest schema finalized.
2. Run status and reason codes finalized.
3. Scheduler adapter submit/cancel/query implemented.
4. Admission accept/defer/reject path implemented.
5. Telemetry exporter contract implemented.
6. Cancellation semantics validated under stress.

### Backend Parity Checklist

1. SYCL baseline pipeline passes correctness tests.
2. CUDA baseline pipeline passes correctness tests.
3. Shared conformance harness passes for both backends.
4. Event and synchronization semantics differences documented.
5. Backend parity report generated.

### Multi-GPU Checklist

1. DeviceShardNode implemented for CUDA and SYCL.
2. PeerCopyNode implemented for CUDA and SYCL.
3. CollectiveReduceNode implemented for CUDA and SYCL.
4. P2P fallback path validated.
5. 2-GPU integration topology passes correctness tests.
6. Multi-GPU stability soak test passes.

### Benchmark Checklist

1. CPU reference Mini SAR pipeline implemented.
2. EW fallback reference path available.
3. Reproducible dataset and harness finalized.
4. CUDA benchmark topology integrated.
5. SYCL benchmark topology integrated.
6. Cross-backend KPI comparison report produced.

### Quality and Release Checklist

1. Backend-gated CI lanes added and stable.
2. Optional multi-GPU CI lane configured with clear skip logic.
3. Failure-mode test suite passes.
4. Metrics and diagnostics coverage validated.
5. Error taxonomy documented.
6. Pilot readiness review completed and signed off.

### Weekly Review Cadence

1. Review open blockers and status transitions for all in-progress tasks.
2. Refresh parity matrix and conformance outcomes for CUDA and SYCL.
3. Record performance deltas for target topologies (single-GPU and multi-GPU).
4. Reprioritize backlog based on risk, hardware availability, and integration milestones.
