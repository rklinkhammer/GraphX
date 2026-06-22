
## GraphX Node Generalization Plan

### Goal

Examine all GraphX node implementations and determine whether repeated patterns can be generalized or simplified **without changing existing input and output port definitions**.

Primary focus:

* Split nodes
* Merge nodes
* Fan-out / fan-in nodes
* N-input / M-output nodes
* Nodes that differ only by token type, routing policy, buffering policy, or execution backend

## 1. Inventory all node implementations

Create a table for every node:

| Field              | Description                                                       |
| ------------------ | ----------------------------------------------------------------- |
| Node name          | Class / file name                                                 |
| Category           | source, sink, transform, split, merge, adapter, GPU, SAR-specific |
| Input ports        | Names and types                                                   |
| Output ports       | Names and types                                                   |
| Token type         | Message/token payload type                                        |
| State              | Stateless, buffered, accumulating, windowed                       |
| Execution behavior | synchronous, async, worker, GPU event-driven                      |
| Routing behavior   | 1→1, 1→N, N→1, N→M                                                |
| Failure behavior   | drop, block, retry, emit error                                    |
| Domain coupling    | generic, SAR-specific, GPU-specific                               |

The important rule: **ports are treated as fixed external contracts**.

## 2. Classify node behavior by shape

Group nodes by dataflow shape:

### Basic shapes

| Shape | Meaning                               |
| ----- | ------------------------------------- |
| 0→1   | Source                                |
| 1→0   | Sink                                  |
| 1→1   | Transform                             |
| 1→N   | Split / fan-out                       |
| N→1   | Merge / join / reduction              |
| N→M   | Router / exchanger / staged transform |

This is likely where simplification will emerge.

For each node, ask:

> Is this node special because of its ports, or only because of its policy?

If the answer is “only policy,” it may be a candidate for a generalized base.

## 3. Separate invariant mechanics from policy

For split and merge nodes, look for reusable mechanics.

### Split node mechanics

Common responsibilities may include:

* Read one input token.
* Validate sidecar or metadata.
* Compute output partitioning.
* Emit one or more output tokens.
* Preserve ordering or sequence numbers.
* Track end-of-stream / flush behavior.

Candidate abstraction:

```cpp
template <typename InputToken, typename SplitPolicy, typename PortBinding>
class SplitNodeBase;
```

Where `SplitPolicy` decides:

* number of outputs used,
* how tokens are partitioned,
* whether data is copied, sliced, leased, or referenced,
* output ordering.

### Merge node mechanics

Common responsibilities may include:

* Receive from N input ports.
* Match compatible tokens.
* Buffer incomplete sets.
* Apply merge policy.
* Emit one output token.
* Handle missing, late, duplicate, or end-of-stream tokens.

Candidate abstraction:

```cpp
template <typename OutputToken, typename MergePolicy, typename PortBinding>
class MergeNodeBase;
```

Where `MergePolicy` decides:

* when inputs are complete,
* how tokens are matched,
* how payloads/views are combined,
* what to do with partial input.

## 4. Preserve concrete node names and ports

Do **not** replace existing user-facing nodes immediately.

Instead, refactor concrete nodes into thin wrappers:

```cpp
class AzimuthTileSplitNode
  : public SplitNodeBase<
        SarToken,
        AzimuthTileSplitPolicy,
        AzimuthTileSplitPorts> {};
```

```cpp
class ImageTileMergeNode
  : public MergeNodeBase<
        SarImageToken,
        ImageTileMergePolicy,
        ImageTileMergePorts> {};
```

This preserves:

* existing node class names,
* existing input port names,
* existing output port names,
* existing graph configuration behavior,
* existing tests.

## 5. Identify possible generic abstractions

Likely candidates:

### `TransformNodeBase`

For 1→1 nodes.

Used by:

* format adapters,
* metadata transforms,
* SAR stage transforms,
* CPU/GPU token transforms.

### `SplitNodeBase`

For 1→N fan-out and tiling nodes.

Used by:

* azimuth split,
* range split,
* image tile split,
* multi-backend dispatch split.

### `MergeNodeBase`

For N→1 reconstruction or aggregation.

Used by:

* image tile merge,
* partial result merge,
* accumulator-style nodes.

### `RouterNodeBase`

For N→M nodes.

Used by:

* conditional routing,
* backend selection,
* stage dispatch,
* token demultiplexing.

### `BarrierJoinNodeBase`

For nodes that require one token from each input before emitting.

Used by:

* synchronized multi-input computation,
* metadata + data joins,
* image assembly.

## 6. Key design constraint

Avoid over-generalizing into a confusing universal node.

A good abstraction should remove duplicated mechanics while leaving domain intent visible.

Bad abstraction:

```cpp
UniversalGraphNode<N, M, Policy, Token, Backend, Sidecar, Router>
```

Better abstraction:

```cpp
SplitNodeBase
MergeNodeBase
TransformNodeBase
RouterNodeBase
BarrierJoinNodeBase
```

Each one should map to a real dataflow concept.

## 7. Review questions for every node

For each implementation, answer:

1. Does this node have unique port contracts?
2. Does this node have unique domain semantics?
3. Does it duplicate scheduling, buffering, routing, or token-handling logic?
4. Could its behavior be expressed as a policy?
5. Would refactoring reduce code without hiding intent?
6. Are tests sufficient to prove unchanged port behavior?
7. Does the node depend on SAR semantics, GPU semantics, or both?

## 8. Suggested deliverables

### Deliverable 1: Node inventory report

`GRAPHX_NODE_INVENTORY_REPORT.md`

Contains the full table of nodes and classifications.

### Deliverable 2: Generalization candidate report

`GRAPHX_NODE_GENERALIZATION_REPORT.md`

Sections:

* High-confidence refactors
* Medium-confidence refactors
* Nodes that should remain specialized
* Split/merge-specific findings
* N/M node patterns
* Risks

### Deliverable 3: Refactoring roadmap

`GRAPHX_NODE_SIMPLIFICATION_ROADMAP.md`

Possible PR sequence:

| PR  | Scope                                        |
| --- | -------------------------------------------- |
| PR1 | Inventory and classification only            |
| PR2 | Extract shared transform mechanics           |
| PR3 | Extract split base/policy model              |
| PR4 | Extract merge base/policy model              |
| PR5 | Introduce N/M routing abstraction            |
| PR6 | Remove duplicated legacy code                |
| PR7 | Add regression tests proving unchanged ports |

## 9. Recommended planner-agent instruction

Use this core instruction:

> Inspect all GraphX node implementations. Identify repeated structural patterns and opportunities for simplification or generalization. Do not change public node class names, input port names, output port names, token contracts, graph construction behavior, or observable execution semantics. Pay special attention to split nodes, merge nodes, fan-in/fan-out nodes, and generalized N-input/M-output nodes. Prefer small, explicit abstractions over a universal node framework. Produce an inventory report, candidate generalization report, and staged implementation roadmap.

The key principle should be:

> **Generalize mechanics, not meaning.**
