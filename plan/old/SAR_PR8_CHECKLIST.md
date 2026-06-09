# SAR PR8 Checklist: Sidecar Contract Hardening, Attribution Discipline, and External-Data Readiness

## Objective

Advance SAR after PR7 by closing the remaining architecture and validation gaps that were intentionally deferred:

1. Make accel-token sidecar identity checks explicit and exhaustive across transfer + kernel boundaries.
2. Enforce performance-attribution discipline so benchmark claims are evidence-backed and contract-safe.
3. Establish an external-data (non-CI) validation lane while keeping CI deterministic and lightweight.

PR8 does not introduce native matched-filter/range-compression Metal kernels unless a new parity gate decision is explicitly opened in a separate checklist update.

## Inputs and Context

- plan/SAR.md
- plan/sar_accuracy_fidelity_performance_prompt.md
- plan/SAR_PR1_CHECKLIST.md
- plan/SAR_PR2_CHECKLIST.md
- plan/SAR_PR3_CHECKLIST.md
- plan/SAR_PR4_CHECKLIST.md
- plan/SAR_PR5_CHECKLIST.md
- plan/SAR_PR6_CHECKLIST.md
- plan/SAR_PR7_CHECKLIST.md
- plan/pr_checklist.md

## Carry-Forward from PR7

- [x] Confirm edge_contract remains accel-token across all active SAR JSON presets.
- [x] Add sidecar identity coverage for all key fields through H2D -> kernel -> D2H -> merge.
- [x] Keep benchmark/trace contract backward compatible while extending attribution checks.
- [x] Keep native matched-filter/range-compression kernels deferred unless a new parity gate is approved.

## Scope

- [x] Add sidecar identity regression tests for:
  - [x] sequence_id,
  - [x] batch_id,
  - [x] aperture_id,
  - [x] pulse_range_start,
  - [x] pulse_range_count,
  - [x] stream_id,
  - [x] tile_id,
  - [x] tile_count,
  - [x] EOS/watermark marker,
  - [x] backend/device/queue identifiers.
- [x] Add JSON preset audit tests to ensure portable intent + resolver behavior remains intact.
- [x] Add benchmark-claim guardrail tests that fail when attribution evidence is missing.
- [x] Add non-CI external dataset lane scaffolding (fixture adapter + local/manual execution docs).
- [x] Keep SAR-specific adapters in examples/SAR unless a second consumer justifies promotion.

## Initial Execution Order (First PR8 Slice)

- [x] Slice 2.1: Sidecar identity matrix hardening.
  - [x] Expand existing accel-token/sidecar tests to cover all required identity fields.
  - [x] Validate identity under fanout and merge paths.
- [x] Slice 2.2: Preset contract audit.
  - [x] Verify edge_contract=accel-token in all maintained SAR presets.
  - [x] Verify resolver intent/concrete metadata remains portable and explicit.
- [x] Slice 2.3: Attribution discipline checks.
  - [x] Add assertions that performance claims reference bottleneck-attribution fields.
  - [x] Ensure no claim is made from lifecycle totals alone.
- [x] Slice 2.4: External-data non-CI lane.
  - [x] Define local/manual Gotcha-style replay lane constraints.
  - [x] Keep CI path deterministic and lightweight with tiny fixtures only.

## Risk Ranking and Mitigations

- [x] High: Sidecar regressions can silently break SAR identity fidelity.
  - [x] Mitigation: field-by-field propagation tests on every critical topology.
- [x] High: Performance claims may regress into non-attributed summaries.
  - [x] Mitigation: enforce attribution-key presence and claim policy in tests/docs.
- [x] Medium: External dataset lane can leak heavyweight dependencies into CI.
  - [x] Mitigation: strict CI/non-CI separation with explicit guards.
- [x] Low: Ownership creep from examples/SAR into libgraph/libgpu.
  - [x] Mitigation: require second-use-case evidence before promotion.

## GraphExecutor / JSON Contract

- [x] Does this PR preserve examples/SAR/src/main.cpp as the canonical entrypoint?
- [x] Are all new or changed nodes usable from JSON config?
- [x] Are plugin registration and dynamic loading covered?
- [x] Were examples/SAR/config/*.json files updated or explicitly validated?
- [x] Does at least one GraphExecutor-driven test or benchmark exercise the change?
- [x] Is any direct/non-graph path limited to baseline or parity measurement?

## Accel-Token Guardrails

- [x] No raw SAR payload contracts across transfer/kernel graph edges.
- [x] edge_contract remains accel-token for PR3/PR6/PR7/PR8 SAR presets.
- [x] Token-edge payload copies remain zero in benchmark trace.
- [x] Sidecar identity remains intact through transfer + kernel boundaries.

## Performance and Attribution

- [x] Preserve benchmark separation fields:
  - [x] graph build time,
  - [x] graph run time,
  - [x] graph lifecycle total time,
  - [x] baseline non-graph execution time,
  - [x] transfer payload bytes,
  - [x] token-edge copy count,
  - [x] diagnostics cost bucket.
- [x] Preserve PR6/PR7 trace compatibility fields and parity status.
- [x] Do not claim performance gain without bottleneck attribution and measured evidence.

## External Data Readiness (Non-CI)

- [x] Keep CI fixtures lightweight/deterministic.
- [x] Keep AFRL raw data ingestion out of CI.
- [x] Define non-CI path for larger dataset validation (local/manual benchmark lane).

## Non-Goals

- No framework-wide scheduler redesign.
- No SAR-specific abstractions promoted to shared libraries without reuse proof.
- No replacement of GraphExecutor/JSON with direct-only pipelines.
- No native matched-filter/range-compression Metal kernel rollout in this checklist scope.

## Acceptance Criteria

- [x] Full SAR unit target passes.
- [x] Sidecar identity matrix passes on baseline + fanout + PR7 materialized topologies.
- [x] Accel-token/JSON resolver guardrails remain green.
- [x] Trace schema remains backward compatible with PR6/PR7 fields.
- [x] Attribution policy checks pass for benchmark reporting.
- [x] External-data lane documented and executable outside CI.
