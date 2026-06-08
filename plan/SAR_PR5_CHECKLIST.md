# SAR PR5 Checklist: Matched-Filter Range Compression and Image Metrics

## Objective

Improve SAR mathematical credibility after PR4 by adding matched-filter-aware range-compression reference behavior, deterministic image-quality metrics, and GraphExecutor/JSON validation without adding new GPU kernels.

## Scope

- [x] Add CPU reference utilities for matched-filter range compression under `examples/SAR`.
- [x] Define deterministic chirp/reference parameters:
  - [x] sample rate
  - [x] bandwidth
  - [x] chirp duration or sample count
  - [x] range origin/spacing
  - [x] carrier/wavelength where needed by downstream tests
- [x] Add a tiny known-vector matched-filter fixture.
- [x] Preserve complex phase in the reference path where required for backprojection validation.
- [x] Add image-quality metrics:
  - [x] peak location error
  - [x] peak value error
  - [x] impulse response width
  - [x] peak sidelobe ratio
  - [x] integrated sidelobe ratio
  - [x] dynamic range or log-magnitude range
  - [x] deterministic metadata/image hash
- [x] Extend CPU reference tests beyond the PR4 single point-target fixture:
  - [x] off-grid point target
  - [x] two point targets with deterministic relative reflectivity
  - [x] matched-filter known-vector tolerance test
- [x] Update or add graph/direct parity tests so parity covers algorithm/image metrics, not only diagnostics counters.
- [x] Extend benchmark trace with PR5 accuracy/fidelity fields while preserving PR4 cost buckets.
- [x] Keep any new algorithm code in `examples/SAR` unless a reusable `libdsp` extraction is justified by non-SAR use.

## GraphExecutor / JSON Contract

- [x] Preserve `examples/SAR/src/main.cpp` as the canonical SAR example entrypoint.
- [x] Keep user-facing SAR examples, benchmarks, and integration paths driven by `GraphExecutorBuilder` plus JSON topology/config files.
- [x] Ensure all new or changed runtime nodes are usable from JSON config.
- [x] Ensure plugin registration and dynamic loading are covered for all new runtime nodes.
- [x] Update or explicitly validate affected files under `examples/SAR/config/*.json`.
- [x] Add or update at least one GraphExecutor-driven test or benchmark that exercises the PR5 change.
- [x] Limit direct/non-graph paths to CPU reference, parity, graph-overhead attribution, or focused unit tests.

## Accel-Token Guardrails

- [x] No raw SAR payload contracts across transfer/kernel graph edges.
- [x] Preserve `edge_contract: "accel-token"` for PR3/native/resolved SAR presets.
- [x] Keep transfer/kernel stages using `graph::gpu::accel` token semantics:
  - [x] `HostPinnedBufferView`
  - [x] `DeviceBufferView`
  - [x] `BufferLease`
  - [x] `TransferTicket`
  - [x] `KernelTicket`
- [x] Preserve SAR identity as sidecar metadata:
  - [x] `sequence_id`
  - [x] `batch_id`
  - [x] `aperture_id`
  - [x] `pulse_range_start`
  - [x] `pulse_range_count`
  - [x] `stream_id`
  - [x] `tile_id`
  - [x] `tile_count`
  - [x] EOS/watermark marker
  - [x] backend/device/queue ids
- [x] Keep benchmark transfer payload bytes separate from graph edge copy overhead.
- [x] Keep `token_edge_payload_copies` at zero for accel-token traces.
- [x] Preserve parser/schema rejection of legacy SAR payload contracts under accel-token mode.

## Performance and Attribution

- [x] Preserve existing benchmark separation:
  - [x] graph build time
  - [x] graph run time
  - [x] graph lifecycle total time
  - [x] baseline non-graph execution time
  - [x] provider/plugin lookup attribution
  - [x] transfer payload bytes
  - [x] token-edge copy count
  - [x] diagnostics cost bucket
- [x] Add or refine PR5 cost/metric fields:
  - [x] range-compression reference time
  - [x] matched-filter vector length
  - [x] image metric calculation time if measurable
  - [x] graph-vs-direct image metric deltas
- [x] Do not claim performance improvement without identifying the bottleneck and measurement proving it.

## Display and Artifact Follow-Up

- [x] Keep CI artifacts lightweight and deterministic.
- [x] Add or plan static magnitude/log-magnitude artifact output if image metrics need visual inspection.
- [x] Ensure generated artifacts are not required for correctness unless tests validate them deterministically.
- [x] Do not let display/data-ingestion concerns alter GraphX internal contracts.

## Non-Goals

- No new production GPU or Metal kernels.
- No raw AFRL Gotcha `.mat` ingestion in this PR.
- No large external datasets in CI.
- No SAR-specific abstractions in `libgraph`.
- No framework-wide scheduler or resolver redesign.
- No replacement of the GraphExecutor/JSON runtime path with a direct pipeline.

## Acceptance Criteria

- [x] Full SAR unit target passes, including PR4 CPU reference and native Metal adapter parity where available.
- [x] A matched-filter known-vector test passes with explicit tolerances.
- [x] Point-target image metrics pass with deterministic peak and hash expectations.
- [x] GraphExecutor/JSON SAR pipeline still runs through the example-scoped plugin directory.
- [x] Graph/direct parity includes image or algorithm metrics, not only diagnostics counters.
- [x] PR3 accel-token topology validation still rejects legacy payload-envelope transfer/kernel edges.
- [x] `sar_benchmark --profile=ci --range-stage=compression --trace-out <file>` emits PR4 cost buckets plus PR5 accuracy/fidelity metrics.
- [x] If native Metal is enabled locally, `sar_benchmark --profile=ci --range-stage=compression --native-backend --trace-out <file>` still emits native execution evidence and CPU-reference parity remains within tolerance.

## Follow-Up Candidates

- Add a normalized tiny Gotcha fixture path after PR5 establishes matched-filter and image-metric contracts.
- Add PNG/log-magnitude and heatmap comparison tools for local inspection.
- Promote reusable matched-filter helpers to `libdsp` only after a second non-SAR use case exists.
- Consider native Metal range-compression or fuller backprojection kernels only after PR5 reference parity is stable.
