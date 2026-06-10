Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_PERFORMANCE_AUDIT.md
- plan/reviews/SAR_VERIFIER_PERF_PR1_A1_*.md
- current PR1-A1 diff
- current repository state
- current test output

Task:
Implement PR1-A2 only.

PR1-A2 title:  Public Native Metal Telemetry Snapshots
PR1-A2 Scope:
- Add a public telemetry snapshot API for Metal transfer/kernel metrics.
- Wire it into benchmark trace export.
- Do not change execution semantics.


Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR1-A2 requires it.
- Add or update tests for PR1-A2.
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

PR1-A2 acceptance criteria:
  - Public snapshot API exists and is used by the SAR benchmark.
  - Benchmark trace exports telemetry summaries for native backend runs.
  - Full CTest lane remains green.

=========
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_PERFORMANCE_AUDIT.md
- plan/reviews/SAR_VERIFIER_PERF_PR1_A1.md
- implemented PR1-A1 diff
- Current repository state
- Current diff

Task:
Verify whether PR1-A1  actually satisfies its acceptance criteria.

Check:
- Definitive topology emits stage timing fields in diagnostics and trace.
- Benchmark trace schema covers those fields.
- Full CTest lane remains green.


Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.
==========
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

1. Make the schema expectation for `range_window_time_us` explicitly conditional on `profile.range_stage == "window"`, and document zero for compression-mode traces.
2. Remove the unused helper parameter warning in `examples/SAR/src/ImageTileMergeNode.cpp`.
3. Optionally add a grouped benchmark-trace contract assertion for the stage timing block, rather than only per-field checks.


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

