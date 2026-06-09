# Prompt: Implement Metal-Equivalent Nodes For SAR Definitive Topology

Use this prompt as the full instruction prompt for implementing Metal-equivalent execution for the SAR definitive topology.

Repository root:

```text
/Users/rklinkhammer/workspace/GraphX
```

Canonical topology:

```text
examples/SAR/config/sar_stripmap_definitive.json
```

Do not implement from a legacy topology unless a test explicitly requires legacy compatibility. The definitive JSON is the source of truth.

## Objective

Implement Metal-equivalent execution for every node in `examples/SAR/config/sar_stripmap_definitive.json` where a Metal equivalent is architecturally valid and performance-relevant.

The implementation must:

1. Keep `GraphExecutorBuilder + JSON` as the canonical execution path.
2. Keep `sar_stripmap_definitive.json` portable: node `type` values should remain intent types unless a node is intentionally concrete and documented.
3. Use dynamic resolver mappings for SAR-specific backend substitutions.
4. Keep SAR-specific behavior in `examples/SAR`.
5. Put common reusable Metal node functionality in `libgpu`.
6. Avoid duplicate nodes: do not create a SAR-local `*Metal` node when an equivalent generic `libgpu` Metal node already exists.
7. Demonstrate performance improvement or provide measured bottleneck attribution if improvement is not achieved.

## Current Definitive Pipeline

Implement/analyze Metal-equivalent support for these node intents:

1. `SyntheticApertureIqSourceNode`
2. `RangeWindowNode`
3. `RangeCompressionNode`
4. `AzimuthTileSplitNode`
5. `H2DAsyncNode`
6. `SarBackprojectionTransformNode`
7. `D2HAsyncNode`
8. `ImageTileMergeNode`
9. `SarDiagnosticsSinkNode`

## Placement Rules

Keep in `examples/SAR`:

1. SAR source/replay behavior.
2. SAR-specific math adapters, metadata sidecars, geometry contracts, tile identity, image merge policy, diagnostics policy, and validation fixtures.
3. SAR adapters that delegate to generic `libgpu` nodes while preserving SAR sidecars.
4. SAR-specific resolver mappings in JSON or future SAR/plugin metadata.

Put in `libgpu`:

1. Generic Metal async transfer nodes.
2. Generic device buffer lease/view/ticket types.
3. Generic device transform, kernel, reduce, shard, sync, copy, source, and sink nodes.
4. Reusable Metal descriptors, queue selection, buffer lifecycle, and timing utilities that can serve non-SAR pipelines.

Do not move SAR-only semantics into `libgpu` unless there is a non-SAR consumer or a clearly reusable abstraction.

## No-Duplicate Rule

Before adding any node, search `libgpu` for an existing equivalent.

Do not add SAR-local duplicates of:

1. `HostIngressPinnedSourceNodeMetal`
2. `H2DAsyncNodeMetal`
3. `DeviceShardNodeMetal`
4. `DeviceTransformNodeMetal`
5. `DeviceKernelNodeMetal`
6. `DeviceReduceNodeMetal`
7. `D2HAsyncNodeMetal`
8. `HostEgressSinkNodeMetal`
9. `QueueSyncNodeMetal`
10. `LeaseReleaseNodeMetal`
11. `PeerCopyNodeMetal`
12. `CollectiveReduceNodeMetal`

Allowed SAR-side work:

1. A SAR adapter that preserves sidecars and delegates to a generic `libgpu` node.
2. A SAR-specific node only when its semantics are not equivalent to a `libgpu` node.
3. A SAR-specific Metal kernel descriptor or inline shader payload used by `DeviceKernelNodeMetal` or `DeviceTransformNodeMetal`, if the descriptor is not generally reusable yet.

If a SAR-local `*Metal` node is proposed, explicitly justify why it is not a duplicate of an existing `libgpu` node. Otherwise do not create it.

## Resolver Requirements

Use the dynamic resolver architecture.

1. Generic GPU intent mappings belong in the default resolver registry.
2. SAR-specific intent mappings belong in SAR JSON `resolver_mappings` or future plugin-provided metadata.
3. Do not hard-code SAR node names in `libgraph` resolver implementation.
4. Do not rely on explicit Metal-only JSON as the main path.
5. Explicit Metal JSON may exist only as a diagnostic or compatibility artifact.

The definitive JSON must retain:

```json
{
  "execution_backend": "auto",
  "backend_fallback_policy": "strict",
  "resolver_diagnostics": true,
  "edge_contract": "accel-token"
}
```

If strict `auto` cannot be used for a test environment, use a temporary test/runtime copy rather than changing the canonical intent of the definitive JSON.

## Accel-Token Contract

The SAR path must use accel-token edges.

Required invariants:

1. `edge_contract` remains `accel-token`.
2. Transfer and kernel stages exchange token/view/ticket contracts, not legacy SAR payload messages.
3. SAR metadata travels as sidecars, not as a replacement for accel-token contracts.
4. H2D, kernel/transform, D2H, merge, and diagnostics preserve frame identity:
   - stream id
   - sequence id
   - batch/aperture identity where present
   - tile id
   - marker/EOS/watermark state
   - backend/queue/dispatch metadata
5. Tests must reject legacy payload contracts under accel-token mode.

## Node-By-Node Implementation Strategy

For each definitive node, classify it as one of:

1. Portable intent only.
2. Resolver-substituted to generic `libgpu` Metal node.
3. SAR adapter over generic `libgpu` Metal node.
4. SAR-specific node with no direct Metal replacement.
5. Future/non-goal for this PR.

Recommended initial classification:

| SAR node | Metal strategy | Ownership |
| --- | --- | --- |
| `SyntheticApertureIqSourceNode` | No direct replacement; optionally use host-pinned token mechanics later | `examples/SAR` |
| `RangeWindowNode` | Prefer adapter or descriptor using `DeviceTransformNodeMetal` | SAR adapter in `examples/SAR`; generic transform in `libgpu` |
| `RangeCompressionNode` | Prefer adapter or descriptor using `DeviceKernelNodeMetal`; CPU parity is mandatory | SAR adapter/descriptor in `examples/SAR`; generic dispatch in `libgpu` |
| `AzimuthTileSplitNode` | No direct replacement; graph/tile routing semantics remain SAR-specific | `examples/SAR` |
| `H2DAsyncNode` | Resolver-substitute to `H2DAsyncNodeMetal` when available | `libgpu` concrete Metal node |
| `SarBackprojectionTransformNode` | SAR adapter delegates to `DeviceKernelNodeMetal` in native-device mode | Adapter in `examples/SAR`; generic kernel node in `libgpu` |
| `D2HAsyncNode` | Resolver-substitute to `D2HAsyncNodeMetal` when available | `libgpu` concrete Metal node |
| `ImageTileMergeNode` | Keep host-side correctness path; optional future reduce experiment only after parity | `examples/SAR` |
| `SarDiagnosticsSinkNode` | Keep SAR diagnostics sink; may consume generic timing fields | `examples/SAR` |

## Required Work

### 1. Inventory and Design Note

Create or update a design note that lists:

1. Every node in `sar_stripmap_definitive.json`.
2. Current implementation file(s).
3. Existing nearest `libgpu` Metal node.
4. Whether the stage is a direct replacement, adapter, or SAR-specific.
5. Whether a new node is required.
6. Why the design does not duplicate an existing `libgpu` node.
7. Which resolver mapping, if any, selects the Metal-capable path.

### 2. Resolver Mapping Updates

Update `examples/SAR/config/sar_stripmap_definitive.json` and maintained SAR presets only as needed.

Rules:

1. Keep portable node types.
2. Add/adjust `resolver_mappings` for SAR-specific substitutions only.
3. Do not add SAR mappings to `libgraph`.
4. Tests must prove resolver strict/fallback behavior.

### 3. Code Changes

Implement the smallest safe slice that makes one or more definitive stages use real Metal-capable execution.

Preferred order:

1. Transfer stages: ensure `H2DAsyncNode` and `D2HAsyncNode` resolve to common `libgpu` Metal nodes when available.
2. `RangeWindowNode`: use `DeviceTransformNodeMetal` if the current descriptor path can support the operation.
3. `RangeCompressionNode`: use `DeviceKernelNodeMetal` only after CPU reference parity is locked.
4. `SarBackprojectionTransformNode`: keep/extend adapter delegation to `DeviceKernelNodeMetal`; do not create `SarBackprojectionTransformNodeMetal`.

Do not add new GPU kernels unless required for the selected implementation slice and justified by tests/performance evidence.

### 4. Tests

Add or update tests for:

1. Resolver substitution for generic `libgpu` Metal nodes.
2. Dynamic SAR resolver mappings.
3. Strict Metal failure when required concrete nodes are unavailable.
4. `allow_fallback` success when Metal is unavailable.
5. Sidecar preservation across H2D/kernel/D2H/merge.
6. Completion signaling through `GraphExecutorBuilder`.
7. CPU reference parity for any SAR math moved to a Metal path.
8. No duplicate-node rule:
   - search/test should prove SAR does not register a duplicate of an existing `libgpu` Metal node.

### 5. Performance Validation

Add or update benchmark support that compares:

1. Definitive topology with `execution_backend=auto`.
2. Definitive topology with `execution_backend=metal`.
3. Direct/non-graph baseline for graph-overhead attribution only.

Performance output must include:

1. Per-run wall-clock milliseconds.
2. min/median/max/stddev.
3. Graph build time.
4. Graph run time.
5. Baseline execution time.
6. Graph overhead: graph median minus baseline median.
7. Transfer bytes H2D/D2H.
8. Kernel dispatch count.
9. Queue wait/backpressure metrics.
10. Backend synchronization or queue timing where available.
11. Resolver diagnostics / concrete node selections.
12. Explicit improvement/regression summary for Metal versus non-Metal/auto.

Acceptance policy:

1. Prefer measurable improvement for at least one meaningful profile.
2. If there is no improvement, report the bottleneck and explain whether the limitation is scheduler overhead, fallback behavior, data movement, kernel work size, or missing native kernel coverage.
3. Do not claim acceleration from metadata-only backend tags.

## Documentation Updates

Update:

1. `examples/SAR/README.md`
2. `plan/SAR.md`
3. Relevant PR checklist or create one if missing

Documentation must state:

1. Which nodes have Metal equivalents.
2. Which nodes remain SAR-specific.
3. Which nodes use generic `libgpu` Metal implementations.
4. Which resolver mappings are dynamic and SAR-owned.
5. How to run the benchmark.
6. How to interpret performance results.
7. Why no duplicate nodes were introduced.

## Validation Commands

Run the most relevant available commands after implementation:

```bash
cmake --build build-ninja/ninja-debug --target test_libgraph_unit
./build-ninja/ninja-debug/libgraph/test/test_libgraph_unit '--gtest_filter=ResolvingNodeProviderTest.*:GraphConfigParserExpectedTest.*Resolver*'

cmake --build build-ninja/ninja-debug --target test_sar_example_unit
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit

cmake --build build-ninja/ninja-debug --target sar_benchmark
./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci
```

If benchmark scripts exist for Metal versus non-Metal comparison, run them and include the output summary.

## Final Response Requirements

At completion, report:

1. Summary of implemented Metal-equivalent behavior.
2. Node mapping table.
3. Files changed.
4. Tests run and pass/fail status.
5. Benchmark results with improvement/regression interpretation.
6. Any residual risks or next recommended optimization.

Do not claim full Metal acceleration unless the measured path actually uses Metal-capable `libgpu` nodes or native Metal kernel execution.

