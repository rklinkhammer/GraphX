# GraphX FHSS Dashboard Prompt Pack (Strict Mode)

This strict pack is for CI-style execution and verification of
`GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md`.

It enforces:

- mandatory build/test commands per step,
- mandatory failure-injection coverage where applicable,
- explicit pass/fail/block gate format.

## How To Use

- Use one fresh chat per step.
- Run IMPLEMENTER prompt first.
- Run VERIFIER prompt second.
- Treat any unmet required check as `fail`.

## Global Strict Constraints

```text
Strict constraints:
- Implement/verify exactly one step only.
- Build must pass for affected targets, or report `blocked` with concrete pre-existing blocker evidence.
- Required tests for the step must run and be reported.
- No future-step functionality may be implemented.
- Preserve existing API and decision contracts in GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.
- Any deviation from required checks -> verdict `fail`.
```

## Standard Strict Report Format

Use this exact report structure for both IMPLEMENTER and VERIFIER outputs:

```text
1. Verdict: pass | fail | blocked
2. Scope checked
3. Files changed/inspected
4. Build commands run + outcome
5. Required tests run + outcome
6. Failure-injection checks run + outcome
7. Contract compliance checks
8. Regressions found
9. Required fixes (if fail/blocked)
```

## Required Commands Baseline

Use repository-native commands for your environment. At minimum:

```text
cmake -S . -B build -DGRAPHX_BUILD_WEB_DASHBOARD=ON -DGRAPHX_BUILD_EXAMPLES_DSP=ON
cmake --build build --target dsp_fhss_demo
```

For test execution, run only step-relevant test targets or filters when
possible and include exact command lines in the report.

## Step 1 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 1 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- Server shell and static asset serving.
- GraphConfigurationService canonical-owner stub.
- GraphRuntimeSession stub.
- GraphSnapshotCollector/SnapshotService stub.
- Read-only Step 1 endpoints, including metrics endpoint schemas with empty/default payloads.
- Declarative graph viewer only.

Forbidden in Step 1:
- Runtime execution lifecycle behavior.
- Rebuild implementation (must remain pre-Step-3 behavior).
- Parameter mutation and stepping flows.

Required tests:
- Ephemeral port startup test.
- /healthz and /readyz state tests.
- /api/v1/graph and /api/v1/config response schema tests.
- /api/v1/metrics and /api/v1/metrics/edges schema-valid empty/default tests.
- Clean shutdown test.

Failure-injection checks (required):
- dashboard startup failure (bind/asset/config).
- plugin loading failure path (if initialization path touches plugin loading).

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP1_STRICT.md
```

## Step 1 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 1 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- Scope is Step-1 only.
- Step-1 stubs exist and are used as the integration seam.
- API phasing is respected (no Step-2/3 behavior leaked).
- Required tests and failure-injection checks were executed.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP1_STRICT.md
```

## Step 2 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 2 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- Authoritative /fhss/scenario flow.
- Deterministic derivation and generated-path protection.
- Parameter inspection via unstarted inspection graph/session.
- Staged mutation/validate/undo/discard/export.
- Validation level taxonomy and structured errors.
- Operations lifecycle semantics for async export (get/cancel/delete/expire).

Required tests:
- Stale revision conflict tests.
- Generated-field write rejection tests.
- Validation level and error-shape tests.
- Sync-vs-async contract tests (PATCH sync, export async with operation resource).
- API contract replay/resync tests for operations where relevant.

Failure-injection checks (required):
- disk-full (`ENOSPC`) during artifact export.
- websocket disconnect/reconnect during active updates.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP2_STRICT.md
```

## Step 2 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 2 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- Ownership and derivation invariants hold.
- Validation schema and error shape are complete.
- Operation retention/expiration/deletion semantics are implemented correctly.
- Browser/API concurrency semantics are tested.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP2_STRICT.md
```

## Step 3 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 3 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- Enable GraphRuntimeSession runtime lifecycle behavior.
- Rebuild gating and 409 invalid_state behavior.
- Transactional replace/restore semantics.
- Runtime status behavior and controls for Step 3.

Required tests:
- Rebuild accepted/rejected state matrix tests.
- No-side-effects tests for invalid/failed rebuild.
- Activation-after-successful-construction tests.
- Cleanup-failed behavior tests.

Failure-injection checks (required):
- executor construction failure.
- queue disable during rebuild.
- process shutdown (SIGINT/SIGTERM) during rebuild.
- thread interruption around command/runtime-owner flow.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP3_STRICT.md
```

## Step 3 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 3 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- Rebuild lifecycle contract is fully enforced.
- Readiness state machine transitions are preserved during rebuild and shutdown.
- Failure-injection coverage is complete and passing.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP3_STRICT.md
```

## Step 4 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 4 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- Populate existing metrics endpoint schemas (no schema changes).
- Add diagnostics and topology activity views.

Required tests:
- Endpoint schema stability tests (Step-1 shape unchanged).
- Populated-value correctness tests.
- Browser/CLI consistency tests.

Failure-injection checks (required):
- snapshot collection interruption/resume.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP4_STRICT.md
```

## Step 4 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 4 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- Metrics schema continuity from Step 1 stubs.
- Runtime values are populated correctly.
- No hidden dashboard-only metrics path.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP4_STRICT.md
```

## Step 5 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 5 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- Queue-based one-message stepping.
- Terminal correlation and first-terminal-wins semantics.
- Reset/continue behavior per plan.

Required tests:
- exactly-one-message-per-step.
- duplicate/concurrent request rejection.
- reset behavior with terminal record retention.

Failure-injection checks (required):
- timeout/cancellation races.
- queue disable edge cases.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP5_STRICT.md
```

## Step 5 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 5 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- stepping semantics match plan contracts.
- terminal correlation tuple enforcement is strict.
- no CLI step command was added unless explicitly approved.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP5_STRICT.md
```

## Step 6 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 6 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required implementation scope:
- live events stream with sequence contract.
- contiguous-retained-only replay guarantee.
- mandatory resync on any missing/expired range.
- bounded queues and backpressure behavior.

Required tests:
- monotonic sequence tests.
- contiguous replay resume tests.
- missing-range -> resync-required tests.
- slow-client non-blocking tests.

Failure-injection checks (required):
- websocket disconnect/reconnect under load.
- forced retention expiration gap during reconnect.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP6_STRICT.md
```

## Step 6 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 6 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required checks:
- replay guarantees are exactly contiguous-retained-only.
- any gap forces API resynchronization.
- runtime threads are never blocked by subscribers.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP6_STRICT.md
```

## Step 7 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 7 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required tests:
- schedule and heatmap rendering correctness.
- bounded snapshot size/rate behavior.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP7_STRICT.md
```

## Step 7 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 7 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP7_STRICT.md
```

## Step 8 Strict Implementer

```text
Act as IMPLEMENTER (STRICT MODE).

Implement exactly Step 8 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Required tests:
- decoder diagnostics correctness.
- artifact export constraints and containment checks.

Failure-injection checks (required):
- artifact write failures and path containment violations.

Output strict report.
Save to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP8_STRICT.md
```

## Step 8 Strict Verifier

```text
Act as VERIFIER (STRICT MODE).

Verify exactly Step 8 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Output strict report.
Save to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP8_STRICT.md
```
