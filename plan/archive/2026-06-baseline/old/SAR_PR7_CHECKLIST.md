# SAR PR7 Checklist: Materialized Image Parity and Native Kernel Expansion Gate

## Objective

Carry SAR forward from PR6 by addressing the two intentionally deferred items:

1. Full materialized image-sample graph/direct parity.
2. Device-side matched-filter/range-compression Metal kernel path, gated by accepted CPU-reference parity and sidecar preservation.

PR7 must preserve the PR6 accel-token architecture and GraphExecutor + JSON runtime contract.

## Inputs and Context

- plan/SAR.md
- plan/sar_accuracy_fidelity_performance_prompt.md
- plan/SAR_PR1_CHECKLIST.md
- plan/SAR_PR2_CHECKLIST.md
- plan/SAR_PR3_CHECKLIST.md
- plan/SAR_PR4_CHECKLIST.md
- plan/SAR_PR5_CHECKLIST.md
- plan/SAR_PR6_CHECKLIST.md
- plan/pr_checklist.md

## Carry-Forward from PR6

- [x] Implement full materialized image-sample graph/direct parity path (not diagnostics-only parity).
- [x] Implement or explicitly reject device-side matched-filter/range-compression Metal kernels based on parity evidence.
- [x] Keep PR6 benchmark/trace contracts intact while expanding parity depth.

## Scope

- [x] Add a graph path that exposes deterministic image buffers suitable for direct graph-vs-reference image comparison.
- [x] Add image-sample parity tests with explicit tolerances:
  - [x] L-infinity,
  - [x] RMS,
  - [x] relative L2,
  - [x] peak-location error,
  - [x] dynamic-range delta.
- [ ] If native range-compression kernel work is attempted:
  - [ ] define CPU reference as source of truth,
  - [ ] add deterministic fixture parity tests,
  - [ ] preserve SAR sidecar identity,
  - [ ] preserve accel-token edge contracts,
  - [ ] block rollout when parity fails.
- [x] Keep portable JSON presets on generic intents; resolver performs backend substitution.
- [x] Keep SAR-specific adapters in examples/SAR unless a second non-SAR use case justifies promotion.

## Initial Execution Order (First PR7 Slice)

- [x] Slice 1.1: Lock deterministic parity fixtures and golden outputs.
  - [x] Finalize a small deterministic scene set for CI.
  - [x] Capture or regenerate reference image buffers from CPU baseline path.
  - [x] Record tolerance defaults in test helper constants.
- [x] Slice 1.2: Expose materialized image buffers in GraphExecutor topology.
  - [x] Add or wire a graph node/output path that emits deterministic image samples.
  - [x] Ensure JSON presets can enable this path without breaking existing presets.
  - [x] Keep direct path only for baseline/parity comparison.
- [x] Slice 1.3: Add image-sample parity test harness.
  - [x] Implement metric computation for L-infinity, RMS, relative L2, peak-location error, dynamic-range delta.
  - [x] Add GraphExecutor-driven tests that compare graph output to reference output.
  - [x] Fail tests on any tolerance breach and emit metric values in failure output.
- [x] Slice 1.4: Preserve PR6 trace compatibility.
  - [x] Confirm required PR6 trace fields are still emitted.
  - [x] Add/extend schema assertions only if new PR7 fields are introduced.
- [x] Slice 1.5: Gate decision for native matched-filter/range-compression kernels.
  - [x] Run parity evidence review with current CPU-reference fixtures.
  - [x] Decide include/defer and document decision before kernel coding begins.

## Risk Ranking and Mitigations

- [x] High: Numeric drift can cause flaky parity on different GPU/driver combinations.
  - [x] Mitigation: deterministic fixtures, fixed seeds, stable tolerance constants, metric printouts.
- [ ] High: Materialized image path may accidentally bypass accel-token/sidecar invariants.
  - [ ] Mitigation: explicitly route through existing token-edge contracts and keep sidecar checks in tests.
- [x] High: JSON preset changes may regress existing GraphExecutor scenarios.
  - [x] Mitigation: additive JSON fields only, backward-compatible defaults, run full SAR unit target.
- [ ] Medium: New parity metrics may increase test runtime and CI noise.
  - [ ] Mitigation: small fixture sizes for CI and optional larger local benchmark lane.
- [x] Medium: Native kernel decision may be made without enough evidence.
  - [x] Mitigation: require parity summary table and contract/trace check results before decision.
- [ ] Low: Ownership boundaries may drift into libgraph prematurely.
  - [ ] Mitigation: keep SAR-specific adapters under examples/SAR unless a second consumer appears.

## GraphExecutor / JSON Contract

- [x] Does this PR preserve examples/SAR/src/main.cpp as the canonical entrypoint?
- [x] Are all new or changed nodes usable from JSON config?
- [x] Are plugin registration and dynamic loading covered?
- [x] Were examples/SAR/config/*.json files updated or explicitly validated?
- [x] Does at least one GraphExecutor-driven test or benchmark exercise the change?
- [x] Is any direct/non-graph path limited to baseline or parity measurement?

## Accel-Token Guardrails

- [x] No raw SAR payload contracts across transfer/kernel graph edges.
- [ ] edge_contract remains accel-token for PR3/PR6/PR7 SAR presets.
- [x] Token-edge payload copies remain zero in benchmark trace.
- [ ] Sidecar identity remains intact through transfer + kernel boundaries:
  - [ ] sequence_id,
  - [ ] batch_id,
  - [ ] aperture_id,
  - [ ] pulse_range_start,
  - [ ] pulse_range_count,
  - [ ] stream_id,
  - [ ] tile_id,
  - [ ] tile_count,
  - [ ] EOS/watermark marker,
  - [ ] backend/device/queue ids.

## Accuracy and Fidelity

- [x] Expand deterministic scene fixtures for image-sample comparison.
- [x] Add graph/direct image parity tests beyond diagnostics counters.
- [x] Maintain PR5/PR6 matched-filter known-vector checks.
- [x] Maintain native backprojection parity tolerance checks.

## Native Metal Expansion Gate

- [x] Decide whether PR7 includes native matched-filter/range-compression kernels.
- [ ] If yes, require all of:
  - [ ] CPU-reference parity pass,
  - [ ] sidecar preservation pass,
  - [ ] accel-token contract pass,
  - [ ] trace schema compatibility pass.
- [x] If no, document explicit defer decision with rationale in SAR.md.

## Performance and Attribution

- [x] Preserve benchmark separation fields:
  - [x] graph build time,
  - [x] graph run time,
  - [x] graph lifecycle total time,
  - [x] baseline non-graph execution time,
  - [x] transfer payload bytes,
  - [x] token-edge copy count,
  - [x] diagnostics cost bucket.
- [x] Preserve PR6 runtime matched-filter trace fields and parity status.
- [ ] Do not claim performance gain without bottleneck attribution and measured evidence.

## External Data Readiness (Non-CI)

- [ ] Keep CI fixtures lightweight/deterministic.
- [ ] Keep AFRL raw data ingestion out of CI.
- [ ] Define non-CI path for larger dataset validation (local/manual benchmark lane).

## Non-Goals

- No framework-wide scheduler redesign.
- No SAR-specific abstractions in libgraph.
- No replacement of GraphExecutor/JSON with direct-only pipelines.

## Acceptance Criteria

- [x] Full SAR unit target passes.
- [x] At least one GraphExecutor SAR topology emits materialized image buffers for parity checks.
- [x] Graph/direct image parity metrics pass explicit tolerances.
- [x] Accel-token and sidecar guardrails remain green.
- [x] Trace schema remains backward compatible with PR6 fields.
- [x] If native matched-filter kernels are introduced, parity and contract gates pass; otherwise defer decision is documented.
