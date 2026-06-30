# GraphX FHSS Dashboard Prompt Pack

Use these prompts to execute and verify the FHSS dashboard roadmap in
`GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md`.

## Usage

- Copy one prompt block exactly.
- Start a fresh chat for each step.
- Run implementer first, then verifier.
- Keep scope to exactly one step.

## Global Guardrails (append to every prompt)

```text
Constraints:
- Implement/verify exactly the requested step from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.
- Do not implement future steps.
- Preserve all existing truth-in-labeling language and constraints.
- Keep API compatibility promises already declared in the plan.
- Keep changes independently buildable/testable.
- Add or update tests required by that step.
- Save report to plan/reviews with the exact filename requested below.
```

## Implementer Template

```text
Act as IMPLEMENTER.

Implement exactly Step <N> from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: <STEP TITLE>.

Scope:
- Follow only Step <N> deliverables, acceptance criteria, and decisions already locked in the plan.
- Do not implement later steps.
- Respect sync/async API contract, readiness lifecycle, rebuild phasing, and retention policies already defined in the plan.

Output:
- Files changed
- Tests added/updated
- Commands run
- Acceptance status
- Follow-ups not implemented

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP<N>.md
```

## Verifier Template

```text
Act as VERIFIER.

Verify exactly Step <N> from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: <STEP TITLE>.

Required checks:
- Scope compliance: only Step <N> work is included.
- Build/tests for affected targets.
- Acceptance criteria for Step <N> are satisfied.
- No future-step behavior was added.
- Contracts preserved: API phasing, sync/async operations, readyz lifecycle, rebuild policy, operation retention/deletion semantics.

Verdict: pass | fail | blocked

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP<N>.md
```

## Step 1 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 1 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Server shell and effective JSON graph display.

Required scope:
- Embedded server shell and static page.
- Step-1 stubs/interfaces: GraphConfigurationService (canonical owner), GraphRuntimeSession (stub), GraphSnapshotCollector/SnapshotService (stub).
- Endpoints for Step 1 read-only APIs (including schema-stable empty/default metrics endpoints).
- Load/display effective graph without runtime execution.
- Node view limited to declarative data only.
- No mutation, rebuild execution, stepping, websocket runtime behavior beyond Step 1 scope.

Acceptance and tests:
- Ephemeral port startup.
- /healthz and /readyz behavior per state model.
- Graph/config fetch and rendering.
- Clean shutdown.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP1.md
```

## Step 1 Verifier

```text
Act as VERIFIER.

Verify exactly Step 1 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Server shell and effective JSON graph display.

Focus:
- Step 1 is declarative only.
- Runtime inspection/mutation/rebuild/stepping are not implemented.
- Step-1 service stubs/interfaces exist and are stable.
- API surface and phasing match the plan.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP1.md
```

## Step 2 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 2 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Parameter metadata and staged configuration editing.

Required scope:
- Authoritative /fhss/scenario flow and deterministic regeneration via FHSSConfigurationDeriver.
- Generated-path protection and conflict handling.
- Parameter inspection via unstarted inspection graph/session.
- Staged mutation/validate/undo/discard/export behavior.
- Validation levels and structured error records.
- Operations resource behavior for async export and cancellation/deletion/expiration semantics.

Do not implement Step 3 runtime/rebuild activation behavior.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP2.md
```

## Step 2 Verifier

```text
Act as VERIFIER.

Verify exactly Step 2 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Parameter metadata and staged configuration editing.

Focus:
- Correct authoritative-vs-generated ownership.
- Validation levels and error payload shape.
- Sync vs async operation contract correctness.
- Stale revision and concurrent edit handling.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP2.md
```

## Step 3 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 3 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Transactional graph build and generic runtime status.

Required scope:
- Enable full runtime lifecycle behavior in existing GraphRuntimeSession stub.
- Rebuild lifecycle constraints and invalid_state rejection.
- Transactional replacement/restore behavior.
- Runtime status APIs and controls needed for Step 3.

Do not implement Step 5+ FHSS stepping specifics beyond Step 3 requirements.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP3.md
```

## Step 3 Verifier

```text
Act as VERIFIER.

Verify exactly Step 3 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Transactional graph build and generic runtime status.

Focus:
- Rebuild state gating and 409 invalid_state behavior.
- No side effects on failed rebuild.
- Activation only after successful construction.
- Cleanup failure behavior and readiness transitions.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP3.md
```

## Step 4 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 4 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Generic metrics, topology activity, and diagnostics.

Required scope:
- Populate existing Step-1 metrics endpoint schemas with runtime values.
- Add diagnostics and topology activity presentation.
- Preserve API schema continuity from Step 1 stubs.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP4.md
```

## Step 4 Verifier

```text
Act as VERIFIER.

Verify exactly Step 4 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Generic metrics, topology activity, and diagnostics.

Focus:
- Metrics APIs are populated without schema break.
- Browser/CLI snapshot consistency.
- No dashboard-only hidden metrics path.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP4.md
```

## Step 5 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 5 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: FHSS scenario controller and queue-driven one-message stepping.

Required scope:
- Source-owned queue injection model.
- One-message step semantics and terminal correlation.
- Reset/continue behavior per plan constraints.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP5.md
```

## Step 5 Verifier

```text
Act as VERIFIER.

Verify exactly Step 5 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Focus:
- Exactly-one-message semantics.
- First-terminal-wins operation completion.
- No CLI step command introduced unless explicitly planned.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP5.md
```

## Step 6 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 6 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: WebSocket live snapshots.

Required scope:
- /api/v1/events with sequence contract.
- Contiguous-retained replay-only semantics.
- Resync-required behavior on any missing/expired range.
- Backpressure and queue constraints.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP6.md
```

## Step 6 Verifier

```text
Act as VERIFIER.

Verify exactly Step 6 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Focus:
- Replay guarantees are contiguous-retained-only.
- Gap detection and mandatory resync behavior.
- Slow clients cannot block runtime threads.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP6.md
```

## Step 7 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 7 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: FHSS schedule, channel heatmap, and pulse timeline.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP7.md
```

## Step 7 Verifier

```text
Act as VERIFIER.

Verify exactly Step 7 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP7.md
```

## Step 8 Implementer

```text
Act as IMPLEMENTER.

Implement exactly Step 8 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md: Decoder and signal investigation views.

Save report to: plan/reviews/GRAPHX_IMPL_DASHBOARD_STEP8.md
```

## Step 8 Verifier

```text
Act as VERIFIER.

Verify exactly Step 8 from GRAPHX_FHSS_WEB_DASHBOARD_PLAN.md.

Save report to: plan/reviews/GRAPHX_VERIFY_DASHBOARD_STEP8.md
```
