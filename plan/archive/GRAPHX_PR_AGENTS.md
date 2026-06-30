# GraphX Cleanup PR Agents

Use these prompts with:

- `plan/agents/GRAPHX_AGENT_ROLES.md`
- `plan/roadmap/GRAPHX_PR_ROADMAP.md`
- `plan/reviews/GRAPHX_INSPECTOR_REPORT.md`
- `plan/reviews/GRAPHX_SIMPLIFIER_REPORT.md`

The active cleanup roadmap is:

```text
plan/roadmap/GRAPHX_PR_ROADMAP.md
```

Archived PR prompts are historical reference only. Do not treat archived PR
plans as active scope unless the user explicitly says to do so.

## Global Constraints

- Implement or verify exactly the named PR.
- Every PR must compile independently.
- Every PR must add, update, or delete tests.
- Do not add compatibility shims.
- Do not preserve obsolete behavior only because old tests reference it.
- Do not maintain dual canonical paths.
- Prefer deletion over compatibility when the roadmap calls for removal.
- Do not invent GraphX adaptors, accessors, executor paths, or pseudo-node APIs.
- Use repository-native GraphX APIs, including `GraphExecutorBuilder`, existing
  graph config parsing, existing plugin loading, and existing node/edge methods.
- Public `...Node` classes must be real GraphX nodes.
- Accelerator-ready edge contracts must use
  `graph::gpu::accel::ControlToken<PacketT>` packet sidecars.
- Keep GraphX core domain-neutral.
- Keep DSP, SDR/FHSS, SAR, and GPU semantics in their domain layers.
- Preserve truth-in-labeling in code, config, docs, reports, and tests.
- Do not claim production RF, production SAR, real FFT behavior, GPU execution,
  Doppler/noise/multipath support, overlap-aware separation, or external
  baseline support unless the named PR implements and tests that claim.
- Keep local-only and external-data work out of default CI unless the named PR
  explicitly adds CI-safe skip behavior.

## Standard Implementer Report

Save implementer reports to:

```text
plan/reviews/GRAPHX_IMPL_PR<N>.md
```

Use the exact PR number, including suffixes if introduced later.

The report must contain:

1. Files changed.
2. Files deleted.
3. Tests added or updated.
4. Tests deleted.
5. Build/test commands run.
6. Acceptance criteria status.
7. Truth-in-labeling status.
8. Remaining follow-up work.
9. Scope intentionally not touched.

## Standard Verifier Report

Save verifier reports to:

```text
plan/reviews/GRAPHX_VERIFY_PR<N>.md
```

Use the exact PR number, including suffixes if introduced later.

The report must contain:

1. Verdict: pass, fail, or blocked.
2. Scope compliance findings.
3. Acceptance criteria findings.
4. Tests/build commands run.
5. Files inspected.
6. Compatibility-shim or dual-canonical-path check.
7. Truth-in-labeling check.
8. Regression or deletion-risk findings.
9. Required fixes before acceptance.

## Implementer Prompt Template

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR<N> from plan/roadmap/GRAPHX_PR_ROADMAP.md: <PR title>.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Scope:
- Follow only the PR<N> purpose, scope, file expectations, tests, acceptance
  criteria, truth-in-labeling requirements, risks, rollback plan, and CI/local
  status in plan/roadmap/GRAPHX_PR_ROADMAP.md.
- Read the current repository before editing.
- Delete obsolete files/tests when the PR calls for deletion.
- Add or update tests required by the PR.
- Keep the change independently compiling and reviewable.

Do not implement future PRs.
Do not add compatibility shims.
Do not add alternate GraphX APIs.
Do not widen truth-in-labeling claims.

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR<N>.md.
```

## Verifier Prompt Template

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR<N> from plan/roadmap/GRAPHX_PR_ROADMAP.md: <PR title>.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Required checks:
- The implementation satisfies only the PR<N> scope in
  plan/roadmap/GRAPHX_PR_ROADMAP.md.
- The repository compiles for the affected targets or the report clearly
  identifies a blocking pre-existing build issue.
- Required tests were added, updated, or deleted.
- Acceptance criteria from PR<N> are satisfied.
- Truth-in-labeling requirements from PR<N> are preserved.
- No compatibility shim was added.
- No dual canonical path was introduced or preserved when the PR requires
  deletion.
- No future-PR work was smuggled into the change.
- Local-only or external-data behavior is gated and does not enter default CI.

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR<N>.md.
```

## PR Index

Use the following titles exactly when issuing implementer or verifier prompts:

| PR | Title |
|---|---|
| PR1 | Baseline Architecture Guardrails |
| PR2 | Remove FHSS Pulse Merge Duplicate Node |
| PR3 | Remove FHSS Correlator-Bank Canonical Surface |
| PR4 | Normalize Repeated-Port GraphX Helpers |
| PR5 | Simplify Channelizer Port Implementation |
| PR6 | Remove Aggregate Channelizer Contracts And Guards |
| PR7 | SAR Config Set Consolidation |
| PR8 | Documentation Surface Reduction |
| PR9 | Placeholder And Editor Artifact Cleanup |
| PR10 | Accelerator Token Contract Hardening |
| PR11 | Deterministic Diagnostics And Metrics Baseline |
| PR12 | SAR Token Architecture Stability Pass |
| PR13 | External SAR Baseline Survey |
| PR14 | Local-Only SAR Baseline Runner |
| PR15 | GraphX-Vs-Baseline SAR Comparison Harness |
| PR16 | Optional SAR Baseline Substitution Experiment |
| PR17 | Cleanup Roadmap Closure And Baseline Refresh |

## PR-Specific Implementer Agents

### PR1: Baseline Architecture Guardrails

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR1 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Baseline Architecture Guardrails.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR1.md.
```

### PR2: Remove FHSS Pulse Merge Duplicate Node

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR2 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove FHSS Pulse Merge Duplicate Node.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR2.md.
```

### PR3: Remove FHSS Correlator-Bank Canonical Surface

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR3 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove FHSS Correlator-Bank Canonical Surface.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR3.md.
```

### PR4: Normalize Repeated-Port GraphX Helpers

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR4 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Normalize Repeated-Port GraphX Helpers.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR4.md.
```

### PR5: Simplify Channelizer Port Implementation

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR5 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Simplify Channelizer Port Implementation.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR5.md.
```

### PR6: Remove Aggregate Channelizer Contracts And Guards

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR6 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove Aggregate Channelizer Contracts And Guards.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR6.md.
```

### PR7: SAR Config Set Consolidation

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR7 from plan/roadmap/GRAPHX_PR_ROADMAP.md: SAR Config Set Consolidation.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR7.md.
```

### PR8: Documentation Surface Reduction

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR8 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Documentation Surface Reduction.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR8.md.
```

### PR9: Placeholder And Editor Artifact Cleanup

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR9 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Placeholder And Editor Artifact Cleanup.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR9.md.
```

### PR10: Accelerator Token Contract Hardening

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR10 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Accelerator Token Contract Hardening.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR10.md.
```

### PR11: Deterministic Diagnostics And Metrics Baseline

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR11 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Deterministic Diagnostics And Metrics Baseline.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR11.md.
```

### PR12: SAR Token Architecture Stability Pass

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR12 from plan/roadmap/GRAPHX_PR_ROADMAP.md: SAR Token Architecture Stability Pass.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR12.md.
```

### PR13: External SAR Baseline Survey

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR13 from plan/roadmap/GRAPHX_PR_ROADMAP.md: External SAR Baseline Survey.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR13.md.
```

### PR14: Local-Only SAR Baseline Runner

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR14 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Local-Only SAR Baseline Runner.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR14.md.
```

### PR15: GraphX-Vs-Baseline SAR Comparison Harness

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR15 from plan/roadmap/GRAPHX_PR_ROADMAP.md: GraphX-Vs-Baseline SAR Comparison Harness.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR15.md.
```

### PR16: Optional SAR Baseline Substitution Experiment

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR16 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Optional SAR Baseline Substitution Experiment.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR16.md.
```

### PR17: Cleanup Roadmap Closure And Baseline Refresh

```text
Act as IMPLEMENTER using plan/agents/GRAPHX_AGENT_ROLES.md.

Implement exactly PR17 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Cleanup Roadmap Closure And Baseline Refresh.

Use the implementer prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Output the standard IMPLEMENTER summary.
Save the report to plan/reviews/GRAPHX_IMPL_PR17.md.
```

## PR-Specific Verifier Agents

### PR1: Baseline Architecture Guardrails

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR1 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Baseline Architecture Guardrails.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR1.md.
```

### PR2: Remove FHSS Pulse Merge Duplicate Node

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR2 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove FHSS Pulse Merge Duplicate Node.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR2.md.
```

### PR3: Remove FHSS Correlator-Bank Canonical Surface

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR3 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove FHSS Correlator-Bank Canonical Surface.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR3.md.
```

### PR4: Normalize Repeated-Port GraphX Helpers

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR4 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Normalize Repeated-Port GraphX Helpers.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR4.md.
```

### PR5: Simplify Channelizer Port Implementation

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR5 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Simplify Channelizer Port Implementation.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR5.md.
```

### PR6: Remove Aggregate Channelizer Contracts And Guards

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR6 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Remove Aggregate Channelizer Contracts And Guards.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR6.md.
```

### PR7: SAR Config Set Consolidation

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR7 from plan/roadmap/GRAPHX_PR_ROADMAP.md: SAR Config Set Consolidation.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR7.md.
```

### PR8: Documentation Surface Reduction

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR8 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Documentation Surface Reduction.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR8.md.
```

### PR9: Placeholder And Editor Artifact Cleanup

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR9 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Placeholder And Editor Artifact Cleanup.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR9.md.
```

### PR10: Accelerator Token Contract Hardening

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR10 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Accelerator Token Contract Hardening.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR10.md.
```

### PR11: Deterministic Diagnostics And Metrics Baseline

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR11 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Deterministic Diagnostics And Metrics Baseline.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR11.md.
```

### PR12: SAR Token Architecture Stability Pass

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR12 from plan/roadmap/GRAPHX_PR_ROADMAP.md: SAR Token Architecture Stability Pass.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR12.md.
```

### PR13: External SAR Baseline Survey

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR13 from plan/roadmap/GRAPHX_PR_ROADMAP.md: External SAR Baseline Survey.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR13.md.
```

### PR14: Local-Only SAR Baseline Runner

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR14 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Local-Only SAR Baseline Runner.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR14.md.
```

### PR15: GraphX-Vs-Baseline SAR Comparison Harness

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR15 from plan/roadmap/GRAPHX_PR_ROADMAP.md: GraphX-Vs-Baseline SAR Comparison Harness.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR15.md.
```

### PR16: Optional SAR Baseline Substitution Experiment

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR16 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Optional SAR Baseline Substitution Experiment.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR16.md.
```

### PR17: Cleanup Roadmap Closure And Baseline Refresh

```text
Act as VERIFIER using plan/agents/GRAPHX_AGENT_ROLES.md.

Verify exactly PR17 from plan/roadmap/GRAPHX_PR_ROADMAP.md: Cleanup Roadmap Closure And Baseline Refresh.

Use the verifier prompt from: plan/agents/GRAPHX_PR_AGENTS.md

Stop after verifier report.
Save the report to plan/reviews/GRAPHX_VERIFY_PR17.md.
```
