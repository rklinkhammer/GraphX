# SAR PR9 Checklist: Real Image Materialization and End-to-End Parity Hardening

Status:

- [x] PR9 started
- [x] PR9 implementation complete
- [x] PR9 ready for review
- [ ] PR9 merged

## Objective

Replace surrogate graph-output image materialization with real image extraction from accel-token host views, then harden end-to-end parity checks and preserve resolver/accel-token contract discipline.

## Scope

- [x] Replace deterministic surrogate output path in `SarMaterializedImageSinkNode` with real host payload extraction.
- [x] Preserve SAR sidecar identity contract at output boundary.
- [x] Add end-to-end graph-output image parity coverage for maintained SAR JSON paths (PR6/PR7 lanes).
- [x] Keep resolver-driven generic intents and backend substitution behavior unchanged unless required for parity correctness.
- [x] Preserve benchmark attribution-policy schema and compatibility fields.

## Mandatory GraphExecutor / JSON Contract (from plan/pr_checklist.md)

- [x] Does this PR preserve `examples/SAR/src/main.cpp` as the canonical entrypoint?
- [x] Are all new or changed nodes usable from JSON config?
- [x] Are plugin registration and dynamic loading covered?
- [x] Were `examples/SAR/config/*.json` files updated or explicitly validated?
- [x] Does at least one GraphExecutor-driven test or benchmark exercise the change?
- [x] Is any direct/non-graph path limited to baseline or parity measurement?

## Accel-Token and Sidecar Guardrails

- [x] No legacy SAR payload-edge contracts are introduced under `edge_contract: "accel-token"`.
- [x] Transfer/kernel boundaries continue using accel token/view/ticket semantics.
- [x] Output-stage materialization does not reinterpret graph edge payload semantics as raw message copies.
- [x] Sidecar fields are preserved and asserted across EOS and non-EOS paths:
- [x] `sequence_id`
- [x] `batch_id`
- [x] `aperture_id`
- [x] `pulse_range_start`
- [x] `pulse_range_count`
- [x] `stream_id`
- [x] `tile_id`
- [x] `tile_count`
- [x] marker (Data/Watermark/EndOfStream)

## Tests Required

### Tier 1: Unit/Contract

- [x] `SarMaterializedImageSinkNode` unit coverage validates real extraction path and deterministic behavior for fixed fixtures.
- [x] Sidecar continuity tests cover non-EOS and EOS behavior.
- [x] Existing accel-token guardrail tests remain passing.

### Tier 2: Integration/JSON

- [x] GraphExecutor JSON pipeline test validates end-to-end materialized output parity against CPU reference metrics.
- [x] Maintained presets contract audit still passes for resolver fields and portable intents.
- [x] Fanout and merge invariants remain stable under terminal-state variability.

### Tier 3: Benchmark/Trace

- [x] Trace schema tests continue to enforce:
- [x] `performance_claim_policy`
- [x] `overhead_ms.graph_run_minus_baseline_median`
- [x] attribution consistency with `graph_overhead_ms`
- [x] If any new trace field is added for output materialization, schema tests are updated with backward-compatibility preserved.

## Validation Matrix

- [x] Build target(s) compile for SAR example and SAR tests.
- [x] SAR unit/integration suite passes in the local development lane.
- [x] At least one GraphExecutor JSON runtime path is executed in validation.
- [x] Benchmark smoke path is executed if benchmark-related code is touched.

## Non-Goals

- [x] No new generic Metal node family introduction in this PR.
- [x] No resolver policy redesign.
- [x] No broad external dataset ingestion expansion in CI.
- [x] No replacement of GraphExecutor+JSON as the user-facing runtime contract.

## PR Deliverables

- [x] Code changes for real materialized output extraction.
- [x] Updated/added tests for parity and sidecar continuity.
- [x] Updated docs/plan notes describing behavior and validation.
- [x] Short PR summary including:
- [x] What changed
- [x] Why it changed
- [x] How correctness was validated
- [x] Residual risks/follow-ups

PR9 Summary:

### What Changed

- Replaced surrogate-only materialization with accel-token keyed real payload extraction in the SAR backprojection to materialized sink flow.
- Added a shared SAR payload runtime component so token-keyed payload handoff remains consistent across plugin and non-plugin execution paths.
- Added focused unit coverage for sink disabled mode, non-data marker behavior, missing-payload no-capture behavior, and payload-consume behavior.

### Why It Changed

- Improve fidelity at graph output by materializing real computed image payloads where available.
- Keep PR6-PR8 JSON GraphExecutor contracts stable while closing PR9 accuracy and parity gaps.

### Validation

- Added and executed SAR materialized-sink contract tests and accel-path extraction tests.
- Re-ran SAR GraphExecutor JSON pipeline parity tests and full CTest SAR lane.

### Residual Risks

- Payload handoff correctness depends on the shared runtime component being linked in SAR plugin layouts.
- Future follow-up can promote the runtime payload registry to a broader shared library layer if reuse beyond SAR is required.

## Reviewer Acceptance Checklist

- [x] Change is architecturally aligned with accel-token contracts.
- [x] CPU reference parity evidence is explicit with tolerances.
- [x] No regression in maintained JSON presets and resolver contract fields.
- [x] Trace/attribution discipline is preserved.
- [x] Tests demonstrate end-to-end GraphExecutor JSON coverage for modified behavior.

## Progress Update (2026-06-08)

- [x] Slice complete: real materialized image extraction from accel-token flow is implemented for simulated backend paths.
- [x] Slice complete: shared-runtime payload handoff implemented so plugin and non-plugin paths use one registry contract.
- [x] Slice complete: dedicated materialized-sink contract tests added for disabled mode, non-data markers, missing-payload no-capture behavior, and stored-payload extraction.
- [x] Validation: SAR unit lane passed via `sar_example_unit` in CTest.

## Follow-Up Candidates (Post-PR9)

- [ ] Optional native matched-filter descriptor path once PR9 parity harness is stable.
- [ ] Expanded external tiny-fixture validation lane with explicit metadata/unit checks.
- [ ] Local visualization/report tooling enhancements (PNG/log-dB/delta heatmaps).

## PR-Ready Commit Plan

### Commit 1: Shared Runtime Wiring

- Add shared payload runtime target and link SAR example, benchmark, tests, and relevant plugins.
- Remove duplicate per-target payload-store compilation units.

### Commit 2: Strict Materialization Behavior

- Keep accel-token keyed payload publication in backprojection.
- Enforce materialized sink capture only on shared payload availability.
- Preserve sidecar extraction metadata on captured images.

### Commit 3: Validation and Docs

- Add dedicated materialized-sink contract tests.
- Update PR9 checklist status and implementation summary.
- Update SAR README with PR9 shared-runtime materialization contract notes.
