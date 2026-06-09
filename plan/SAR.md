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

## Current State Update (Post-PR7)

As of the latest SAR checklist consolidation:

1. PR6 is complete for its intended scope: runtime matched-filter path, accel-token guardrails, resolver/Metal substitution audit, benchmark trace contract, and deterministic display artifact coverage.
2. PR7 is complete for its intended scope: materialized image-sample graph/direct parity harness, PR6 trace-compatibility preservation, and native matched-filter/range-compression kernel gate decision.
3. Full SAR example unit coverage is green in the local native-metal configuration (`test_sar_example_unit`: 60 tests passing).
4. The PR6 Metal substitution review and ownership decisions remain documented in plan/SAR_PR6_METAL_AUDIT.md.

What remains intentionally deferred out of PR7 and carried into PR8:

1. Exhaustive sidecar identity matrix hardening across baseline/fanout/materialized topologies.
2. Explicit attribution-discipline enforcement for benchmark performance claims.
3. External-data non-CI validation lane (kept out of CI profile).
4. Device-side matched-filter/range-compression Metal kernel implementation remains deferred unless a new parity gate is opened with accepted evidence.

PR8 planning entrypoint:

- plan/SAR_PR8_CHECKLIST.md

PR8 should preserve all PR6/PR7 invariants:

1. GraphExecutorBuilder plus JSON remains the canonical user-facing runtime path.
2. Transfer/kernel edges remain accel-token contracts with SAR sidecars.
3. Portable SAR JSON remains generic-intent driven, with resolver-managed backend substitution.

### PR7 Slice 1.5 Native Matched-Filter Gate Decision (Recorded)

Decision: defer native matched-filter/range-compression Metal kernel implementation in PR7.

Evidence reviewed from current deterministic fixtures and benchmark trace contracts:

1. Matched-filter parity is already passing against CPU reference in current runtime path (`RangeCompressionNodeTest.MatchedFilterModeMatchesCpuReferenceMagnitude`, `SarCpuReferenceTest.MatchedFilterKnownVectorFindsDelayedEcho`).
2. PR7 graph materialized-image parity harness is passing (`SarJsonPipelineTest.Pr7MaterializedImageParityMetricsMatchReference`).
3. PR6 benchmark trace contract remains intact and confirms native backprojection execution evidence while matched-filter parity remains in tolerance (`SarTraceSchemaTest.BenchmarkTraceContainsRequiredSchemaAndDiagnosticsFields`).

Rationale for defer:

1. Current evidence confirms fidelity and contract correctness for runtime matched-filter behavior without introducing a new native kernel risk surface.
2. PR7 priority remains materialized graph/direct parity and trace/contract stability; adding native range-compression kernels now would expand scope and regression risk beyond current acceptance gates.
3. Native kernel expansion should proceed only in a dedicated follow-up slice/PR with explicit CPU parity tables, sidecar/accel-token preservation checks, and benchmark attribution proving benefit.

### PR8 Kickoff Focus

PR8 focuses on contract hardening and validation depth rather than new kernel rollout:

1. Sidecar identity propagation audit and regression coverage expansion.
2. Accel-token + resolver preset integrity checks across maintained SAR JSON topologies.
3. Performance-attribution policy enforcement in benchmark trace review/tests.
4. External-data readiness lane definition for local/manual runs while preserving deterministic CI scope.

### PR8 Progress Update (Slice 2.1 Complete)

PR8 Slice 2.1 is complete: sidecar identity matrix hardening is now explicitly covered in GraphExecutor-driven baseline, fanout, and PR7 materialized-image paths.

Completed validation outcomes:

1. Added explicit sidecar envelope identity assertions for sequence/batch/aperture/pulse-range/stream/tile fields and marker propagation in JSON runtime tests.
2. Added transfer/kernel metadata checks for backend/queue identifiers, with host-backend merge tolerance where kernel tickets are intentionally absent.
3. Stabilized fanout terminal-frame assertions against observed Data-vs-EOS end-of-run variability while preserving identity and diagnostics invariants.
4. Full SAR example unit target remains green in the local native-metal configuration (`test_sar_example_unit`: 60 tests passing).

### PR8 Progress Update (Slice 2.2 Complete)

PR8 Slice 2.2 is complete: SAR preset contract audit now enforces accel-token edge-contract usage and explicit resolver metadata across maintained SAR JSON presets.

Completed validation outcomes:

1. Added explicit resolver-contract fields (`execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, `edge_contract`) to maintained non-metal presets (`sar_stripmap_pr1`, `sar_stripmap_pr2_fanout`, `sar_stripmap_pr7_materialized_image`, `sar_projectile_approach_pr1`).
2. Added `SarJsonRuntimeTest.MaintainedPresetsKeepAccelTokenAndResolverContractExplicit` to assert, for all maintained presets, JSON-level contract keys, parser-level resolver values, and portable (non-concrete) node intents.
3. Revalidated resolver metadata audits for PR3/PR6 metal presets in the existing `SarPr3MetalJsonTest.*PresetUsesAccelTokenResolverMetadata` suite.
4. Full SAR example unit target remains green in the local native-metal configuration (`test_sar_example_unit`: 61 tests passing).

### PR8 Progress Update (Slice 2.3 Complete)

PR8 Slice 2.3 is complete: benchmark attribution discipline is now enforced by trace-level claim-policy fields plus schema assertions that fail when attribution evidence is missing or when lifecycle totals are used as speedup basis.

Completed validation outcomes:

1. Extended SAR benchmark trace output with a `performance_claim_policy` contract carrying required attribution policy flags, evidence presence, explicit speedup basis, and explicit disallowed lifecycle-only claim sources.
2. Added schema-test guardrails that require `performance_claim_policy` presence/values and enforce consistency between `overhead_ms.graph_run_minus_baseline_median` and `overhead_attribution.cost_buckets.graph_overhead_ms`.
3. Preserved PR6/PR7 trace compatibility while extending attribution checks (existing compatibility key matrix remains enforced).
4. Full SAR example unit target remains green in the local native-metal configuration (`test_sar_example_unit`: 61 tests passing).

### PR8 Progress Update (Slice 2.4 Complete)

PR8 Slice 2.4 is complete: a strict non-CI external-data lane scaffold now exists for Gotcha-style replay with explicit CI guards, local/manual opt-in controls, and documentation.

Completed validation outcomes:

1. Added `GotchaReplaySourceNode` external-fixture gate policy: non-test fixture paths are rejected unless both `allow_external_fixture=true` and `GRAPHX_SAR_ALLOW_EXTERNAL_DATA=1` are set.
2. Added guardrail tests that verify reject-by-default behavior, environment-gate enforcement, and successful explicit local/manual opt-in for external fixtures.
3. Added manual external replay topology scaffold (`examples/SAR/config/sar_gotcha_external_manual.json`) and documented usage/constraints in `examples/SAR/README.md`.
4. Full SAR example unit target remains green in the local native-metal configuration (`test_sar_example_unit`: 64 tests passing).

### PR8 Closeout (Complete)

PR8 is complete. All planned slices (2.1-2.4) and acceptance criteria are closed with passing SAR validation in the native-metal local lane.

Closeout summary:

1. Sidecar identity matrix is hardened across baseline, fanout, and PR7 materialized paths.
2. JSON preset contract audits enforce explicit resolver metadata and accel-token edge-contract invariants.
3. Benchmark trace now carries explicit performance-claim policy and attribution evidence guardrails.
4. External-data validation lane is explicitly non-CI, opt-in gated, and documented for local/manual execution.
5. Ownership boundaries remain intact: SAR-specific adapters stay in `examples/SAR`; no promotion to shared libs was introduced in PR8.

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
2. SAR graph edges carry accel tokens plus SAR metadata sidecars instead of raw SAR payload envelopes.
3. Local SAR H2D/D2H nodes provide a CI-safe simulated async-transfer lane.
4. `ImageTileMergeNode` is correctly treated as the key fan-in/correctness node.
5. The benchmark compares GraphX against a direct non-graph baseline and reports repeated-run statistics.

Main PR2 design concern: current tile semantics are deterministic but symbolic. `AzimuthTileSplitNode` assigns tile IDs using pulse modulo behavior, which distributes pulses across tile IDs but does not yet create true graph-visible fan-out. PR2 should evolve toward one pulse/aperture block producing multiple independent range/azimuth/image tiles.

Accel-token conversion update: the current SAR graph-processing direction is that graph edges carry `graph::gpu::accel` views, leases, tickets, and lightweight SAR sidecar metadata. Historical `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` language should be treated as PR1 compatibility vocabulary or wrapper internals only. New PR3 topology, resolver, benchmark, and trace work must not reintroduce raw SAR payload-message edges between transfer/kernel stages.

Post-PR4 execution invariant: `examples/SAR/src/main.cpp` is the canonical SAR example entrypoint and must remain driven by `GraphExecutorBuilder` plus JSON topology/config files. All user-facing SAR examples, benchmarks, and integration paths must preserve the GraphExecutor + JSON execution path as the primary runtime contract.

Direct/programmatic SAR execution is allowed only for CPU reference baselines, parity checks, graph-overhead attribution, and focused unit tests. It must not become the primary example path or replace JSON-driven graph execution in demos, benchmarks, or validation flows.

Every future SAR update must explicitly consider whether `examples/SAR/config/*.json`, plugin loading, dynamic node registration, edge contracts, accel-token flow, backend resolver behavior, and GraphExecutor execution tests need to change. New or modified SAR nodes must remain usable from JSON config and dynamically loadable through the SAR plugin path unless the change is explicitly limited to non-runtime test/reference code.

Ownership guidance: when a Metal implementation is SAR-specific rather than reusable, it may remain as a node or adapter under `examples/SAR`. Reserve `libgpu` for generic Metal nodes, runtime helpers, or kernels that clearly apply beyond the SAR example boundary.

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

| Node | Reused/New | Proposed Path | Edge/token contract (in/out) | PR1 Tests | Deferred Follow-up |
| --- | --- | --- | --- | --- | --- |
| SyntheticApertureIqSourceNode | New | examples/SAR/include/sar/SyntheticApertureIqSourceNode.hpp | out: host accel token (`HostPinnedBufferView` or compatible stub token) plus SAR pulse sidecar: sequence ids, pulse range, geometry/meta, EOS | deterministic output, EOS correctness, fixed counts | richer scene models/noise/motion error |
| RangeWindow/RangeCompression stage | Reuse existing DSP pattern or example-local deterministic placeholder | wired from existing GraphX DSP style | in: host accel token plus pulse sidecar; out: host range-tile token plus SAR tile sidecar | sample count invariants, deterministic transform output | real matched filter fidelity and accelerated FFT |
| AzimuthTileSplitNode | New | examples/SAR/include/sar/AzimuthTileSplitNode.hpp | in: host range token plus tile sidecar; out: independent branch tokens with `batch_id`/`aperture_id`/`tile_id` sidecars | tile fan-out count, metadata completeness | adaptive tiling/scheduling |
| H2D async transfer | Reused | existing GPU async transfer pattern | in: `HostPinnedBufferView`/lease token plus SAR tile sidecar; out: `TransferTicket` + `DeviceBufferView` token plus same sidecar | bytes moved/transfer ticket counters | backend-specific tuning |
| SarBackprojectionTransformNode | New | examples/SAR/include/sar/SarBackprojectionTransformNode.hpp | in: `DeviceBufferView` token plus kernel descriptor/SAR tile sidecar; out: device image token plus `KernelTicket` and SAR image sidecar | dispatch count, deterministic tile output | native backend kernels |
| D2H async transfer | Reused | existing GPU async transfer pattern | in: device image token plus SAR sidecar; out: `TransferTicket` + `HostPinnedBufferView` token plus SAR image sidecar | D2H bytes counters | overlap/stream tuning |
| ImageTileMergeNode | New | examples/SAR/include/sar/ImageTileMergeNode.hpp | in: host image tokens plus SAR tile sidecars and EOS/watermark; out: final host token/status sidecar plus merge diagnostics | duplicate/missing/out-of-order/EOS matrix | partial preview/sliding aperture |
| Detection/Metrics sink | Reuse existing sink pattern or small example sink | examples/SAR/src/main.cpp wiring | in: final token/status sidecar plus diagnostics bundle | metrics presence/tolerance checks | richer observability/export |

Why SAR differs from vibration-health pipeline: SAR emphasizes tile independence, explicit async transfer boundaries, fan-in merge correctness under out-of-order completion, and graph-vs-baseline overhead attribution for image formation stages.

PR2 correction to the node model: make tile fan-out real at the graph level. The current `AzimuthTileSplitNode` tile assignment is deterministic and useful for PR1, but PR2 should move toward one pulse block or aperture block emitting N independent tiles that can traverse distinct graph branches.

---

## 4) Edge Token and Metadata Types Needed

Define SAR metadata sidecars in `examples/SAR/include/sar/SarMessages.hpp`, but keep graph data movement represented by `graph::gpu::accel` token contracts.

1. Accel edge tokens
   - `graph::gpu::accel::HostPinnedBufferView` for host-resident pulse/range/image buffers.
   - `graph::gpu::accel::DeviceBufferView` for device-resident range/image buffers.
   - `graph::gpu::accel::BufferLease` for lifetime ownership and release accounting.
   - `graph::gpu::accel::TransferTicket` for H2D/D2H enqueue/completion metadata.
   - `graph::gpu::accel::KernelTicket` for transform/reduce dispatch metadata.
2. SAR identity sidecar
   - `sequence_id`, `batch_id`, `aperture_id`, `pulse_range_start`, `pulse_range_count`, `tile_id`, `tile_count`, `range_block_id`, `azimuth_block_id`, frame/EOS markers.
3. SAR signal/geometry sidecar
   - `samples_per_pulse`, `sample_rate_hz`, `carrier_hz`, chirp metadata, image tile dimensions, platform/scene geometry metadata, deterministic seed/profile fields.
4. SAR diagnostics/status sidecar
   - `pulses_processed`, `tiles_processed`, `bytes_h2d`, `bytes_d2h`, `kernel_dispatches`, `fanin_wait_ms`, `e2e_latency_ms`, transfer/kernel timing, queue/backpressure counters, merge completeness counters.
5. Compatibility wrappers
   - Historical `SarPulseBlockMessage`, `SarRangeTileMessage`, `SarImageTileMessage`, `SarDeviceLeaseMessage`, and `SarTransferTicketMessage` names may remain as adapter/test vocabulary only when they wrap or reference accel tokens.
   - PR3 native/resolved topologies must not use those historical SAR payload wrappers as transfer/kernel edge contracts.

Buffer lifetime labels (PR1 diagnostics-level, not framework state machine):
`Allocated -> HostFilled -> ReadyForTransfer -> TransferInFlight -> DeviceReady -> KernelRunning -> KernelComplete -> TransferBack -> HostReady -> Consumed -> Released`.

PR2/PR3 token-contract direction:

1. Unify SAR backend metadata with existing `graph::gpu::accel` contracts where possible.
2. Prefer wrapping or referencing existing GPU types such as `BackendKind`, `BufferLease`, `DeviceBufferView`, `HostPinnedBufferView`, `TransferTicket`, and `KernelTicket`.
3. Avoid growing parallel SAR-only equivalents (`SarBackendKind`, `SarBufferDescriptor`, `SarTransferTicketMessage`, `SarDispatchMetadata`) beyond what is needed for PR1 compatibility adapters.
4. Add better tile identity: `batch_id`, `aperture_id`, `tile_id`, `tile_count`, `pulse_range`, and backend/device/queue metadata.
5. Preserve SAR sidecars across resolver substitution so stub, Metal, CUDA, and SYCL variants expose equivalent graph-facing contracts.

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

1. Source creates deterministic host-resident pulse buffers and emits host accel tokens with sequence ids and explicit EOS sidecars.
2. Optional deterministic range window/compression stage normalizes sample shape while preserving the host token plus SAR sidecar contract.
3. `AzimuthTileSplitNode` exposes CPU-visible DAG width via independent tile token branches.
4. H2D transfer stage consumes host tokens and emits device tokens plus `TransferTicket` metadata.
5. `SarBackprojectionTransformNode` runs per-tile work unit against a device token and emits device image token plus `KernelTicket` metadata.
6. D2H consumes device image tokens and returns host image tokens with completion sidecars.
7. `ImageTileMergeNode` validates tile completeness/uniqueness/watermark behavior from SAR sidecars while consuming host image tokens.
8. Metrics sink emits deterministic counters, token lifecycle data, and latency figures.

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
| GPU boundary strategy | Generic SAR nodes with GPU underpinnings | full SAR graph surface built from Metal-specific nodes | keeps SAR graph portable while still allowing backend execution paths | requires explicit adapter seam and backend policy |
| Device stage representation | Example-local SAR wrappers over backend capabilities | immediate libgpu core extension of SAR semantics | keeps SAR-specific semantics in `examples/SAR` while backend nodes stay reusable | duplicate logic risk until generalized |
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

---

## 16) PR3 Metal Node Gap Analysis (2026-06-07)

This analysis compares SAR pipeline stages and accel-token edge contracts against currently available Metal node families.

### Current state in SAR PR3 JSON presets

Current files:

1. `examples/SAR/config/sar_stripmap_pr3_metal_window.json`
2. `examples/SAR/config/sar_stripmap_pr3_metal_compression.json`

These files should use portable SAR example node types (`SyntheticApertureIqSourceNode`, `RangeWindowNode`/`RangeCompressionNode`, `AzimuthTileSplitNode`, `H2DAsyncNode`, `SarBackprojectionTransformNode`, `D2HAsyncNode`, `ImageTileMergeNode`, `SarDiagnosticsSinkNode`) as generic intents plus `execution_backend`/resolver policy.

They should not rely on backend-only SAR payload wrappers or `backend=2` metadata tags as a substitute for resolver-selected accel-token variants. Concrete Metal node plugin types such as `H2DAsyncNodeMetal`, `D2HAsyncNodeMetal`, and `DeviceTransformNodeMetal` may appear in backend-specific validation topologies, but portable SAR presets should remain generic-intent JSON and report the resolved concrete types in graph-build diagnostics.

### Available Metal node types today (libgpu)

From `libgpu/plugins/metal_*.cpp` and `libgpu/include/gpu/metal/nodes/*.hpp`:

1. `HostIngressPinnedSourceNodeMetal` (source, emits `accel::HostPinnedBufferView`)
2. `H2DAsyncNodeMetal` (`HostPinnedBufferView -> DeviceBufferView`)
3. `DeviceShardNodeMetal` (`DeviceBufferView -> DeviceBufferView`)
4. `DeviceTransformNodeMetal` (`DeviceBufferView -> DeviceBufferView`, kernel descriptor driven)
5. `DeviceKernelNodeMetal` (`DeviceBufferView -> DeviceBufferView`, descriptor-driven input/output kernel boundary)
6. `DeviceReduceNodeMetal` (`DeviceBufferView -> DeviceBufferView`, kernel descriptor driven)
7. `D2HAsyncNodeMetal` (`DeviceBufferView -> HostPinnedBufferView`)
8. `HostEgressSinkNodeMetal` (sink, consumes `HostPinnedBufferView`)
9. `QueueSyncNodeMetal` (`DeviceBufferView -> DeviceBufferView`)
10. `LeaseReleaseNodeMetal` (sink, consumes `accel::BufferLease`)
11. `PeerCopyNodeMetal` (`DeviceBufferView -> DeviceBufferView`)
12. `CollectiveReduceNodeMetal` (present but runtime marked unsupported)

### Post-conversion contract summary

SAR graph edges now operate on accel tokens plus SAR sidecar metadata:

1. `accel::HostPinnedBufferView`
2. `accel::DeviceBufferView`
3. `accel::BufferLease`
4. `accel::TransferTicket`
5. `accel::KernelTicket`
6. SAR sidecars for sequence, aperture, tile identity, geometry, EOS/watermark, and diagnostics

This removes the old H2D/D2H edge type blocker. Direct substitution is now viable for transfer and generic device-transform boundaries when the resolver proves port-contract parity and preserves SAR sidecars.

Remaining blockers are no longer raw edge type mismatch. They are resolver policy, SAR kernel descriptor coverage, sidecar propagation, native runtime availability, and trace/diagnostic parity.

### Stage-by-stage compatibility matrix

| SAR stage | Current SAR node type | Closest Metal node type(s) | Token-edge replacement status | Remaining gap category |
| --- | --- | --- | --- | --- |
| Source IQ generation | `SyntheticApertureIqSourceNode` (host token + pulse sidecar) | `HostIngressPinnedSourceNodeMetal` | Partial | Source must generate SAR-specific deterministic samples and sidecars; Metal ingress can provide host token mechanics |
| Range window/compression | `RangeWindowNode` / `RangeCompressionNode` (host token -> host/range token) | `DeviceTransformNodeMetal` (kernel) | Partial | Need SAR range-window/compression kernel descriptors and host/device staging policy |
| Tile split/fanout | `AzimuthTileSplitNode` (token + tile sidecar fan-out) | `DeviceShardNodeMetal` | Partial | `DeviceShardNodeMetal` shards bytes; SAR split also owns tile identity and aperture semantics |
| H2D boundary | `H2DAsyncNode` (`HostPinnedBufferView` -> `DeviceBufferView`) | `H2DAsyncNodeMetal` | Yes, with resolver | Must preserve SAR sidecar and emit resolved concrete node diagnostics |
| Backprojection kernel | `SarBackprojectionTransformNode` (`DeviceBufferView` -> `DeviceBufferView`) | `DeviceKernelNodeMetal` / `DeviceTransformNodeMetal` | Partial | SAR node can now bind and delegate to the generic Metal kernel boundary while preserving sidecar identity; still need production SAR backprojection math, geometry parameter binding, and native-runtime parity tests |
| D2H boundary | `D2HAsyncNode` (`DeviceBufferView` -> `HostPinnedBufferView`) | `D2HAsyncNodeMetal` | Yes, with resolver | Must preserve SAR sidecar and timing counters |
| Tile merge | `ImageTileMergeNode` (host token + merge sidecars) | `DeviceReduceNodeMetal` (not semantic equivalent) | Partial/future | DeviceReduce may accelerate accumulation, but host merge still owns watermark/EOS/diagnostics semantics |
| Diagnostics sink | `SarDiagnosticsSinkNode` (status/diagnostics sidecar) | `HostEgressSinkNodeMetal` | Partial | Need token lifecycle/trace export parity with SAR diagnostics |

### Existing Metal kernel capability status

Native Metal capability supports builtin/inline/metallib registration via descriptor, but builtin kernel source generation currently resolves only generic families:

1. XOR inplace byte transform
2. identity inplace byte transform
3. reduce-to-metrics over byte buffer

No SAR-specific Metal kernel functions are currently represented in native builtin generation.

### New Metal kernel work items needed for SAR

The following kernel-level work is required for true SAR Metal execution path beyond metadata-only backend tags:

1. `graphx_sar_range_window_f32` (or equivalent): deterministic windowing over complex/float SAR range vectors.
2. `graphx_sar_range_compression_fft_*` path:
   - Either explicit FFT kernel path plus matched-filter multiply kernels, or
   - Integration path to backend FFT capability (if introduced) with adapter kernels for pre/post layout transforms.
3. `graphx_sar_backprojection_tile_f32` (core PR3 target): tile backprojection kernel with explicit geometry parameter contract.
4. Optional merge-side kernels for future GPU merge/reduction acceleration (`graphx_sar_tile_accumulate_*`) if merge leaves host path.

Important: this can be implemented either as new SAR-specialized Metal node wrappers or by reusing `DeviceKernelNodeMetal`/`DeviceTransformNodeMetal`/`DeviceReduceNodeMetal` plus new kernel descriptors and SAR adapter nodes. `SarBackprojectionTransformNode` now follows the preferred adapter direction for native-device mode: it binds GPU capabilities, configures `DeviceKernelNodeMetal`, delegates the kernel launch, and restores the SAR sidecar token for downstream merge.

### New node/adaptor work items needed (non-kernel)

The selected direction is accel-token graph edges with SAR metadata sidecars. Adapter work should therefore be limited to token/sidecar bridging, legacy test shims, and capability binding, not payload-envelope translation.

Required non-kernel work:

1. Preserve SAR sidecars through H2D/D2H/kernel/transform/reduce resolver substitution. Initial coverage exists for the SAR backprojection adapter delegating to `DeviceKernelNodeMetal`.
2. Validate topology schemas reject legacy payload-envelope edges in PR3 native/resolved presets unless an explicit compatibility adapter is present.
3. Emit graph-build diagnostics for generic intent -> concrete backend variant resolution.
4. Include lease release/lifecycle integration where `LeaseReleaseNodeMetal` or equivalent backend capability is selected.
5. Keep any remaining `SarPulseBlockMessage`/`SarRangeTileMessage`/`SarImageTileMessage` wrappers out of native transfer/kernel edge contracts.

### Architectural decisions and remaining implementation gates

Resolved:

1. **Edge contract strategy**
   - Selected: move SAR graph to accel view/token edges and represent SAR metadata as sidecar/context objects.
2. **Ownership boundary for SAR+Metal integration**
   - Selected: `examples/SAR` owns SAR graph contracts and metadata sidecars; `libgpu` owns reusable backend runtime nodes and accel token types.

Resolved PR3 decisions:

1. **Kernel packaging strategy**
   - Selected for PR3: register SAR kernels through explicit kernel descriptors with inline-source support when the native runtime is available.
   - Defer versioned `.metallib` artifacts until the SAR kernel ABI and geometry parameter block stabilize.
   - Builtin names remain acceptable only for reusable generic kernels already owned by `libgpu`; SAR-specific kernels should not be hidden behind generic builtin names until they have reusable value outside `examples/SAR`.
2. **Merge location**
   - Selected for PR3: keep `ImageTileMergeNode` host-side for deterministic diagnostics, watermark/EOS handling, and graph-vs-baseline parity.
   - Device-side partial merge/reduce remains a future optimization experiment after native kernel parity is established.
3. **Queue/device policy**
   - Selected for PR3: use a single backend queue by default for deterministic CI/local behavior.
   - Explicit `QueueSyncNodeMetal` or equivalent queue-boundary nodes may be introduced only when measuring transfer/kernel overlap; trace output must then report queue id, transfer time, kernel time, and overlap utilization.
4. **Test/CI policy for native Metal**
   - Selected for PR3: CI correctness gates must pass with the accel-token/stub lane and skip native runtime checks when unavailable.
   - Local native acceptance uses `GRAPHX_REQUIRE_METAL_NATIVE_RUNTIME=ON`; when enabled on macOS with Metal runtime available, native SAR kernel tests must fail fast if kernels cannot compile/bind.
   - PR3 documentation and checklists must not describe the simulated native flag as true backend SAR compute until a native SAR kernel is present.

Remaining implementation gate:

1. Replace the simulated native SAR compute path with backend-specific SAR kernel execution where runtime is available. Existing Metal support has generic transform/reduce kernels, but no SAR range-compression or backprojection kernel entry points yet.

### PR3 performance and attribution policy

1. CI performance gates verify metric presence and deterministic graph-vs-baseline parity, not absolute GPU speed.
2. Local native performance gates should report:
   - graph build time,
   - graph run time,
   - graph lifecycle teardown time,
   - baseline execute time,
   - transfer payload bytes,
   - transfer and kernel timing,
   - queue/backpressure counters,
   - overlap utilization when multiple queues or explicit sync nodes are enabled.
3. Benchmark overhead attribution must treat H2D/D2H byte counters as transfer-stage payload counters only. Accel-token graph edges carry tokens and SAR sidecars, so token-edge payload copy attribution is zero unless a compatibility adapter explicitly copies payload bytes.
4. Suggested local representative thresholds after native kernels are available:
   - diagnostics parity with the non-graph baseline is required,
   - graph run median should be reported separately from lifecycle total,
   - graph scheduling overhead should be tracked as `median(graph_run) - median(baseline_execute)`,
   - native profile thresholds should be set per machine/backend after collecting at least three local baseline runs.

### Required PR3 testing after token conversion

1. Topology validation: PR3 native/resolved presets use accel-token edge contracts and fail fast on legacy SAR payload-envelope edges.
2. Resolver conformance: generic `H2DAsyncNode`, `D2HAsyncNode`, transform, and reduce intents resolve to compatible stub/Metal/CUDA/SYCL variants where available.
3. Sidecar propagation: `batch_id`, `aperture_id`, `pulse_range_start`, `pulse_range_count`, `tile_id`, `tile_count`, EOS/watermark, backend/device/queue ids survive source -> split -> H2D -> transform -> D2H -> merge.
4. Trace schema: emitted traces include token ids, lease ids, transfer ticket ids, kernel ticket ids, resolved concrete node types, backend lane, queue id, and timing counters.
5. Negative compatibility: backend-native PR3 presets must reject `SarRangeTileMessage`/`SarImageTileMessage` style payload edges unless an explicit compatibility adapter is declared.
6. Benchmark parity: `sar_benchmark --profile=ci --range-stage=compression --native-backend --trace-out ...` reports token-edge counters and graph-vs-baseline overhead without attributing raw payload copies to graph edges.

Implementation should not mark PR3 complete until these tests are present or intentionally deferred with maintainer sign-off.

### Boundary decision for SAR GPU usage

Selected boundary:

1. SAR example graphs should remain generic and portable at the application layer.
2. GPU-specific behavior belongs in backend-capability nodes, adapters, and native runtime plumbing.
3. Metal-specific node families in `libgpu` are valid backend/runtime constructs, but they should not become the default public SAR graph surface.
4. The SAR/Metal seam should stay at accel-token sidecar propagation and capability binding, not at the core SAR algorithm contract.

Practical interpretation:

1. `examples/SAR` owns SAR algorithm nodes, SAR metadata sidecars, and SAR topology definitions.
2. `libgpu` owns the reusable Metal node families and the GPU capability runtime.
3. Any SAR-to-Metal integration should be deliberate and resolver/capability-based, not a wholesale replacement of SAR nodes with `*Metal` nodes in portable presets.

---

## 17) Re-evaluation: Token-Edge Model with Implicit Backend Node Substitution

This section re-evaluates the SAR GPU strategy using the viewpoint:

1. Edges remain token/control-plane contracts, not raw payload copies.
2. JSON config uses generic node intents (for example `H2DAsyncNode`).
3. Runtime/build resolution selects backend-specific node implementation (`H2DAsyncNodeMetal`, `H2DAsyncNodeCuda`, `H2DAsyncNodeSycl`) based on available GPU support and policy.

This model is consistent with `doc/architecture/CUDA_GRAPH_NODE_IMPLEMENTATION_PLAN.md`, especially:

1. thin layers,
2. tokenized movement,
3. backend parity,
4. tri-lane completion (stub + backend lane + backend lane).

### What changes vs previous SAR analysis

Previous analysis assumed static node type names in JSON were the final implementation type. Under implicit substitution, JSON node names become *capability intents* and concrete class selection happens later.

Implication:

1. `H2DAsyncNode` in a SAR JSON can validly map to `H2DAsyncNodeMetal` on macOS native-metal execution,
2. while mapping to CUDA/SYCL/stub variants on other configured lanes,
3. without changing topology shape.

### Feasibility requirements for implicit substitution

To make this safe, the resolver must enforce strict compatibility:

1. **Port contract compatibility**
   - all backend variants for a generic intent must expose equivalent graph-facing port contracts.
2. **Configuration contract compatibility**
   - shared generic fields must be accepted by all variants,
   - backend-specific extensions must be namespaced or optional.
3. **Deterministic selection policy**
   - explicit order: requested backend > available native runtime > fallback/stub.
4. **Capability-bus validation before graph start**
   - mapping must fail early if the selected backend node cannot bind required capabilities.

### Proposed resolver boundary

Introduce a graph-load substitution layer (name illustrative):

1. `NodeIntentResolver` resolves intent type -> concrete type before instantiation.
2. `BackendSelectionPolicy` determines target backend per graph/profile.
3. `NodeVariantRegistry` provides allowed variants per intent.

Example registry mapping (conceptual):

1. `H2DAsyncNode` -> `{H2DAsyncNodeCuda, H2DAsyncNodeSycl, H2DAsyncNodeMetal, H2DAsyncNodeStub}`
2. `D2HAsyncNode` -> `{D2HAsyncNodeCuda, D2HAsyncNodeSycl, D2HAsyncNodeMetal, D2HAsyncNodeStub}`
3. `DeviceTransformNode` -> `{DeviceTransformNodeCuda, DeviceTransformNodeSycl, DeviceTransformNodeMetal, DeviceTransformNodeStub}`

### Updated SAR-specific interpretation

For SAR, keep domain nodes explicit where they encode SAR semantics:

1. `SyntheticApertureIqSourceNode`
2. `RangeWindowNode` / `RangeCompressionNode`
3. `AzimuthTileSplitNode`
4. `SarBackprojectionTransformNode`
5. `ImageTileMergeNode`
6. `SarDiagnosticsSinkNode`

For backend boundary intents, prefer generic names with resolver substitution:

1. `H2DAsyncNode`
2. `D2HAsyncNode`
3. optional generic kernel/transform intents where practical.

This preserves SAR topology readability while still enabling true backend node execution.

### Architectural risks in this model

1. **Semantic drift risk**
   - backend variants may diverge in behavior despite matching signatures.
2. **Config drift risk**
   - intent-level config fields can drift from variant-specific requirements.
3. **Observability risk**
   - debugging becomes harder if the resolved concrete node is not surfaced in diagnostics.

Mitigations:

1. emit resolved node type map at graph build time,
2. require conformance tests per intent across CUDA/SYCL/Metal/stub lanes,
3. enforce parity checks in CI for intent contracts.

### Recommended decision under this viewpoint

Adopt a mixed model:

1. Domain algorithm nodes remain explicit and generic to the problem domain (SAR).
2. Backend-boundary nodes use generic intent names resolved implicitly to backend-specific nodes.
3. Backend-specific node families remain first-class implementations in `libgpu`, but become an implementation target of resolver policy instead of the default JSON surface.

### Open implementation decisions for this model

1. Where substitution happens:
   - JSON pre-processing,
   - GraphBuilder node-resolution phase,
   - provider-level aliasing.
2. How backend preference is expressed:
   - graph-level field (for example `execution_backend: metal|cuda|sycl|stub`),
   - CLI/preset,
   - capability discovery default.
3. How to represent backend-specific config extensions without breaking intent-level portability.

### Resolver Contract (Normative)

The resolver contract for generic-intent node substitution is defined as follows.

#### Core architecture contract

1. Edges carry tokens, context, metadata, leases, and tickets.
2. Edges do not imply byte movement.
3. Nodes transform messages and declare intent.
4. Nodes do not secretly own the data plane.
5. Capabilities perform backend work.
6. GPU behavior belongs behind CUDA/SYCL/Metal/simulated capability boundaries.
7. SAR is an example package, not a new framework layer.
8. SAR-specific types stay under `examples/SAR` unless promoted deliberately.
9. PR1 must demonstrate the architecture, not perfect SAR math.

#### Intent resolution inputs

1. Graph-level backend preference: `execution_backend` in `{auto, metal, cuda, sycl, stub}`.
2. Optional per-node override: `backend_override` in `{metal, cuda, sycl, stub}`.
3. Runtime capability discovery via capability bus.
4. Build-time backend gates (for example backend enable flags).

#### Intent resolution precedence

1. If `backend_override` is set, use that backend or fail fast.
2. Else if graph-level `execution_backend` is not `auto`, use it or fail fast.
3. Else use deterministic auto policy: metal on macOS with native capability, otherwise cuda, otherwise sycl, otherwise stub.
4. If no compatible variant exists, graph build fails before execution starts.

#### Contract parity requirements

1. All variants for one intent must expose equivalent graph-facing port contracts.
2. Shared intent config fields must parse identically across variants.
3. Backend-specific fields must be optional and namespaced to avoid portability breakage.
4. Capability binding failures must be surfaced as build/init errors, not runtime silent fallback.

#### Required observability

1. Graph build must emit an intent-resolution map: `intent_type -> concrete_type -> selected_backend`.
2. Diagnostics must include fallback reason when non-requested variant is selected.
3. Test artifacts must record lane identity (stub/cuda/sycl/metal) for every intent conformance run.

### Resolver JSON Schema (Implementation-Ready)

The following schema defines portable resolver controls for generic-intent node substitution.

#### Graph-level fields

1. `execution_backend`
   - Type: string
   - Allowed: `auto`, `metal`, `cuda`, `sycl`, `stub`
   - Default: `auto`
   - Meaning: preferred backend family for all eligible generic-intent nodes.
2. `backend_fallback_policy`
   - Type: string
   - Allowed: `strict`, `allow_fallback`
   - Default: `strict`
   - Meaning:
     - `strict`: fail graph build/init if requested backend variant is unavailable.
     - `allow_fallback`: use precedence fallback and report downgrade reason.
3. `resolver_diagnostics`
   - Type: boolean
   - Default: `true`
   - Meaning: emit resolved node mapping and fallback annotations in build diagnostics.

#### Node-level fields (optional)

1. `backend_override`
   - Type: string
   - Allowed: `metal`, `cuda`, `sycl`, `stub`, `inherit`
   - Default: `inherit`
   - Meaning: per-node backend selection override.
2. `backend_fallback_policy`
   - Type: string
   - Allowed: `inherit`, `strict`, `allow_fallback`
   - Default: `inherit`
   - Meaning: optional per-node fallback behavior.
3. `backend_variant`
   - Type: string
   - Allowed: concrete variant type name (for example `H2DAsyncNodeMetal`)
   - Default: omitted
   - Meaning: explicit variant pinning for debugging or backend-validation scenarios.

#### Generic-intent portability rule

For portable SAR topologies, nodes should specify generic intent types (for example `H2DAsyncNode`, `D2HAsyncNode`).

Concrete `*Metal`, `*Cuda`, or `*Sycl` node types are allowed only when:

1. validating backend-specific runtime behavior,
2. reproducing backend-specific defects,
3. or intentionally creating backend-only test topologies.

#### Resolution algorithm (normative)

1. Parse graph-level resolver fields.
2. For each node:
   - determine effective backend preference from `backend_override` or graph-level `execution_backend`.
   - determine effective fallback policy from node-level override or graph-level policy.
3. Resolve node type:
   - if `backend_variant` is provided, attempt exact variant bind.
   - else if node type is generic intent, select variant by precedence.
   - else treat node type as concrete and validate availability.
4. Validate capability binding contract for selected variant.
5. Record resolution result and any fallback reason.
6. Fail before execution start when `strict` policy is violated.

#### Precedence for `allow_fallback`

When effective backend is `auto` or requested backend is unavailable under `allow_fallback`, the resolver chooses:

1. `metal` (macOS with native Metal capability),
2. `cuda`,
3. `sycl`,
4. `stub`.

This order may be overridden by explicit build/profile policy, but must remain deterministic and reported.

#### Error behavior

1. Unknown resolver field value: graph build error with field path and allowed values.
2. Unknown generic intent type: graph build error.
3. Requested backend unavailable under `strict`: graph init error with missing capability details.
4. Selected variant capability bind failure: graph init error.
5. Port-contract mismatch between intent and variant: graph build error.

#### Minimal schema example

```json
{
  "name": "sar_stripmap_generic_resolved",
  "execution_backend": "metal",
  "backend_fallback_policy": "allow_fallback",
  "resolver_diagnostics": true,
  "nodes": [
    {
      "id": "h2d",
      "type": "H2DAsyncNode",
      "node_config": {
        "backend_override": "inherit",
        "backend_fallback_policy": "inherit"
      }
    },
    {
      "id": "d2h",
      "type": "D2HAsyncNode",
      "node_config": {
        "backend_override": "metal"
      }
    }
  ]
}
```

## PR3 Completion Summary (2026-06-07)

PR3 implementation scope is complete for the Metal-native lane and ready for review.

Completed in this phase:

1. Native backprojection execution path delegates through `DeviceKernelNodeMetal` in `SarBackprojectionTransformNode` and executes descriptor-driven inline Metal kernels.
2. Backprojection kernel behavior progressed from placeholder copy semantics to interpolation-based accumulation with configurable shaping controls:
   - `tap_count`
   - `delay_step`
   - `phase_tap_scale`
   - `phase_aperture_scale`
3. Accel-token contract migration and sidecar propagation requirements are satisfied for PR3 topologies and tests.
4. Trace and benchmark evidence now includes resolved backend lane, token lifecycle fields, kernel tickets, and execution-outcome telemetry.
5. SAR PR3 validation targets (`sar_example_unit`, trace schema checks, and benchmark smoke path) are passing in the native-metal build profile.

Explicitly deferred beyond this PR3 closeout:

1. CUDA/SYCL native SAR kernel parity is not part of this finalized PR3 merge scope and remains future follow-up dependent on runtime availability and backend lane priorities.

Acceptance posture:

1. PR3 is complete for the resolver-driven, accel-token SAR architecture with native Metal kernel execution evidence.
2. Remaining backend expansion work is incremental and does not block PR3 review/merge.

## Post-PR3 Executive Recommendation

The best next PR is to add a deterministic CPU reference SAR correctness harness before further GPU expansion.

Rationale:

- PR3 has strengthened the GraphX architecture path with accel-token contracts, resolver-driven backend selection, and benchmark overhead attribution.
- The remaining highest-risk area is SAR mathematical correctness.
- Native GPU/Metal work should be gated by CPU scalar references, deterministic fixtures, and numeric parity tolerances.
- Future GPU kernels must prove parity against CPU reference output, not against another GPU path.

Non-goal:

- Do not add more native GPU kernels until point-target correctness, CPU reference parity, and graph/direct baseline parity are in place.

## PR5 Completion Summary

PR5 implementation scope is complete for the matched-filter reference and image-quality metric lane.

Completed in this phase:

1. Added CPU reference utilities for deterministic linear-FM chirp generation, delayed echo generation, matched-filter range compression, magnitude image conversion, and image-quality measurement under `examples/SAR`.
2. Added deterministic PR5 validation coverage for:
   - matched-filter known-vector peak detection,
   - off-grid point-target image-quality behavior,
   - two-point target relative peak behavior,
   - existing CPU-vs-native-Metal adapter parity.
3. Extended SAR benchmark trace output with PR5 accuracy/fidelity fields while preserving PR4 overhead attribution:
   - matched-filter vector length,
   - matched-filter peak bin/value,
   - peak location error,
   - impulse-response width,
   - peak sidelobe ratio,
   - integrated sidelobe ratio,
   - dynamic range,
   - deterministic image hash,
   - image metric timing,
   - graph/direct metric-delta placeholders.
4. Preserved the post-PR4 execution invariant: user-facing SAR execution remains driven by `GraphExecutorBuilder` plus JSON config/topology files, with direct execution limited to CPU reference, parity, graph-overhead attribution, and focused unit tests.
5. Preserved accel-token architecture: no raw SAR payload contracts were introduced across transfer/kernel graph edges, and benchmark trace output continues to distinguish transfer payload bytes from graph token movement.

Validation evidence:

1. `cmake --build build-ninja/ninja-debug --target test_sar_example_unit sar_benchmark`
2. `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit` passed 50/50 tests.
3. `./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci --range-stage=compression --trace-out /tmp/graphx_sar_pr5_trace.json` completed and emitted PR5 accuracy/fidelity metrics.

Explicitly deferred beyond PR5:

1. No new GPU/Metal kernels were added in PR5.
2. Runtime `RangeCompressionNode` behavior remains compatible with existing JSON presets; PR5 establishes the CPU reference target and trace contract before replacing production DSP behavior.
3. Full image-sample graph/direct parity remains a future follow-up because the current user-facing graph path reports diagnostics and token lifecycle evidence rather than materialized image samples.
4. Raw AFRL Gotcha `.mat` ingestion and large external datasets remain out of CI scope until a normalized tiny fixture path is defined.

## Post-PR5 Executive Recommendation

The best next PR is to connect the PR5 matched-filter reference contract to the runtime SAR range-compression path and tighten graph/direct parity from diagnostics-level equivalence toward image/algorithm metric equivalence.

Rationale:

- PR4 and PR5 now provide CPU reference foundations, deterministic fixtures, image metrics, and benchmark trace evidence.
- The remaining highest-leverage fidelity gap is that the production `RangeCompressionNode` still behaves as FFT magnitude extraction rather than matched-filter SAR range compression.
- Metal expansion should now be considered through automatic resolver substitution of generic CPU/SAR intents to Metal-capable implementations when Metal is available, but only where token contracts, sidecar preservation, and CPU-reference parity are proven.

Next Metal planning requirement:

- Future SAR updates must analyze which SAR nodes can be automatically substituted by existing Metal nodes when Metal is available, and which new general or SAR-specific Metal nodes/kernel descriptors would be required. Portable JSON should continue to express generic intents; backend-specific concrete nodes should be selected by resolver/capability policy and reported in diagnostics.

## PR6 Metal Auto-Substitution Audit

PR6 reviewed automatic Metal substitution against the current SAR graph path.

Current resolver behavior:

1. Generic-intent resolver policy exists for backend boundary families such as `H2DAsyncNode`, `D2HAsyncNode`, `DeviceTransformNode`, `DeviceKernelNode`, `DeviceReduceNode`, and `QueueSyncNode`.
2. Resolver policy can select Metal concrete types when the requested or automatic backend policy is `metal` and the corresponding concrete node type is available from the provider.
3. Fallback behavior is explicit through `backend_fallback_policy` and resolver diagnostics.
4. Current SAR user-facing JSON remains portable and uses generic SAR/example node names rather than concrete `*Metal` names.
5. The SAR example plugin path is example-scoped. It proves SAR plugin dynamic loading and the SAR backprojection adapter's native Metal delegation, but it does not by itself prove that every generic transfer intent is substituted with libgpu `*Metal` plugin nodes unless those concrete libgpu plugins are also available to the provider/bootstrap path.

PR6 substitution status:

| SAR stage | Current SAR node | Closest existing Metal node | Direct replacement viability | New Metal work needed | Ownership |
| --- | --- | --- | --- | --- | --- |
| Synthetic source | `SyntheticApertureIqSourceNode` | `HostIngressPinnedSourceNodeMetal` | No direct replacement | none for PR6; source owns SAR scene/sidecar generation | SAR-specific example node |
| Gotcha replay source | `GotchaReplaySourceNode` | `HostIngressPinnedSourceNodeMetal` | No direct replacement | normalized fixture/device-ingress adapter later | SAR-specific example node |
| Range window | `RangeWindowNode` | `DeviceTransformNodeMetal` | Partial/future | window kernel descriptor if moving stage to device | likely reusable `libdsp`/adapter |
| Range compression | `RangeCompressionNode` | `DeviceTransformNodeMetal` or `DeviceKernelNodeMetal` | Partial/future | matched-filter/FFT kernel descriptor after CPU parity | reusable `libdsp` primitive or SAR adapter |
| Pulse fanout | `SarPulseFanoutNode` | none | No direct replacement | none; graph fanout is topology/control-plane behavior | no Metal node needed |
| Azimuth tile split | `AzimuthTileSplitNode` | `DeviceShardNodeMetal` | Partial/future | sidecar-aware tile identity adapter if sharding moves device-side | SAR-specific adapter |
| H2D transfer | `H2DAsyncNode` | `H2DAsyncNodeMetal` | Viable when concrete Metal plugin is available | provider/bootstrap coverage for SAR example plugin path | general `libgpu` node |
| Backprojection | `SarBackprojectionTransformNode` | `DeviceKernelNodeMetal` | Implemented as adapter delegation | fuller SAR geometry kernel later, after CPU parity | SAR adapter over general `libgpu` node |
| D2H transfer | `D2HAsyncNode` | `D2HAsyncNodeMetal` | Viable when concrete Metal plugin is available | provider/bootstrap coverage for SAR example plugin path | general `libgpu` node |
| Tile merge | `ImageTileMergeNode` | `DeviceReduceNodeMetal` | Partial/future | device reduce/accumulation experiment after image parity | SAR-specific merge semantics plus possible general reduce |
| Visualization sink | `SarVisualizationSinkNode` | `HostEgressSinkNodeMetal` | Partial/future | real image-buffer egress if graph materializes image samples | SAR-specific display adapter |
| Diagnostics sink | `SarDiagnosticsSinkNode` | `HostEgressSinkNodeMetal` | No direct replacement | none; diagnostics are SAR sidecar/control-plane data | SAR-specific example node |

PR6 conclusion:

1. Automatic Metal substitution is complete enough for generic resolver infrastructure and native backprojection adapter evidence.
2. Automatic Metal substitution is not yet complete as a blanket SAR pipeline guarantee because SAR source, range/DSP, split/fanout, merge, visualization, and diagnostics stages carry SAR-specific semantics beyond generic Metal buffer movement.
3. Future Metal work should prioritize provider/bootstrap coverage for generic transfer substitutions, then device-side matched-filter/range-compression kernel descriptors only after runtime CPU/reference parity is stable.
4. Portable SAR JSON should continue to express generic intents and SAR-stage names. Concrete `*Metal` names should remain diagnostics output or backend-specific validation topology details.

## PR6 Completion Summary

PR6 implementation scope is complete for runtime matched-filter compression and the required Metal substitution audit.

Completed in this phase:

1. Added JSON-selectable runtime range-compression modes:
   - `fft_magnitude` remains the backward-compatible default,
   - `matched_filter` executes the PR5 CPU-reference-aligned matched-filter path.
2. Added runtime matched-filter configuration:
   - `mode`,
   - `output`,
   - `sample_rate_hz`,
   - `bandwidth_hz`,
   - `chirp_duration_s`,
   - `range_origin_m`,
   - `range_spacing_m`,
   - `gain`.
3. Added a PR6 JSON preset: `examples/SAR/config/sar_stripmap_pr6_matched_filter.json`.
4. Added runtime tests comparing `RangeCompressionNode` matched-filter output against the PR5 CPU reference.
5. Extended SAR benchmark generation and trace output with PR6 runtime matched-filter evidence:
   - runtime compression mode,
   - runtime matched-filter timing,
   - reference timing,
   - runtime-vs-reference L-infinity/RMS/relative-L2 error,
   - parity status.
6. Preserved GraphExecutor/JSON execution as the user-facing path and kept direct execution limited to reference/parity/benchmark attribution.
7. Preserved accel-token architecture and added no new GPU/Metal kernels.

Validation evidence:

1. `cmake --build build-ninja/ninja-debug --target test_sar_example_unit sar_benchmark`
2. `./build-ninja/ninja-debug/examples/SAR/test/test_sar_example_unit` passed 54/54 tests.
3. Focused PR6 test pass:
   - `RangeCompressionNodeTest.*`
   - PR6 matched-filter JSON execution,
   - PR6 matched-filter resolver metadata,
   - SAR trace schema.
4. `./build-ninja/ninja-debug/examples/SAR/sar_benchmark --profile=ci --range-stage=compression --trace-out /tmp/graphx_sar_pr6_trace.json` completed and emitted PR6 runtime matched-filter parity metrics.

Explicitly deferred beyond PR6:

1. Full image-sample graph/direct parity remains future work because the current public graph path still reports diagnostics and token lifecycle evidence rather than materialized image samples.
2. Device-side matched-filter/range-compression Metal kernels remain future work and require CPU-reference parity gates before implementation.
3. Provider/bootstrap changes that make libgpu Metal transfer plugins automatically available inside the SAR example plugin path remain a follow-up if blanket H2D/D2H `*Metal` substitution is required.
