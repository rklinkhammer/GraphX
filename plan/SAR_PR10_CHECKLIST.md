# SAR PR10 Checklist: Metal-Equivalent Nodes For Definitive SAR Topology

Status:

- [x] PR10 analysis complete
- [x] PR10 checklist created
- [ ] PR10 implementation started
- [ ] PR10 implementation complete
- [ ] PR10 ready for review
- [ ] PR10 merged

## Objective

Implement Metal-equivalent execution for the nodes in:

```text
examples/SAR/config/sar_stripmap_definitive.json
```

while preserving the GraphX SAR architecture:

- GraphExecutorBuilder + JSON remains the canonical execution path.
- `sar_stripmap_definitive.json` remains the canonical topology.
- SAR-specific behavior stays in `examples/SAR`.
- reusable/common Metal primitives stay in `libgpu`.
- resolver mappings for SAR-specific substitutions are dynamic and SAR-owned.
- no duplicate SAR-local Metal nodes are introduced when `libgpu` already owns the equivalent node.

## Current Evidence Baseline

- [x] Definitive topology uses portable node types.
- [x] Definitive topology declares:
  - [x] `execution_backend`
  - [x] `backend_fallback_policy`
  - [x] `resolver_diagnostics`
  - [x] `edge_contract`
  - [x] SAR-owned `resolver_mappings`
- [x] `libgraph` default resolver registry contains generic GPU mappings.
- [x] `libgraph` resolver implementation has no SAR-specific static mapping.
- [x] SAR backprojection adapter delegates native-device execution through `DeviceKernelNodeMetal`.
- [x] CPU reference parity exists for point-target backprojection, matched-filter range compression, and native Metal backprojection adapter.
- [x] Benchmark trace schema includes graph-overhead attribution and performance-claim policy fields.

## Mandatory Architecture Invariants

- [ ] Preserve `GraphExecutorBuilder + JSON` as the user-facing runtime path.
- [ ] Keep direct/non-graph execution limited to CPU reference, parity checks, baselines, and graph-overhead attribution.
- [ ] Keep definitive JSON node `type` values portable; do not replace them with concrete `*Metal` types in the canonical topology.
- [ ] Keep `edge_contract` set to `accel-token`.
- [ ] Preserve SAR sidecar identity across transfer/kernel/merge stages.
- [ ] Do not add SAR node names to `libgraph` resolver defaults.
- [ ] Do not use explicit Metal-only JSON as the primary runtime architecture.
- [ ] Do not claim speedup from lifecycle total metrics.

## No-Duplicate Node Guardrail

Before adding a node, search `libgpu` for an existing equivalent.

Do not add SAR-local duplicates of:

- [ ] `HostIngressPinnedSourceNodeMetal`
- [ ] `H2DAsyncNodeMetal`
- [ ] `DeviceShardNodeMetal`
- [ ] `DeviceTransformNodeMetal`
- [ ] `DeviceKernelNodeMetal`
- [ ] `DeviceReduceNodeMetal`
- [ ] `D2HAsyncNodeMetal`
- [ ] `HostEgressSinkNodeMetal`
- [ ] `QueueSyncNodeMetal`
- [ ] `LeaseReleaseNodeMetal`
- [ ] `PeerCopyNodeMetal`
- [ ] `CollectiveReduceNodeMetal`

Allowed SAR-side work:

- [ ] SAR adapter that preserves SAR metadata sidecars and delegates to generic `libgpu` Metal nodes.
- [ ] SAR-specific descriptor/kernel payload used by generic `DeviceKernelNodeMetal` or `DeviceTransformNodeMetal`.
- [ ] SAR-specific node only when its semantics are not equivalent to an existing `libgpu` node.

If a SAR-local `*Metal` node is proposed:

- [ ] Document why it is not a duplicate.
- [ ] Add tests proving why a generic `libgpu` node cannot cover the behavior.
- [ ] Add resolver mapping through SAR-owned `resolver_mappings`, not through `libgraph`.

## Node Mapping Checklist

| Definitive node | Current classification | PR10 target | Status |
| --- | --- | --- | --- |
| `SyntheticApertureIqSourceNode` | SAR-specific source | Keep in `examples/SAR`; no direct Metal replacement | [ ] Confirm/document |
| `RangeWindowNode` | Host/SAR-message Hann window | SAR adapter over `DeviceTransformNodeMetal` if parity and token bridge are feasible | [ ] Implement or defer with evidence |
| `RangeCompressionNode` | CPU matched-filter/FFT path | SAR adapter over `DeviceKernelNodeMetal` only after CPU parity gate | [ ] Analyze/likely defer |
| `AzimuthTileSplitNode` | SAR tile routing/identity | Keep SAR-specific; no `DeviceShardNodeMetal` duplicate | [ ] Confirm/document |
| `H2DAsyncNode` | Generic intent with default Metal mapping | Resolve to `libgpu` `H2DAsyncNodeMetal` when available | [ ] Implement/provider-test |
| `SarBackprojectionTransformNode` | SAR adapter over `DeviceKernelNodeMetal` | Keep adapter; improve evidence and diagnostics | [ ] Verify/document |
| `D2HAsyncNode` | Generic intent with default Metal mapping | Resolve to `libgpu` `D2HAsyncNodeMetal` when available | [ ] Implement/provider-test |
| `ImageTileMergeNode` | SAR fan-in/diagnostics | Keep host/SAR-specific; no device reduce replacement in PR10 | [ ] Confirm/document |
| `SarDiagnosticsSinkNode` | SAR diagnostics sink | Keep SAR-specific; consume timing/telemetry evidence | [ ] Confirm/document |

## PR10A: Provider And Resolver Proof

- [ ] Ensure SAR runtime provider/bootstrap path can load or compose common `libgpu` Metal plugins with SAR plugins.
- [ ] Add test proving definitive topology can resolve `H2DAsyncNode -> H2DAsyncNodeMetal` when Metal plugin is available.
- [ ] Add test proving definitive topology can resolve `D2HAsyncNode -> D2HAsyncNodeMetal` when Metal plugin is available.
- [ ] Add test proving SAR-specific backprojection mapping comes from `resolver_mappings`.
- [ ] Add strict Metal failure test when required concrete Metal provider entries are unavailable.
- [ ] Add `allow_fallback` test using the same definitive topology.
- [ ] Ensure resolver diagnostics report actual concrete types selected by `GraphBuilder`, not benchmark hard-coded summaries.

## PR10B: First Metal-Equivalent Algorithm Stage

Preferred first stage: `RangeWindowNode`.

- [ ] Decide whether `DeviceTransformNodeMetal` can support deterministic Hann windowing with current descriptor path.
- [ ] If yes, implement SAR sidecar-preserving adapter over `DeviceTransformNodeMetal`.
- [ ] If no, document blocker and keep PR10B scoped to transfer/backprojection evidence.
- [ ] Add CPU reference parity test for Metalized range window.
- [ ] Add graph/direct parity test for range window output or deterministic diagnostics.
- [ ] Preserve EOS/watermark behavior.
- [ ] Preserve envelope fields:
  - [ ] stream id
  - [ ] sequence id
  - [ ] batch id
  - [ ] aperture id
  - [ ] pulse range
  - [ ] tile id/count
  - [ ] frame marker
  - [ ] backend/queue metadata

## Range Compression Gate

Do not implement Metal range compression until these are true:

- [ ] CPU matched-filter parity remains green.
- [ ] Complex-output and magnitude-output parity tolerances are explicit.
- [ ] Kernel descriptor can express required chirp/filter parameters.
- [ ] Benchmark shows range compression is meaningful enough to justify GPU movement.
- [ ] Sidecar/token propagation is already proven through the previous PR10 stages.

If implemented:

- [ ] Use `DeviceKernelNodeMetal` or another generic `libgpu` primitive.
- [ ] Keep SAR-specific chirp/matched-filter parameters in `examples/SAR`.
- [ ] Add known-vector parity tests against `SarCpuReference`.
- [ ] Add graph/direct image metric deltas.

## Backprojection Evidence Hardening

- [ ] Keep `SarBackprojectionTransformNode` as the SAR adapter; do not add `SarBackprojectionTransformNodeMetal`.
- [ ] Verify adapter binds `DeviceKernelNodeMetal` in native-device mode.
- [ ] Verify native Metal backprojection parity against CPU adapter reference.
- [ ] Ensure benchmark trace reports:
  - [ ] native backend requested
  - [ ] resolved execution backend
  - [ ] native kernel bound
  - [ ] native kernel executed
  - [ ] kernel ticket backend
  - [ ] kernel ticket id
  - [ ] kernel ticket queue id
  - [ ] kernel ticket arg count

## Accel-Token Enforcement

- [ ] Definitive topology retains `edge_contract=accel-token`.
- [ ] Maintained SAR presets retain explicit resolver metadata.
- [ ] No legacy SAR payload contract appears on accel-token graph edges.
- [ ] H2D/kernel/D2H stages use accel token/view/ticket contracts.
- [ ] SAR metadata is preserved as sidecar/control-plane data, not as edge payload replacement.
- [ ] Tests continue to reject legacy payload contracts under accel-token mode.

## SAR Mathematical Correctness And CPU Reference Parity

- [ ] Point-target backprojection reference test remains green.
- [ ] Matched-filter known-vector test remains green.
- [ ] Runtime `RangeCompressionNode` matched-filter parity remains green.
- [ ] Native Metal backprojection adapter parity remains green when Metal runtime is available.
- [ ] Any newly Metalized algorithm stage has:
  - [ ] CPU reference result
  - [ ] tolerance policy
  - [ ] l_inf / rms / relative_l2 or image-metric comparison
  - [ ] deterministic fixture

## Performance And Graph-Overhead Attribution

- [ ] Benchmark compares definitive topology under at least two backend selections:
  - [ ] auto/stub or non-Metal baseline
  - [ ] Metal
- [ ] Benchmark reports:
  - [ ] graph build time
  - [ ] graph run time
  - [ ] graph lifecycle total time
  - [ ] direct baseline execute time
  - [ ] min/median/max/stddev
  - [ ] H2D bytes
  - [ ] D2H bytes
  - [ ] kernel dispatches
  - [ ] queue wait/backpressure
  - [ ] backend synchronization or proxy timing
  - [ ] resolved concrete node selections
- [ ] Trace schema retains:
  - [ ] `overhead_ms.graph_run_minus_baseline_median`
  - [ ] `overhead_attribution.cost_buckets.graph_overhead_ms`
  - [ ] `performance_claim_policy.speedup_basis`
  - [ ] `performance_claim_policy.disallow_lifecycle_total_as_speedup_basis`
- [ ] Performance report demonstrates improvement or includes bottleneck attribution.
- [ ] No acceleration claim is made from metadata-only backend tags.

## Documentation Updates

- [ ] Update `examples/SAR/README.md` with:
  - [ ] node mapping table
  - [ ] SAR-vs-libgpu ownership decisions
  - [ ] resolver mapping policy
  - [ ] no-duplicate-node rule
  - [ ] benchmark command and interpretation
- [ ] Update `plan/SAR.md` with PR10 status.
- [ ] Update this checklist as implementation progresses.

## Validation Commands

Run after implementation:

```bash
cmake --build build-ninja/ninja-debug --target test_libgraph_unit
./build-ninja/ninja-debug/libgraph/test/test_libgraph_unit '--gtest_filter=ResolvingNodeProviderTest.*:GraphConfigParserExpectedTest.*Resolver*'

cmake --build build-ninja/ninja-debug --target test_sar_example_unit
./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit

cmake --build build-ninja/ninja-debug --target sar_benchmark
./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci --range-stage=compression --native-backend
```

Optional performance comparison:

```bash
bash ./examples/SAR/tools/benchmark_main_metal_vs_nonmetal.sh
```

## Acceptance Criteria

- [ ] Definitive topology executes successfully through `GraphExecutorBuilder`.
- [ ] SAR runtime can use common `libgpu` Metal node implementations where equivalent nodes exist.
- [ ] SAR-specific adapter behavior remains in `examples/SAR`.
- [ ] No duplicate SAR-local Metal nodes are introduced.
- [ ] Resolver diagnostics prove actual backend selection.
- [ ] Accel-token guardrails remain green.
- [ ] CPU reference parity remains green.
- [ ] Benchmark evidence shows improvement or clearly attributes bottlenecks.
- [ ] Documentation explains residual gaps and next optimization candidates.

## Reviewer Checklist

- [ ] Is every Metal-equivalent node either generic `libgpu` or justified SAR-specific adapter?
- [ ] Are resolver mappings dynamic and SAR-owned where domain-specific?
- [ ] Are the definitive JSON node types still portable?
- [ ] Are sidecars preserved across transfer/kernel/merge boundaries?
- [ ] Is the performance claim based on graph run time versus baseline, not lifecycle total?
- [ ] Does the PR avoid duplicating `libgpu` nodes?
