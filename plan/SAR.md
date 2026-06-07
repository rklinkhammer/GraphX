# GraphX SAR Example Implementation Plan (from example3 prompt)

## Grounding in Existing Repository Patterns

Recommendations are grounded in the following existing tests/patterns:

1. [libgraph/test/unit/test_sdr_graph.cpp](../libgraph/test/unit/test_sdr_graph.cpp): SDR graph execution and plugin-driven node composition.
2. [libgraph/test/unit/test_json_dynamic_graph_loader.cpp](../libgraph/test/unit/test_json_dynamic_graph_loader.cpp): JSON topology loading path and validation expectations.
3. [libgraph/test/unit/test_plugin_system.cpp](../libgraph/test/unit/test_plugin_system.cpp): plugin/provider contracts and bootstrap boundaries.
4. [libgpu/test/unit/test_gpu_topologies.cpp](../libgpu/test/unit/test_gpu_topologies.cpp): async H2D/device/D2H topology patterns.
5. [libgpu/test/runtime/test_metal_runtime_graph_pipeline.cpp](../libgpu/test/runtime/test_metal_runtime_graph_pipeline.cpp): runtime GPU-style pipeline staging/flow.

This plan intentionally reuses existing GraphX patterns rather than introducing framework-wide abstractions.

---

## Current Implementation Notes

The current SAR example has a working CI-safe benchmark and JSON runtime path. Recent benchmark analysis clarified two important operational details:

1. SAR plugins should be loaded from an example-scoped plugin directory, not the global build plugin directory. Loading from the global directory can register generic names such as `H2DAsyncNode` and `D2HAsyncNode` more than once when CUDA/GPU and SAR plugins coexist.
2. Benchmark output must separate graph run time from graph lifecycle teardown. The full `Execute()` lifecycle includes `Join()` teardown, which can dominate the reported time and obscure the actual SAR graph run-loop overhead.

Current benchmark interpretation:

- Graph build time measures provider/bootstrap/plugin lookup and JSON graph construction.
- Graph run time is the comparable GraphX execution metric against the direct non-graph baseline.
- Graph lifecycle total time includes init/start/run/stop/join and should be reported separately.
- Join teardown time is currently a notable follow-up area, not SAR algorithm compute time.

These findings should be treated as PR1/PR2 planning constraints: benchmark reports must not attribute lifecycle teardown to SAR algorithm or graph scheduling overhead without labeling it separately.

Implementation review also confirms the current PR1 vertical slice is credible and aligned with the intended `examples/SAR` boundary:

```text
SyntheticApertureIqSource
-> AzimuthTileSplit
-> H2DAsync
-> SarBackprojectionTransform
-> D2HAsync
-> ImageTileMerge
-> SarDiagnosticsSink
```

Strong PR1 choices already present:

1. SAR-specific implementation is isolated under `examples/SAR`.
2. SAR messages carry explicit metadata instead of assuming graph edges move bytes.
3. Local SAR H2D/D2H nodes provide a CI-safe simulated async-transfer lane.
4. `ImageTileMergeNode` is correctly treated as the key fan-in/correctness node.
5. The benchmark compares GraphX against a direct non-graph baseline and reports repeated-run statistics.

Main PR2 design concern: current tile semantics are deterministic but symbolic. `AzimuthTileSplitNode` assigns tile IDs using pulse modulo behavior, which distributes pulses across tile IDs but does not yet create true graph-visible fan-out. PR2 should evolve toward one pulse/aperture block producing multiple independent range/azimuth/image tiles.

---

## 1) Recommended First PR Scope

PR1 objective: architecture-correct, deterministic, CI-stable SAR vertical slice under `examples/SAR`, not full SAR mathematical fidelity.

### PR1 inclusions

1. Add a new example package at `examples/SAR` with JSON-driven topology as the primary demo path.
2. Add deterministic synthetic stripmap SAR pipeline with fixed seed and bounded packet/tile counts.
3. Add no more than 4 new SAR nodes:
   - `SyntheticApertureIqSourceNode` (new)
   - `AzimuthTileSplitNode` (new)
   - `SarBackprojectionTransformNode` (new, example-local wrapper over generic device transform semantics)
   - `ImageTileMergeNode` (new)
4. Reuse existing GPU async transfer/device node patterns (H2D/device transform/D2H style) where available.
5. Add correctness + diagnostics tests and a non-graph baseline comparison harness for overhead attribution.
6. Isolate SAR plugins from the global plugin directory to avoid type-name collisions with GPU/test plugins.

PR1 currently also includes example-local simulated H2D/D2H and diagnostics/visualization support. Those are acceptable as example-package implementation details, but they should not become SAR-specific libgpu abstractions.

### PR1 explicit non-goals

1. Full-fidelity SAR physics (motion compensation/autofocus/radiometric calibration).
2. Framework-wide scheduler or architecture rewrites.
3. New libgpu SAR-specific global abstractions.
4. Dynamic multi-device load balancing.

---

## 2) Proposed File Layout

All example-specific files remain under `examples/SAR`.

```text
examples/SAR/
  CMakeLists.txt
  README.md
  config/
    sar_stripmap_pr1.json
  include/sar/
      SyntheticApertureIqSourceNode.hpp
      AzimuthTileSplitNode.hpp
      SarBackprojectionTransformNode.hpp
      ImageTileMergeNode.hpp
      SarMessages.hpp
  src/
    main.cpp
    SyntheticApertureIqSourceNode.cpp
    AzimuthTileSplitNode.cpp
    SarBackprojectionTransformNode.cpp
    ImageTileMergeNode.cpp
  test/
      test_synthetic_aperture_iq_source_node.cpp
      test_azimuth_tile_split_node.cpp
      test_sar_backprojection_transform_node.cpp
      test_image_tile_merge_node.cpp
      test_sar_json_pipeline.cpp
      test_sar_baseline_compare.cpp
      test_sar_diagnostics_contract.cpp
```

Potential reusable extraction (deferred unless justified): tiny generic metadata helpers in libgraph/libgpu only if reuse is demonstrated by more than SAR.

---

## 3) Node List and Responsibilities (PR1 cap: 4 new nodes)

| Node | Reused/New | Proposed Path | Message Contract (in/out) | PR1 Tests | Deferred Follow-up |
| --- | --- | --- | --- | --- | --- |
| SyntheticApertureIqSourceNode | New | examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp | out: `SarPulseBlockMessage` with deterministic sequence ids, pulse range, geometry/meta, EOS | deterministic output, EOS correctness, fixed counts | richer scene models/noise/motion error |
| RangeWindow/RangeCompression stage | Reuse existing DSP pattern or example-local deterministic placeholder | wired from existing GraphX DSP style | in: `SarPulseBlockMessage`; out: `SarRangeTileMessage` | sample count invariants, deterministic transform output | real matched filter fidelity and accelerated FFT |
| AzimuthTileSplitNode | New | examples/SAR/include/sar/AzimuthTileSplitNode.hpp | in: `SarRangeTileMessage`; out: independent `SarRangeTileMessage` branches tagged by tile ids | tile fan-out count, metadata completeness | adaptive tiling/scheduling |
| H2D async transfer | Reused | existing GPU async transfer pattern | in: host tile msg + lease metadata; out: transfer ticket + device-ready tile | bytes moved/transfer ticket counters | backend-specific tuning |
| SarBackprojectionTransformNode | New | examples/SAR/include/sar/SarBackprojectionTransformNode.hpp | in: device tile + kernel descriptor meta; out: device image tile | dispatch count, deterministic tile output | native backend kernels |
| D2H async transfer | Reused | existing GPU async transfer pattern | in: device tile; out: host `SarImageTileMessage` + completion metadata | D2H bytes counters | overlap/stream tuning |
| ImageTileMergeNode | New | examples/SAR/include/sar/ImageTileMergeNode.hpp | in: `SarImageTileMessage` + EOS/watermark; out: final merged image + `SarMergeStatusMessage` | duplicate/missing/out-of-order/EOS matrix | partial preview/sliding aperture |
| Detection/Metrics sink | Reuse existing sink pattern or small example sink | examples/SAR/src/main.cpp wiring | in: merged image + diagnostics bundle | metrics presence/tolerance checks | richer observability/export |

Why SAR differs from vibration-health pipeline: SAR emphasizes tile independence, explicit async transfer boundaries, fan-in merge correctness under out-of-order completion, and graph-vs-baseline overhead attribution for image formation stages.

PR2 correction to the node model: make tile fan-out real at the graph level. The current `AzimuthTileSplitNode` tile assignment is deterministic and useful for PR1, but PR2 should move toward one pulse block or aperture block emitting N independent tiles that can traverse distinct graph branches.

---

## 4) Message/Buffer Types Needed

Define message contracts in `examples/SAR/include/sar/SarMessages.hpp`.

1. `SarPulseBlockMessage`
   - `sequence_id`, `pulse_start`, `pulse_count`, `samples_per_pulse`, `sample_rate_hz`, `carrier_hz`, `chirp_meta`, `platform_geometry_meta`, `eos`.
2. `SarRangeTileMessage`
   - `tile_id`, `range_block_id`, `azimuth_block_id`, `pulse_range`, `batch_id`, `expected_tile_count`, `timestamp_ns`, `backend`, `device_id`, `queue_id`, payload buffer/view.
3. `SarDeviceLeaseMessage`
   - `lease_id`, `backend`, `device_id`, `queue_id`, `bytes`, lifecycle state label.
4. `SarTransferTicketMessage`
   - `ticket_id`, `transfer_direction`, `bytes`, `enqueue_ts_ns`, `complete_ts_ns`, `backend`, `device_id`, `queue_id`.
5. `SarImageTileMessage`
   - `tile_id`, `image_tile_dims`, `pulse_range`, `sequence_id`, `batch_id`, completion flag + payload.
6. `SarMergeStatusMessage`
   - `expected_tiles`, `received_tiles`, `duplicate_tiles`, `missing_tiles`, `out_of_order_count`, `watermark_seen`, `merge_complete`.
7. `SarDiagnosticsMessage`
   - `pulses_processed`, `tiles_processed`, `bytes_h2d`, `bytes_d2h`, `kernel_dispatches`, `fanin_wait_ms`, `e2e_latency_ms`, queue/backpressure counters where available.

Buffer lifetime labels (PR1 diagnostics-level, not framework state machine):
`Allocated -> HostFilled -> ReadyForTransfer -> TransferInFlight -> DeviceReady -> KernelRunning -> KernelComplete -> TransferBack -> HostReady -> Consumed -> Released`.

PR2 message-contract direction:

1. Unify SAR backend metadata with existing `graph::gpu::accel` contracts where possible.
2. Prefer wrapping or referencing existing GPU types such as `BackendKind`, `BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, and `KernelTicket`.
3. Avoid growing parallel SAR-only equivalents (`SarBackendKind`, `SarBufferDescriptor`, `SarTransferTicketMessage`, `SarDispatchMetadata`) beyond what is needed for PR1 compatibility.
4. Add better tile identity: `batch_id`, `aperture_id`, `tile_id`, `tile_count`, `pulse_range`, and backend/device/queue metadata.

---

## 5) JSON Topology Shape (Primary Demo Path)

```json
{
  "name": "sar_stripmap_pr1",
  "num_threads": 4,
  "nodes": [
    {"id": "src",   "type": "SyntheticApertureIqSourceNode", "node_config": {"seed": 1337, "pulse_count": 64, "samples_per_pulse": 1024}},
    {"id": "split", "type": "AzimuthTileSplitNode", "node_config": {"tile_count": 4}},
    {"id": "h2d",   "type": "H2DAsyncNode"},
    {"id": "bp",    "type": "SarBackprojectionTransformNode", "node_config": {"tile_w": 128, "tile_h": 128, "simulated": true}},
    {"id": "d2h",   "type": "D2HAsyncNode"},
    {"id": "merge", "type": "ImageTileMergeNode", "node_config": {"expected_tiles": 4}},
    {"id": "sink",  "type": "SarDiagnosticsSinkNode"}
  ],
  "edges": [
    {"source_node_id": "src",   "source_port": 0, "target_node_id": "split", "target_port": 0},
    {"source_node_id": "split", "source_port": 0, "target_node_id": "h2d",   "target_port": 0},
    {"source_node_id": "h2d",   "source_port": 0, "target_node_id": "bp",    "target_port": 0},
    {"source_node_id": "bp",    "source_port": 0, "target_node_id": "d2h",   "target_port": 0},
    {"source_node_id": "d2h",   "source_port": 0, "target_node_id": "merge", "target_port": 0},
    {"source_node_id": "merge", "source_port": 0, "target_node_id": "sink",  "target_port": 0}
  ]
}
```

Programmatic graph construction is allowed only for focused tests/helpers.

PR2 topology direction: make fan-out/fan-in visible in JSON rather than only in tile metadata.

```text
split -> h2d_tile0 -> bp_tile0 -> d2h_tile0 \
split -> h2d_tile1 -> bp_tile1 -> d2h_tile1 -> merge
split -> h2d_tile2 -> bp_tile2 -> d2h_tile2 /
```

This should demonstrate executor-level branch parallelism and give `ImageTileMergeNode` real out-of-order fan-in behavior to validate.

---

## 6) Execution and Diagnostics Flow

1. Source emits deterministic pulse blocks with sequence ids and explicit EOS.
2. Optional deterministic range window/compression stage normalizes sample shape.
3. `AzimuthTileSplitNode` exposes CPU-visible DAG width via independent tile branches.
4. H2D transfer stage transitions host buffer ownership to device lease/ticket semantics.
5. `SarBackprojectionTransformNode` runs per-tile work unit (simulated deterministic kernel in PR1).
6. D2H returns host image tiles with completion metadata.
7. `ImageTileMergeNode` validates tile completeness/uniqueness/watermark behavior.
8. Metrics sink emits deterministic counters and latency figures.

Required diagnostics in PR1:

- pulses processed
- bytes H2D / D2H
- tiles processed
- kernel dispatches
- fan-in wait time
- end-to-end latency
- duplicate/missing tile counts
- queue depth/high-water/backpressure where existing instrumentation permits

---

## 7) Unit and Integration Tests to Add

### Unit tests

1. `test_sar_diagnostics_contract.cpp`
   - Verifies deterministic metrics presence and value tolerances.
2. `test_image_tile_merge.cpp`
   - Correct merge
   - Duplicate tile handling
   - Missing tile handling
   - Out-of-order completion
   - EOS/watermark gating
3. Node-level determinism tests for source and split semantics.

### Integration tests

1. `test_sar_json_pipeline.cpp`
   - JSON-loaded topology path under provider/bootstrap contracts.
2. `test_sar_baseline_compare.cpp`
   - Graph vs non-graph baseline consume identical deterministic input and compare output tolerance.
3. Simulated backend CI-safe run (no native GPU dependency).

Test strategy aligns with patterns used in:

- [libgraph/test/unit/test_sdr_graph.cpp](../libgraph/test/unit/test_sdr_graph.cpp)
- [libgraph/test/unit/test_json_dynamic_graph_loader.cpp](../libgraph/test/unit/test_json_dynamic_graph_loader.cpp)
- [libgpu/test/unit/test_gpu_topologies.cpp](../libgpu/test/unit/test_gpu_topologies.cpp)

---

## 8) Build/CMake/Plugin Registration Changes

1. Add `examples/SAR/CMakeLists.txt` and optional top-level flag:
   - `GRAPHX_BUILD_EXAMPLES_SAR` (default ON locally, configurable for CI/minimal builds).
2. Build example executable + test targets under `examples/SAR`.
3. Register SAR example nodes through existing plugin/provider bootstrap flow.
4. Keep graph construction code dependent on provider interfaces, not direct plugin loader details.
5. No framework-wide CMake rewrites in PR1.
6. SAR plugin output should be example-scoped, for example `${CMAKE_BINARY_DIR}/examples/SAR/plugins`.
7. SAR executables and tests should use the example-scoped SAR plugin directory by default.

---

## 9) Risks and Staged Follow-up PRs

### PR1 risks

1. Merge correctness under out-of-order arrivals.
2. Metadata contract drift across stages.
3. CI flakiness from timing-sensitive assertions.

### PR2

1. Replace symbolic pulse-modulo tile assignment with real graph-visible fan-out branches.
2. Add a DSP-significant stage: prefer `RangeWindowNode` or deterministic direct `RangeCompressionNode` before backprojection.
3. Start unifying SAR messages with existing `graph::gpu::accel` tickets, leases, and views.
4. Evaluate DeviceReduceNode accumulation showcase.
5. Add optional trace export and deeper queue/backpressure diagnostics.
6. Investigate graph lifecycle teardown/join latency separately from SAR algorithm runtime.

### PR3

1. Native backend kernel path (CUDA/SYCL/Metal as available).
2. FFT-backed range compression using libdsp or backend FFT libraries where appropriate.
3. Improve overlap and transfer/kernel pipelining.
4. Add real transfer/kernel timing for native backends.

PR2 closeout update:

1. PR2 now carries explicit tile identity metadata (`batch_id`, `aperture_id`, `pulse_range_start`, `pulse_range_count`, `tile_id`, `tile_count`) through source/split/transform/merge diagnostics boundaries.
2. Trace schema validation is now covered by automated tests (`SarTraceSchemaTest`) that execute `sar_benchmark --trace-out` and verify required diagnostics and queue fields.
3. DeviceReduce accumulation was evaluated via feature-gated benchmark mode (`--evaluate-device-reduce`), with current CI evidence recommending defer-to-PR3 due insufficient deterministic speedup despite diagnostics parity.

### PR4/PR5

1. Dynamic load balancing/work stealing.
2. Multi-device/heterogeneous routing policy.

---

## 10) Clear Acceptance Criteria

1. `examples/SAR` builds and runs in CI-safe profile.
2. JSON topology is the primary demonstration path.
3. PR1 adds no more than 4 new SAR nodes.
4. Deterministic synthetic data yields deterministic packet/tile counts and stable diagnostics.
5. `ImageTileMergeNode` passes duplicate/missing/out-of-order/EOS correctness matrix.
6. Graph and non-graph baseline outputs match within explicit tolerance.
7. Required metrics are produced for DSP/GPU/DAG categories.
8. Simulated backend path works without native GPU.

---

## 11) External-Review Relevance Table (17 items)

| # | Topic | Classification | PR1 handling |
| --- | --- | --- | --- |
| 1 | Control plane vs data plane | PR1 requirement | Edges carry tokens/metadata; transfer nodes/capabilities perform byte movement |
| 2 | Sequence and watermark contracts | PR1 requirement | Add sequence/tile/watermark metadata and merge validations |
| 3 | Explicit end-of-stream | PR1 requirement | Use explicit EOS/completion messages |
| 4 | Backpressure diagnostics | PR1 decision | Collect where available; defer full percentile suite if not already exposed |
| 5 | Thread ownership model | PR1 requirement | No node-owned pools; use graph branch width + executor workers |
| 6 | Backend-neutral dispatch metadata | PR1 requirement | Emit backend/device/queue/dispatch metadata in diagnostics |
| 7 | Buffer lifetime state | PR1 decision | Track lifecycle labels for diagnostics, no framework state machine |
| 8 | Lease reuse metrics | Deferred | Add hooks if available; full accounting in PR2 |
| 9 | Execution trace recording | Deferred | Optional lightweight CSV/JSON trace in PR2 |
| 10 | Error/status contract | PR1 decision | Reuse existing GraphX status/result forms before adding new contract |
| 11 | Deterministic scheduler/debug mode | PR1 decision | Fixed synthetic data + deterministic profile; no scheduler rewrite |
| 12 | Graph overhead attribution | PR1 requirement | Mandatory graph vs baseline timing and overhead categories |
| 13 | Tile merge correctness | PR1 requirement | Full merge correctness matrix required |
| 14 | Simulated device backend | PR1 requirement | CI-safe simulated/stub lane required |
| 15 | Multi-device future metadata | PR1 decision | Include cheap metadata fields now; scheduling deferred |
| 16 | SAR as architecture demonstration | PR1 requirement | Prioritize fan-out/fan-in, ownership, async boundaries, diagnostics |
| 17 | DAG event/ticket model | PR1 decision | Reuse existing transfer/completion ticket concepts; broaden later |

No item is rejected; deferred items are outside reviewable PR1 scope.

---

## 12) Graph vs Non-Graph Benchmark Plan (Overhead Attribution)

### Baseline parity requirements

1. Identical synthetic seed, scene, pulse count, sample count, and tile size.
2. Same algorithmic stage sequence and backend mode.
3. Same output tolerance check for final image/tile data.

### Metrics to report

1. End-to-end wall time.
2. Graph build time.
3. Graph run time.
4. Graph lifecycle total time with init/start/run/stop/join split.
5. Stage-level timings (source, compression/window/FFT if present, transfer, kernel, merge).
6. Throughput (samples/s, pulses/s, tiles/s).
7. Transfer totals and effective bandwidth.

### Overhead attribution categories

1. Graph scheduling overhead.
2. Message allocation/copy overhead.
3. Queue wait/backpressure overhead.
4. Provider/plugin lookup overhead.
5. Diagnostics collection overhead.
6. Backend synchronization overhead.
7. Lifecycle teardown/join overhead (reported separately from graph run overhead).

### Measurement methodology

1. Warm-up runs + repeated measured runs.
2. Report median, min, max, and stddev (or CI-stable compact summary if needed).
3. Two profiles:
   - CI-safe small profile (fast, deterministic)
   - local larger profile (more representative)
4. CI gates focus on correctness and metric presence; performance thresholds remain conservative.

---

## 13) DAG Layering Plan

1. DSP DAG
   - synthetic pulse generation, optional deterministic range preprocessing.
2. Tile DAG
   - explicit independent tile decomposition with tile ids and expected counts.
3. Transfer DAG
   - H2D and D2H boundaries carrying lease/ticket metadata.
4. Kernel DAG
   - per-image-tile backprojection work units with dispatch metadata.
5. Reduction/Merge DAG
   - fan-in correctness checks and final merged output emission.

This separation keeps DSP concerns independent from device execution concerns.

---

## 14) ImageTileMerge Correctness Plan and Test Matrix

`ImageTileMergeNode` is a core PR1 demonstration node.

### Correctness requirements

1. Validate expected tile count before final emit.
2. Require unique tile ids.
3. Detect/record duplicates and missing tiles.
4. Support out-of-order tile completion.
5. Honor EOS/watermark semantics.
6. Track fan-in wait behavior for diagnostics.

### Test matrix

1. Happy path: all expected tiles once, any order, EOS arrives -> emit final result.
2. Duplicate tile path: duplicate id detected -> counted and rejected from double-accumulation.
3. Missing tile path: EOS before full set -> explicit failure status/diagnostic.
4. Out-of-order path: random order -> deterministic correct final merge.
5. Late tile path: tile after completion policy -> ignore or report according to policy.

---

## 15) Long-Term Heterogeneous Execution Roadmap

### PR2-PR3 evolution

1. Increase SAR fidelity (better matched filter/backprojection internals).
2. Add native backend implementations (CUDA/SYCL/Metal) behind same metadata contracts.
3. Optionally integrate DeviceReduce-based accumulation.

### PR4-PR5 evolution

1. Multi-backend tile routing.
2. Dynamic load balancing/work stealing.
3. Multi-device NUMA-aware scheduling and richer utilization metrics.

### Why deferred beyond PR1

1. Reviewability and CI stability: PR1 must stay small and deterministic.
2. Architectural safety: preserve contracts now; optimize scheduling/routing later.
3. Existing repository fit: avoid introducing broad abstractions before proving example value.

---

## Architectural Decision Log

| Decision | Selected option | Rejected alternatives | Reason | Follow-up risk |
| --- | --- | --- | --- | --- |
| PR1 SAR mode | Stripmap synthetic scenario | Spotlight-first | simpler deterministic geometry for CI | less algorithm breadth initially |
| Primary work granularity | Image tile as kernel work unit | global mutable image object | best fit for fan-out/fan-in and async device stages | merge complexity |
| PR1 tile semantics | deterministic pulse-modulo tile IDs | random/stochastic assignment | stable diagnostics and CI behavior | PR2 must add true fan-out |
| Parallelization boundary | Graph branch parallelism | node-owned internal pools | aligns with GraphX model and testability | current PR1 topology is still mostly linear |
| GPU boundary strategy | Reuse existing H2D/device/D2H style | new SAR-specific libgpu node family | minimizes framework churn in PR1 | later extraction may be needed |
| Device stage representation | Example-local backprojection transform wrapper | immediate libgpu core extension | keeps SAR-specific semantics in example boundary | duplicate logic risk until generalized |
| GPU metadata direction | wrap/reference `graph::gpu::accel` contracts | maintain parallel SAR-only backend model | avoids duplicate backend abstractions | migration from PR1 message fields |
| PR2 DSP stage | deterministic RangeWindow/RangeCompression | jump directly to full FFT SAR | adds meaningful DSP without destabilizing CI | FFT acceleration deferred |
| Build integration | examples/SAR behind option, on by default locally | test-only fixture | preserves package boundary and publishable example shape | CI matrix growth |
| Benchmark scope | graph vs non-graph mandatory | graph-only timing | required for overhead attribution | baseline maintenance burden |
| Determinism strategy | fixed synthetic seeds/counts + CI-safe profile | performance-only stochastic runs | stable CI and reproducible diagnostics | may under-represent real-world variance |

---

## Algorithm and Measurement References

1. Remote Sensing (2022): review of SAR image formation algorithms and implementations.
2. DLR/IEEE GRSS SAR tutorial material for SAR fundamentals.
3. Backprojection references (Sandia-style image-formation and filtered backprojection sources).
4. FFTW manual/FFTW3 design references for FFT planning/performance considerations.
5. CUDA elapsed-time/effective-bandwidth methodology references (adapted conceptually for backend timing consistency).

---

## Minimal PR1 Work Breakdown (reviewable sequence)

1. Scaffold `examples/SAR` build, README, and JSON topology.
2. Add `SarMessages.hpp` contracts and deterministic synthetic source node.
3. Add tile split and merge nodes with correctness-first behaviors.
4. Wire reuse of existing async transfer/device stage pattern and add example-local backprojection transform.
5. Add metrics sink and diagnostics contracts.
6. Add unit/integration tests and baseline comparison harness.
7. Add CI-safe profile configuration and docs.

This yields a complete, reviewable vertical slice aligned with existing GraphX architecture and your PR1 constraints.
