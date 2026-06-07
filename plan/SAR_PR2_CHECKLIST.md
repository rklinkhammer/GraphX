# SAR PR2 Checklist

Status:

- [x] PR2 branch created
- [ ] PR2 implementation complete
- [ ] PR2 ready for review
- [ ] PR2 merged

## Scope (from plan/SAR.md)

- [x] Replace PR1 pulse-modulo tile assignment with real graph-visible fan-out branches.
- [x] Add a DSP-significant RangeWindow or deterministic RangeCompression stage.
- [x] Start unifying SAR backend metadata with existing `graph::gpu::accel` tickets/leases/views.
- [x] Preserve example-local SAR package boundary; do not add SAR-specific libgpu node families.
- [ ] Evaluate DeviceReduceNode accumulation showcase.
- [x] Add optional trace export and deeper queue/backpressure diagnostics.
- [x] Keep benchmark reporting split between graph run time and lifecycle teardown/join time.

## Phase A - True Tile Fan-Out

- [x] Document current PR1 tile semantics (`tile_id = sequence_id % tile_count`) as deterministic but not true fan-out.
- [ ] Define PR2 tile identity fields: `batch_id`, `aperture_id`, `tile_id`, `tile_count`, `pulse_range`.
- [x] Change or extend `AzimuthTileSplitNode` so one pulse/aperture block can produce N independent tiles.
- [x] Add a JSON topology that exposes branch-level parallelism:
  - `split -> h2d_tile0 -> bp_tile0 -> d2h_tile0 -> merge`
  - `split -> h2d_tile1 -> bp_tile1 -> d2h_tile1 -> merge`
  - more branches as supported by CI profile.
- [x] Add tests for true fan-out count, unique tile IDs, out-of-order branch completion, and merge correctness.

Implementation notes:

- `SarPulseFanoutNode` uses the existing GraphX `SplitNode4` primitive and is dynamically loaded as an example-scoped SAR plugin.
- `sar_stripmap_pr2_fanout.json` now exposes four branch lanes: `fanout -> split_tileN -> h2d_tileN -> bp_tileN -> d2h_tileN -> merge`.
- `AzimuthTileSplitNode` now supports `fixed_tile_id` for branch-specific tile identity; default sequence-modulo behavior remains available for PR1 compatibility.
- `ImageTileMergeNode` now has an opt-in `require_all_tile_eos_before_complete` policy so branch fan-in does not complete on the first branch EOS.

Exit criteria:

- [x] JSON topology visibly contains multiple tile branches.
- [x] `ImageTileMergeNode` receives independently produced branch outputs, not only pulse-modulo tile IDs.
- [x] Baseline and graph outputs match within explicit tolerance for PR2 fan-out dataset.
- [x] CI-safe profile remains deterministic and stable.

## Phase B - DSP-Significant Stage

- [x] Choose `RangeWindowNode` or deterministic direct `RangeCompressionNode` for PR2.
- [x] Prefer direct matched filtering first; defer FFT-backed implementation unless reuse from `libdsp` is low-risk.
- [x] Define deterministic reference dataset and tolerance envelope.
- [x] Add/update unit tests validating numeric stability and tolerances.
- [x] Update graph and non-graph baseline to include the same DSP stage.

Exit criteria:

- [x] PR2 pipeline has a meaningful DSP stage before backprojection.
- [x] Graph and baseline both execute the same DSP math path.
- [x] CI-safe profile remains deterministic and stable.

## Phase C - GPU Contract Alignment

- [x] Audit existing `graph::gpu::accel` types:
  - `BackendKind`
  - `BufferLease`
  - `DeviceBufferView`
  - `HostPinnedBufferView`
  - `TransferTicket`
  - `KernelTicket`
- [x] Decide which SAR message fields should wrap/reference existing GPU contracts.
- [x] Avoid expanding parallel SAR-only backend abstractions unless needed for example compatibility.
- [x] Update SAR messages or adapters so range/image tile messages can carry real GPU lease/view/ticket metadata.
- [x] Add tests that validate metadata propagation across H2D, transform, D2H, and merge.

Implementation notes:

- `SarGpuMetadata` now wraps `graph::gpu::accel::BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, and `KernelTicket`.
- `SarRangeTileMessage`, `SarImageTileMessage`, and `SarMergeStatusMessage` carry optional accel metadata.
- `H2DAsyncNode` creates simulated accel host/device views, a lease, and H2D transfer ticket for device-like SAR backends.
- `SarBackprojectionTransformNode` preserves buffer metadata and attaches a kernel ticket.
- `D2HAsyncNode` emits host-view and D2H transfer ticket metadata.
- `ImageTileMergeNode` preserves the latest tile accel metadata in merge status output.
- Existing `SarBackendKind`, `SarBufferDescriptor`, and `SarDispatchMetadata` are retained for PR1/PR2 compatibility and JSON stability; PR3 can replace or further narrow them once native backend payloads are active.

Exit criteria:

- [x] Decision recorded for each SAR backend metadata type: keep, wrap existing GPU type, or replace later.
- [x] PR2 SAR messages can interoperate with the existing GPU capability model at the metadata boundary.

## Phase D - Trace Export and Diagnostics Depth

- [x] Add optional trace export toggle (off by default).
- [x] Emit stage-level timing and transfer counters into trace output.
- [x] Expand queue/backpressure diagnostics depth (without brittle thresholds).
- [ ] Add DAG metrics for branch fan-in wait time, queue high-water marks, out-of-order completion count, and duplicate/missing tile detection.
- [x] Keep graph run time separate from lifecycle total/init/start/stop/join timing in benchmark output.
- [x] Investigate high join teardown latency as lifecycle overhead, not SAR compute overhead.
- [ ] Add tests for trace schema presence and diagnostics fields.

Implementation notes:

- `sar_benchmark` now accepts `--trace-out=<path>` / `--trace <path>`; trace export remains off by default.
- Trace schema `graphx.sar.benchmark.trace.v1` records profile metadata, timing summaries, last lifecycle phase timings, diagnostics counters, queue counters, and overhead proxies.
- Benchmark output and trace both keep graph run time separate from init/start/stop/join lifecycle timing; join remains labeled lifecycle teardown rather than SAR compute.

Exit criteria:

- [x] Trace export generated when enabled.
- [x] Diagnostics fields present and deterministic enough for CI assertions.
- [x] Benchmark docs clearly distinguish graph run overhead from lifecycle teardown overhead.

## Phase E - DeviceReduceNode Evaluation

- [ ] Prototype DeviceReduceNode accumulation path in SAR flow (feature-gated).
- [ ] Compare behavior/perf against current merge/accumulation path.
- [ ] Determine whether tile-shard accumulation belongs in DeviceReduce, ImageTileMerge, or a staged combination.
- [ ] Document keep/defer decision with rationale.

Exit criteria:

- [ ] Decision recorded with evidence (keep in PR2 or defer to PR3).

## Validation Matrix

- [x] Build: SAR example + SAR tests + benchmark target.
- [x] Run: focused SAR unit/integration tests.
- [x] Run: graph-vs-baseline tolerance comparisons for PR2 dataset.
- [x] Run: graph-visible fan-out JSON topology validation.
- [x] Run: GPU metadata propagation tests.
- [x] Run: trace-enabled smoke validation.
- [x] Run: `sar_benchmark --profile=ci` and confirm run/lifecycle timing split.

## PR2 Deliverables

- [x] Code + tests for true fan-out and DSP-stage improvements.
- [x] GPU contract alignment notes and message metadata changes.
- [x] Documentation updates in examples/SAR/README.md.
- [x] Benchmark/trace notes updated.
- [ ] Plan updates noting what remains PR3: native backend transforms, FFT-backed compression, multi-device routing, real transfer/kernel timing.
- [ ] PR body includes risk, rollout, and evidence summary.
