# SAR PR2 Checklist

Status:

- [x] PR2 branch created
- [ ] PR2 implementation complete
- [ ] PR2 ready for review
- [ ] PR2 merged

## Scope (from plan/SAR.md)

- [ ] Replace PR1 pulse-modulo tile assignment with real graph-visible fan-out branches.
- [ ] Add a DSP-significant RangeWindow or deterministic RangeCompression stage.
- [ ] Start unifying SAR backend metadata with existing `graph::gpu::accel` tickets/leases/views.
- [ ] Preserve example-local SAR package boundary; do not add SAR-specific libgpu node families.
- [ ] Evaluate DeviceReduceNode accumulation showcase.
- [ ] Add optional trace export and deeper queue/backpressure diagnostics.
- [ ] Keep benchmark reporting split between graph run time and lifecycle teardown/join time.

## Phase A - True Tile Fan-Out

- [ ] Document current PR1 tile semantics (`tile_id = sequence_id % tile_count`) as deterministic but not true fan-out.
- [ ] Define PR2 tile identity fields: `batch_id`, `aperture_id`, `tile_id`, `tile_count`, `pulse_range`.
- [ ] Change or extend `AzimuthTileSplitNode` so one pulse/aperture block can produce N independent tiles.
- [ ] Add a JSON topology that exposes branch-level parallelism:
  - `split -> h2d_tile0 -> bp_tile0 -> d2h_tile0 -> merge`
  - `split -> h2d_tile1 -> bp_tile1 -> d2h_tile1 -> merge`
  - more branches as supported by CI profile.
- [ ] Add tests for true fan-out count, unique tile IDs, out-of-order branch completion, and merge correctness.

Exit criteria:

- [ ] JSON topology visibly contains multiple tile branches.
- [ ] `ImageTileMergeNode` receives independently produced branch outputs, not only pulse-modulo tile IDs.
- [ ] Baseline and graph outputs match within explicit tolerance for PR2 fan-out dataset.
- [ ] CI-safe profile remains deterministic and stable.

## Phase B - DSP-Significant Stage

- [ ] Choose `RangeWindowNode` or deterministic direct `RangeCompressionNode` for PR2.
- [ ] Prefer direct matched filtering first; defer FFT-backed implementation unless reuse from `libdsp` is low-risk.
- [ ] Define deterministic reference dataset and tolerance envelope.
- [ ] Add/update unit tests validating numeric stability and tolerances.
- [ ] Update graph and non-graph baseline to include the same DSP stage.

Exit criteria:

- [ ] PR2 pipeline has a meaningful DSP stage before backprojection.
- [ ] Graph and baseline both execute the same DSP math path.
- [ ] CI-safe profile remains deterministic and stable.

## Phase C - GPU Contract Alignment

- [ ] Audit existing `graph::gpu::accel` types:
  - `BackendKind`
  - `BufferLease`
  - `DeviceBufferView`
  - `HostPinnedBufferView`
  - `TransferTicket`
  - `KernelTicket`
- [ ] Decide which SAR message fields should wrap/reference existing GPU contracts.
- [ ] Avoid expanding parallel SAR-only backend abstractions unless needed for example compatibility.
- [ ] Update SAR messages or adapters so range/image tile messages can carry real GPU lease/view/ticket metadata.
- [ ] Add tests that validate metadata propagation across H2D, transform, D2H, and merge.

Exit criteria:

- [ ] Decision recorded for each SAR backend metadata type: keep, wrap existing GPU type, or replace later.
- [ ] PR2 SAR messages can interoperate with the existing GPU capability model at the metadata boundary.

## Phase D - Trace Export and Diagnostics Depth

- [ ] Add optional trace export toggle (off by default).
- [ ] Emit stage-level timing and transfer counters into trace output.
- [ ] Expand queue/backpressure diagnostics depth (without brittle thresholds).
- [ ] Add DAG metrics for branch fan-in wait time, queue high-water marks, out-of-order completion count, and duplicate/missing tile detection.
- [ ] Keep graph run time separate from lifecycle total/init/start/stop/join timing in benchmark output.
- [ ] Investigate high join teardown latency as lifecycle overhead, not SAR compute overhead.
- [ ] Add tests for trace schema presence and diagnostics fields.

Exit criteria:

- [ ] Trace export generated when enabled.
- [ ] Diagnostics fields present and deterministic enough for CI assertions.
- [ ] Benchmark docs clearly distinguish graph run overhead from lifecycle teardown overhead.

## Phase E - DeviceReduceNode Evaluation

- [ ] Prototype DeviceReduceNode accumulation path in SAR flow (feature-gated).
- [ ] Compare behavior/perf against current merge/accumulation path.
- [ ] Determine whether tile-shard accumulation belongs in DeviceReduce, ImageTileMerge, or a staged combination.
- [ ] Document keep/defer decision with rationale.

Exit criteria:

- [ ] Decision recorded with evidence (keep in PR2 or defer to PR3).

## Validation Matrix

- [ ] Build: SAR example + SAR tests + benchmark target.
- [ ] Run: focused SAR unit/integration tests.
- [ ] Run: graph-vs-baseline tolerance comparisons for PR2 dataset.
- [ ] Run: graph-visible fan-out JSON topology validation.
- [ ] Run: GPU metadata propagation tests.
- [ ] Run: trace-enabled smoke validation.
- [ ] Run: `sar_benchmark --profile=ci` and confirm run/lifecycle timing split.

## PR2 Deliverables

- [ ] Code + tests for true fan-out and DSP-stage improvements.
- [ ] GPU contract alignment notes and message metadata changes.
- [ ] Documentation updates in examples/SAR/README.md.
- [ ] Benchmark/trace notes updated.
- [ ] Plan updates noting what remains PR3: native backend transforms, FFT-backed compression, multi-device routing, real transfer/kernel timing.
- [ ] PR body includes risk, rollout, and evidence summary.
