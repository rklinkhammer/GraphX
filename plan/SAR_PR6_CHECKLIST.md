# SAR PR6 Checklist: Runtime Matched Filtering and Metal Substitution Audit

## Objective

Connect the PR5 matched-filter CPU reference contract to the runtime SAR range-compression path, tighten graph/direct parity from diagnostics-level equivalence toward algorithm/image-metric equivalence, and complete a required Metal auto-substitution gap analysis before adding new GPU kernels.

## Decision Table

| PR | Status | What is done | What remains | Decision |
| --- | --- | --- | --- | --- |
| PR1 | Complete | Deterministic SAR vertical slice, JSON topology, diagnostics contract, synthetic CI-safe benchmark | Merge flag is not explicitly updated in the checklist | Treat as foundation; no further PR1 scope changes |
| PR2 | Complete and merged | True fan-out, DSP stage, accel-token metadata alignment, trace export, DeviceReduce evaluation | None in scope for current PR line | Closed; use as graph-contract baseline |
| PR3 | Complete, ready for review | Native backend path, FFT-backed range compression, resolver controls, Metal lane completion | Merge and any post-review cleanup | Keep stable; do not widen scope |
| PR4 | Complete | CPU reference correctness foundation, image-quality metrics, parity hooks | Broader image-sample parity remains deferred | Use as correctness reference layer |
| PR5 | Complete | Runtime matched-filter CPU reference, deterministic fidelity metrics, benchmark trace extensions | Native-kernel expansion still deferred | Use as fidelity contract for PR6 |
| PR6 | Complete | Runtime matched-filter path, Metal substitution audit, graph/direct parity target | Full materialized image parity and device-side matched-filter Metal kernels are intentionally deferred to future PRs | Stay audit/parity focused; do not add new kernels without accepted parity evidence |

Recommendation: keep PR6 narrowly scoped to parity, audit, and contract tightening. Do not add new GPU kernels until the Metal substitution matrix and CPU-reference parity requirements are accepted.

## Implementation Status

- [x] Runtime `RangeCompressionNode` supports JSON-selectable matched-filter mode.
- [x] Existing FFT magnitude behavior remains the default compatibility mode.
- [x] Runtime matched-filter output is tested against the PR5 CPU reference fixture.
- [x] PR6 matched-filter JSON preset executes through `GraphExecutorBuilder` and the example-scoped plugin path.
- [x] Benchmark trace emits PR6 runtime matched-filter fields and preserves PR4/PR5 attribution fields.
- [x] Metal auto-substitution audit and SAR-to-Metal node matrix are documented in `plan/SAR.md`.
- [x] No new GPU/Metal kernels were added.
- [x] Full SAR unit target passes.
- [x] Full materialized image-sample graph/direct parity remains deferred because the current public graph path emits diagnostics/token lifecycle evidence rather than image buffers.
- [x] Device-side matched-filter/range-compression Metal kernels remain deferred until CPU-reference parity requirements are accepted.

## Scope

- [x] Update runtime `RangeCompressionNode` behavior or mode selection so the SAR compression path can execute a matched-filter-aware implementation.
- [x] Preserve backward compatibility for existing JSON presets where needed:
  - [x] default/legacy FFT magnitude mode remains available or migration is documented,
  - [x] matched-filter mode is selectable from JSON,
  - [x] config validation rejects incomplete matched-filter parameters.
- [x] Add runtime config fields for deterministic matched-filter operation:
  - [x] sample rate,
  - [x] bandwidth,
  - [x] chirp duration or chirp sample count,
  - [x] range origin/spacing where needed,
  - [x] gain/normalization policy,
  - [x] output representation: complex-preserving reference, magnitude-only compatibility, or both.
- [x] Reuse PR5 CPU reference utilities as the correctness target.
- [x] Add known-vector runtime tests comparing `RangeCompressionNode` output to the PR5 reference.
- [x] Add graph/direct parity checks that include algorithm or image-quality metrics, not only diagnostics counters.
- [x] Extend benchmark trace with PR6 runtime matched-filter fields:
  - [x] runtime compression mode,
  - [x] matched-filter parameter block,
  - [x] reference-vs-runtime error metrics,
  - [x] image metric deltas,
  - [x] compression-stage timing,
  - [x] graph/direct parity status.
- [x] Keep implementation in `examples/SAR` unless a reusable `libdsp` extraction is justified by a second non-SAR use case.

## GraphExecutor / JSON Contract

- [x] Preserve `examples/SAR/src/main.cpp` as the canonical SAR example entrypoint.
- [x] Keep user-facing SAR examples, benchmarks, and integration paths driven by `GraphExecutorBuilder` plus JSON topology/config files.
- [x] Update or add JSON presets under `examples/SAR/config` for matched-filter runtime validation.
- [x] Ensure all new or changed runtime node behavior is configurable from JSON.
- [x] Ensure plugin registration and dynamic loading still cover all changed runtime nodes.
- [x] Add or update at least one GraphExecutor-driven test that exercises the PR6 matched-filter mode.
- [x] Limit direct/non-graph execution to CPU reference, parity, graph-overhead attribution, or focused unit tests.

## Accel-Token Guardrails

- [x] No raw SAR payload contracts across transfer/kernel graph edges.
- [x] Preserve `edge_contract: "accel-token"` for PR3/native/resolved SAR presets.
- [x] Keep transfer/kernel stages using `graph::gpu::accel` token semantics:
  - [x] `HostPinnedBufferView`,
  - [x] `DeviceBufferView`,
  - [x] `BufferLease`,
  - [x] `TransferTicket`,
  - [x] `KernelTicket`.
- [x] Preserve SAR identity as sidecar metadata:
  - [x] `sequence_id`,
  - [x] `batch_id`,
  - [x] `aperture_id`,
  - [x] `pulse_range_start`,
  - [x] `pulse_range_count`,
  - [x] `stream_id`,
  - [x] `tile_id`,
  - [x] `tile_count`,
  - [x] EOS/watermark marker,
  - [x] backend/device/queue ids.
- [x] Keep benchmark transfer payload bytes separate from graph edge copy overhead.
- [x] Keep `token_edge_payload_copies` at zero for accel-token traces.
- [x] Preserve parser/schema rejection of legacy SAR payload contracts under accel-token mode.

## Mandatory Metal Auto-Substitution Audit

- [x] Inspect resolver/provider code for automatic generic-intent substitution when Metal is available.
- [x] Verify how `execution_backend`, `backend_fallback_policy`, `resolver_diagnostics`, and `edge_contract` are interpreted.
- [x] Verify whether Metal is selected automatically when available under the current auto policy.
- [x] Verify fallback behavior is deterministic and observable in diagnostics.
- [x] Verify trace output reports:
  - [x] intent type,
  - [x] concrete type,
  - [x] selected backend,
  - [x] fallback reason,
  - [x] input token type,
  - [x] output token type.
- [x] Confirm portable SAR JSON presets use generic intents rather than concrete `*Metal` names.
- [x] Confirm backend-specific validation topologies are the only acceptable place for concrete `*Metal` node names.

## SAR-to-Metal Node Gap Matrix

| SAR stage | Current graph contract | Closest existing Metal node | Direct replacement viability | Required new Metal node or descriptor | Ownership decision | CPU/reference parity test | Sidecar / accel-token risk |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `SyntheticApertureIqSourceNode` | Host-side source/replay emits pulse blocks and EOS with SAR sidecars | `HostIngressPinnedSourceNodeMetal` (staging analog only) | No direct Metal replacement needed; keep SAR-specific source adapter | No new Metal node; direct raw Gotcha reader can remain in `examples/SAR` | `examples/SAR` | Fixture replay + source determinism | High format/legal risk for raw data; low accel-token risk |
| `RangeWindowNode` | Deterministic sample-wise preprocessing over host or resolved tokens | `DeviceTransformNodeMetal` | Yes for a generic elementwise transform when Metal is available | Optional SAR window descriptor if a kernel uses backend-specific parameters | `libgpu` preferred for reusable transforms; SAR-specific Metal wrapper may live in `examples/SAR` when only the SAR path needs it | Window correctness and sidelobe-behavior test | Medium risk if phase/layout metadata is dropped |
| `RangeCompressionNode` | PR6 matched-filter / FFT-backed compression with reference metrics | `DeviceKernelNodeMetal` | Partial; runtime substitution is plausible, but CPU-reference parity remains the gate | SAR compression kernel descriptor or `libdsp`-backed reusable primitive, depending on reuse evidence | `examples/SAR` for SAR-specific Metal adapters; promote to `libdsp` or `libgpu` only if reuse evidence appears | PR5 CPU reference parity + known-vector runtime test | High risk if complex phase or normalization changes |
| `AzimuthTileSplitNode` | Graph-visible branch sharding and tile identity assignment | `DeviceShardNodeMetal` (closest analog) | No direct Metal replacement; split is graph-level routing, not a device kernel | No new Metal node required; preserve as SAR graph adapter | `examples/SAR` | Tile identity and branch fan-out tests | Low token risk, medium contract risk |
| `SarPulseFanoutNode` | Branch-level fan-out from one pulse block into multiple tiles | `SplitNode4` / `DeviceShardNodeMetal` | No direct Metal replacement needed | No new Metal node required | `examples/SAR` | Branch count and fan-in ordering tests | Low |
| `H2DAsyncNode` | Host token -> device view/lease/ticket boundary | `H2DAsyncNodeMetal` | Yes, direct replacement viable when Metal is available | No new node; ensure resolver maps generic intent cleanly | `libgpu` preferred; SAR-owned Metal wrapper is acceptable if it stays example-scoped | H2D parity + ticket/lease preservation | Medium risk if lease/ticket metadata is dropped |
| `SarBackprojectionTransformNode` | Device tile backprojection work unit preserving SAR sidecars | `DeviceKernelNodeMetal` | Yes, direct replacement viable for native-device mode | SAR kernel descriptor / inline source payload for the native path | `examples/SAR` is the right home for SAR-specific Metal nodes/adapters; `libgpu` should only host reusable generic kernels | PR4/PR5 CPU reference backprojection + image metrics | High risk if geometry, units, or sidecars diverge |
| `D2HAsyncNode` | Device view -> host view/lease/ticket boundary | `D2HAsyncNodeMetal` | Yes, direct replacement viable when Metal is available | No new node; keep resolver substitution deterministic | `libgpu` preferred; SAR-owned Metal wrapper is acceptable if example-scoped | D2H parity + ticket/lease preservation | Medium risk if host-view metadata is lost |
| `ImageTileMergeNode` | Host-side fan-in, duplicate/missing/out-of-order handling, diagnostics emission | `DeviceReduceNodeMetal` / `HostEgressSinkNodeMetal` | No direct replacement for the correctness path; any Metal reduce is a future optimization only | No new node for PR6; possible future reduce descriptor if on-device accumulation is justified | `examples/SAR` for SAR merge semantics; a Metal helper may stay example-scoped if it is not reusable | Merge correctness matrix + diagnostics parity | Medium-high risk if correctness semantics move on-device too early |
| `SarVisualizationSinkNode` | Artifact generation and visual inspection sink | `HostEgressSinkNodeMetal` (sink analog only) | No direct replacement needed | No new node required | `examples/SAR` | Artifact determinism and output hash checks | Low |
| `SarDiagnosticsSinkNode` | Metrics / trace / completion sink with queue and latency fields | `HostEgressSinkNodeMetal` / `QueueSyncNodeMetal` | No direct replacement needed | No new node required | `examples/SAR` | Diagnostics contract and trace schema tests | Low |
| `GotchaReplaySourceNode` | Fixture-based normalized replay source for real-data-like ingestion | `HostIngressPinnedSourceNodeMetal` (staging analog only) | No direct Metal replacement; keep it as an ingest adapter | No new node required for PR6; direct raw reader remains a later example-scoped adapter | `examples/SAR` | Replay fixture parsing + end-to-end graph run | High format/legal risk for raw datasets; low accel-token risk |

Summary: direct Metal substitution is most compelling for transfer and device-kernel boundary stages, but SAR-specific Metal nodes or wrappers may remain under `examples/SAR` when that keeps the implementation scoped to SAR and avoids forcing a generic `libgpu` home prematurely. Source, fan-out, merge, visualization, diagnostics, and Gotcha replay should remain `examples/SAR` adapters unless a later PR proves they belong in a reusable library.

- [x] Produce and store a SAR-to-Metal node matrix in `plan/SAR.md` or a PR6 analysis note.
- [x] Include each SAR runtime stage:
  - [x] `SyntheticApertureIqSourceNode`,
  - [x] `RangeWindowNode`,
  - [x] `RangeCompressionNode`,
  - [x] `AzimuthTileSplitNode`,
  - [x] `SarPulseFanoutNode`,
  - [x] `H2DAsyncNode`,
  - [x] `SarBackprojectionTransformNode`,
  - [x] `D2HAsyncNode`,
  - [x] `ImageTileMergeNode`,
  - [x] `SarVisualizationSinkNode`,
  - [x] `SarDiagnosticsSinkNode`,
  - [x] `GotchaReplaySourceNode`.
- [x] For each stage, classify:
  - [x] current graph contract,
  - [x] closest existing Metal node,
  - [x] direct replacement viability,
  - [x] required new Metal node or kernel descriptor,
  - [x] ownership: general `libgpu`, reusable `libdsp`, SAR-specific adapter under `examples/SAR`, or no Metal node needed,
  - [x] required CPU-reference parity test,
  - [x] sidecar/accel-token risk.
- [x] Explicitly evaluate existing Metal node families:
  - [x] `HostIngressPinnedSourceNodeMetal`,
  - [x] `H2DAsyncNodeMetal`,
  - [x] `DeviceShardNodeMetal`,
  - [x] `DeviceTransformNodeMetal`,
  - [x] `DeviceKernelNodeMetal`,
  - [x] `DeviceReduceNodeMetal`,
  - [x] `D2HAsyncNodeMetal`,
  - [x] `HostEgressSinkNodeMetal`,
  - [x] `QueueSyncNodeMetal`,
  - [x] `LeaseReleaseNodeMetal`,
  - [x] `PeerCopyNodeMetal`,
  - [x] `CollectiveReduceNodeMetal`.

## New Metal Development Candidates

- [x] Identify all Metal nodes or kernel descriptors needed for corresponding SAR stages.
- [x] Classify each candidate as:
  - [x] general `libgpu` node,
  - [x] reusable `libdsp` accelerated primitive,
  - [x] SAR-specific adapter under `examples/SAR`,
  - [x] no new node needed because existing generic Metal node is sufficient.
- [x] Do not add new GPU kernels in PR6 unless explicitly approved after the audit.
- [x] Define CPU-reference parity requirements for any future Metal candidate before implementation.
- [x] Require sidecar preservation tests for any future Metal substitution or adapter.

## Performance and Attribution

- [x] Preserve existing benchmark separation:
  - [x] graph build time,
  - [x] graph run time,
  - [x] graph lifecycle total time,
  - [x] baseline non-graph execution time,
  - [x] provider/plugin lookup attribution,
  - [x] transfer payload bytes,
  - [x] token-edge copy count,
  - [x] diagnostics cost bucket.
- [x] Add or refine PR6 matched-filter cost fields:
  - [x] runtime compression time,
  - [x] CPU reference compression time,
  - [x] runtime-vs-reference error metrics,
  - [x] graph/direct metric deltas,
  - [x] image metric calculation time.
- [x] Do not claim performance improvement without identifying the bottleneck and measurement proving it.
  - Measured bottleneck in the current benchmark run is graph lifecycle join teardown, while graph_run versus baseline remains within the observed noise band.

## Display and Artifacts

- [x] Keep CI artifacts lightweight and deterministic.
- [x] Add or update static magnitude/log-magnitude artifact generation only if it supports PR6 image-metric validation.
- [x] Ensure generated artifacts are not required for correctness unless tests validate them deterministically.
- [x] Do not let display/data-ingestion concerns alter GraphX internal contracts.

## Non-Goals

- No large external datasets in CI.
- No raw AFRL Gotcha `.mat` ingestion.
- No SAR-specific abstractions in `libgraph`.
- No framework-wide scheduler or resolver redesign.
- No replacement of the GraphExecutor/JSON runtime path with a direct pipeline.
- No new production GPU/Metal kernels unless separately approved after the PR6 Metal gap audit.

## Acceptance Criteria

- [x] Full SAR unit target passes.
- [x] Runtime matched-filter mode matches PR5 CPU reference within explicit tolerances.
- [x] At least one JSON/GraphExecutor SAR pipeline exercises runtime matched-filter mode.
- [x] Graph/direct parity includes algorithm or image-quality metrics.
- [x] Benchmark trace emits PR6 runtime matched-filter fields and preserves PR4/PR5 fields.
- [x] PR3 accel-token topology validation still rejects legacy payload-envelope transfer/kernel edges.
- [x] Metal auto-substitution audit is complete and identifies:
  - [x] substitutions that already work,
  - [x] substitutions that are missing,
  - [x] SAR-specific adapters that should remain under `examples/SAR`,
  - [x] general Metal nodes/kernel descriptors that should be considered in future PRs.
- [x] If native Metal is available locally, existing native backprojection adapter parity remains within tolerance.

## Follow-Up Candidates

- Add a normalized tiny Gotcha fixture after PR6 establishes runtime matched-filter parity.
- Add PNG/log-magnitude and heatmap comparison tools for local inspection.
- Promote matched-filter helpers to `libdsp` only after a second non-SAR use case exists.
- Implement native Metal range compression or fuller backprojection kernels only after the PR6 audit and CPU-reference parity requirements are accepted.
