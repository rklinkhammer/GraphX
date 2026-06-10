Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:

- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md

Task:

Convert the external baseline report into reviewable PRs.

Rules:

- Preserve GraphX architecture.
- External packages are comparators, not templates.
- One concern per PR.
- Instrumentation before optimization.
- Metrics before substitution.
- Do not modify GraphX internals to mimic external APIs.

Output:

1. External baseline PR roadmap.
2. Files to add.
3. Tests to add.
4. CI-safe lanes.
5. Local-only benchmark lanes.
6. Acceptance criteria.
7. Deferred work.






Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md
- current repository state
- current test output

Task:
Implement PR3 only

PR3 title:  Tokenize Merge and Diagnostics Boundaries
PR3 Scope:
- Merge/diagnostics tests validating sidecar-derived output semantics from token input only.
- Regression tests confirming no fallback identity derivation outside sidecar.



Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR3 requires it.
- Add or update tests for PR3.
- Remove tests that only validate obsolete behavior.
- Keep Metal as the first backend.
- Preserve GraphX dynamic loading and resolver behavior.

Required output:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed or replaced.
5. Build commands run.
6. Test commands run.
7. Remaining follow-up items.

PR3 acceptance criteria:
- Definitive runtime path remains tokenized through merge/diagnostics boundary.
- Diagnostics and metrics still emitted with unchanged semantic meaning.

=========
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/SAR_IMPL_PR3_1.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md
- current repository state
- current test output
- current PR3 diff


Task:
Verify whether PR3  actually satisfies its acceptance criteria.

Check:
- Definitive runtime path remains tokenized through merge/diagnostics boundary.
- Diagnostics and metrics still emitted with unchanged semantic meaning.

Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.
==========


Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/SAR_IMPL_PR3_1.md
- plan/reviews/SAR_VERIFIER_PR3_1.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md
- current repository state
- current test output
- current PR3 diff

Task:
Apply verifier fixes for PR3 only.

PR2 title:  Remove Encoded Host Pointer and Event Identity
PR2 Scope:
- Topology tests that validate token contract continuity from source through split.
- Node contract tests for sidecar initialization at source and preservation through DSP stages.



Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR2 requires it.
- Add or update tests for PR2.
- Remove tests that only validate obsolete behavior.
- Keep Metal as the first backend.
- Preserve GraphX dynamic loading and resolver behavior.

Required output:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed or replaced.
5. Build commands run.
6. Test commands run.
7. Remaining follow-up items.

PR2 acceptance criteria:
1. Canonical definitive topology uses token contract through source and DSP-to-GPU handoff: NOT SATISFIED.
2. PR2 compiles and tests pass without compatibility shims: SATISFIED.

Required fixes:
1. Convert source and DSP stage node contracts to token form for the definitive path.
   - Source emits `SarAccelControlToken`.
   - Range window/compression consume and emit `SarAccelControlToken`.
   - Split becomes token-to-token (or is removed/repurposed if redundant).
2. Update definitive topology wiring and impacted tests to reflect true token continuity before H2D.
3. Keep current PR2 test coverage, and add explicit compile-time/type-level assertions that source/window/compression signatures are token-based.

========

========
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Apply verifier fixes for PR1-A1 only.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_PERFORMANCE_AUDIT.md
- plan/reviews/SAR_PR1_A1_VERIFIER_REPORT.md
- current PR1-A1 diff
- current repository state
- current test output

Scope:
Fix only the blocking and required-fix items identified in SAR_PR1_A1_VERIFIER_REPORT.md.

Do not implement non-blocking follow-ups.
Do not start PR1-A2.
Do not redesign.
Do not optimize.
Do not change SAR math.
Do not broaden instrumentation beyond PR1-A1.
Do not modify unrelated topology or GPU token architecture.

PR1-A1 acceptance criteria:
1. Definitive topology emits stage timing fields in diagnostics and trace.
2. Benchmark trace schema covers those fields.
3. Full CTest lane remains green.

Required fixes:

<PASTE VERIFIER BLOCKING ISSUES HERE>

<PASTE VERIFIER SUGGESTED FIXES HERE>

Implementation rules:
- Treat the most recent verifier report as authoritative.
- Older reports are historical context only.
- Implement the smallest changes needed to satisfy PR1-A1.
- Preserve `AccelControlToken<SarSidecar>` architecture.
- Do not introduce compatibility shims.
- Do not add performance optimizations.
- Add or update tests only where needed to prove the verifier fixes.
- If a suggested fix conflicts with the PR1-A1 scope, stop and report the conflict instead of implementing around it.

Required output:
1. Verifier findings addressed.
2. Files changed.
3. Files deleted, if any.
4. Tests added or updated.
5. Tests removed, if any.
6. Build commands run.
7. Test commands run.
8. Remaining verifier issues, if any.

==========
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Apply verifier fixes for PR2 only.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_PERFORMANCE_AUDIT.md
- plan/reviews/SAR_PR1_A1_VERIFIER_REPORT.md
- current PR1-A1 diff
- current repository state
- current test output

Scope:
Fix only the blocking and required-fix items identified in plan/reviews/SAR_PR1_A1_VERIFIER_REPORT.md.

Do not implement non-blocking follow-ups.
Do not start PR3.
Do not redesign.
Do not optimize.
Do not change SAR math.
Do not broaden instrumentation beyond PR3.
Do not modify unrelated topology or GPU token architecture.

PR2 acceptance criteria:
1. Canonical definitive topology uses token contract through source and DSP-to-GPU handoff: NOT SATISFIED.
2. PR2 compiles and tests pass without compatibility shims: SATISFIED.

Required fixes:

1. Convert source and DSP stage node contracts to token form for the definitive path.
   - Source emits `SarAccelControlToken`.
   - Range window/compression consume and emit `SarAccelControlToken`.
   - Split becomes token-to-token (or is removed/repurposed if redundant).
2. Update definitive topology wiring and impacted tests to reflect true token continuity before H2D.
3. Keep current PR2 test coverage, and add explicit compile-time/type-level assertions that source/window/compression signatures are token-based.


Implementation rules:
- Treat the most recent verifier report as authoritative.
- Older reports are historical context only.
- Implement the smallest changes needed to satisfy PR1-A1.
- Preserve `AccelControlToken<SarSidecar>` architecture.
- Do not introduce compatibility shims.
- Do not add performance optimizations.
- Add or update tests only where needed to prove the verifier fixes.
- If a suggested fix conflicts with the PR1-A1 scope, stop and report the conflict instead of implementing around it.

Required output:
1. Verifier findings addressed.
2. Files changed.
3. Files deleted, if any.
4. Tests added or updated.
5. Tests removed, if any.
6. Build commands run.
7. Test commands run.
8. Remaining verifier issues, if any.

===============

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_PERFORMANCE_AUDIT.md
- plan/reviews/SAR_VERIFIER_PERF_PR1_A2_*.md
- current PR1-A2 diff
- current repository state
- current test output

Task:
Implement PR1-A3 only.

PR1-A3 Title:  Public Native Metal Telemetry Snapshots
PR1-A3 Scope:
  - Surface read-only memory pool metrics from the native Metal runtime.
  - Export them in benchmark traces.


Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR1-A3 requires it.
- Add or update tests for PR1-A3.
- Remove tests that only validate obsolete behavior.
- Keep Metal as the first backend.
- Preserve GraphX dynamic loading and resolver behavior.

Required output:
1. Files changed.
2. Files deleted.
3. Tests added.
4. Tests removed or replaced.
5. Build commands run.
6. Test commands run.
7. Remaining follow-up items.

PR1-A3 acceptance criteria:
  - Memory metrics are publicly queryable.
  - SAR benchmark trace exports memory metrics for native backend runs.
  - Full CTest lane remains green.


=========
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/SAR_IMPL_PR2_1.md
- plan/reviews/SAR_IMPL_PR2_2.md
- plan/reviews/SAR_IMPL_PR2_3.md
- plan/reviews/SAR_VERIFIER_PR2_2.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md
- current repository state
- current test output
- current PR2 diff

Verify PR2-F1 only.

Required checks:
1. The range window/compression semantic decision is explicit.
2. The decision is tested.
3. The implementer did not defer the semantic clarification.
4. If numerical DSP is deferred, the repo explicitly says so.
5. Full CTest lane remains green.

Fail the PR if the report says "deferred" without an explicit repository-level decision and test.

  =================================
  Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement PR2-F1: Clarify and test pre-GPU DSP token-stage semantics.

Inputs:
- plan/reviews/SAR_PR2_VERIFIER_REPORT.md
- current repository state
- current PR2 diff

Background:
PR2 passed, but the verifier identified an unresolved semantic ambiguity:

"Range window/compression are now contract/timing token stages; if numerical DSP behavior is expected in these host stages, that expectation should be explicitly specified and tested separately."

This is no longer optional for this PR2-F1 task.

Scope:
Resolve this ambiguity only.

Do not start PR3.
Do not redesign the SAR pipeline.
Do not optimize.
Do not change GPU token architecture.
Do not introduce compatibility shims.
Do not implement full SAR fidelity unless explicitly required below.

Required work:
1. Inspect `RangeWindowNode`, `RangeCompressionNode`, definitive topology config, diagnostics output, and related tests.
2. Determine and document whether `RangeWindowNode` and `RangeCompressionNode` are currently intended to be:
   - token-only/timing placeholder stages, or
   - numerically meaningful DSP transform stages.
3. Encode that decision in the repo as documentation, config comments if supported, or explicit test names.
4. Add one integration test that asserts the definitive runtime still produces expected downstream diagnostics under both range-stage modes, if both modes exist.
5. If only one mode exists, add a test proving the current intended mode and explicitly document that the other mode is deferred.
6. Do not defer this semantic clarification.

Acceptance criteria:
- The repository explicitly states whether pre-GPU range window/compression stages are token-only/timing stages or numerical DSP stages.
- Tests reflect that decision.
- If both range-stage modes exist, definitive runtime diagnostics are tested under both.
- If numerical DSP behavior is deferred, the deferral is explicit and tested as non-numerical/token-only behavior.
- Full CTest lane remains green.

Required output:
1. Decision made: token-only, numerical, or dual-mode.
2. Files changed.
3. Tests added or updated.
4. Build/test commands run.
5. Any deferred numerical DSP work, clearly labeled as future PR work.



=======

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md
- current repository state
- current test output

Task:
Implement PR3 only

PR3 title:  Tokenize Merge and Diagnostics Boundaries
PR3 Scope:
- Merge/diagnostics tests validating sidecar-derived output semantics from token input only.
- Regression tests confirming no fallback identity derivation outside sidecar.
