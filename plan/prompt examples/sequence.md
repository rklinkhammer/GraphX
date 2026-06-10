Act as EXTERNAL_SAR_BASELINE_REVIEWER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Analyze the current repository only. Do not redesign or implement. Stop after the current-state report.

=====

Act as INSPECTOR using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Analyze the current repository only. Do not redesign or implement. Stop after the current-state report.

====
save inspector output to plan/reviews/SAR_INSPECTOR_REPORT.md
====
Act as SIMPLIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Use the attached/current Inspector report as the authoritative current-state input:

plan/reviews/SAR_INSPECTOR_REPORT.md

Do not reinspect from scratch unless needed to clarify a specific finding.

Backward compatibility is not required.
Complexity is a defect.
Prefer deletion over compatibility.

Your task:
- Convert the Inspector observations into a clean target architecture.
- Identify what to delete, keep, rename, and replace.
- Resolve the path toward exactly one canonical SAR GPU flow:
  AccelControlToken<SarSidecar>.

Produce only:
1. Target type model.
2. Target node model.
3. Deletion list.
4. Replacement list.
5. Architecture invariants.
6. Open questions that block planning.

Do not implement code.
Do not produce a PR plan yet.
========
save simplifer output to plan/reviews/SAR_SIMPLIFIER_REPORT.md
========
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Use these two reports as inputs:

- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md

Produce a PR-sized cleanup roadmap.

Each PR must:
- compile independently,
- have tests,
- avoid compatibility shims,


I would also update `PLANNER` with this additional roadmap section:

After GraphX SAR token architecture and basic performance instrumentation are stable, prefer:

1. External SAR baseline survey.
2. Select one baseline package.
3. Add local-only baseline runner script.
4. Add GraphX-vs-baseline output comparison harness.
5. Add tiny deterministic fixture comparison.
6. Add CI-safe derived fixture if licensing permits.
7. Add optional local Gotcha/OpenSAR benchmark.
8. Add substitution experiment where GraphX replaces the baseline SAR stage in a selected test.actly one canonical SAR GPU path.


Do not implement code.
==========
save planner output as plan/roadmap/SAR_PR_ROADMAP.md
=======
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR1_1_REPORT.md



Task:
Implement PR1 only.
-- apply PR1 verifier fixes <=======>

PR1 title:
Introduce Canonical Token and Sidecar Types
PR1 scope:
Apply required fixes for PR1 only:
Remove host_ptr identity encoding entirely.
Eliminate identity packing in AzimuthTileSplitNode.cpp:19.
Stop deriving host_ptr from token/sequence identity in:
AzimuthTileSplitNode.cpp:60
D2HAsyncAccelNode.cpp:49
Add explicit tests that enforce the rule.
Assert SAR identity and merge diagnostics are unchanged when host_ptr changes but sidecar is constant.
Assert no runtime node reconstructs SAR identity from host_ptr.
Optional hardening for ready_event rule.
Add tests proving SAR identity is invariant to ready_event variation.

Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR1 requires it.
- Add or update tests for PR1.
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
======
Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- implemented PR1 diff

Task:
Verify whether PR1 actually satisfies its acceptance criteria.

Check:
- No encoded host_ptr identity remains.
- No encoded ready_event identity remains.
- No global sidecar store remains as primary path.
- SAR sidecar is carried explicitly.
- Generic GPU nodes remain SAR-unaware.
- Tests cover sidecar preservation.
- Deleted tests were obsolete.
- Build and test results are credible.

Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.
=============
save verifier output as plan//reviews/SAR_VERIFIER_PR1_1_REPORT.md
====
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR1_!_REPORT.md
Task:
Implement PR1 only.

PR1 title:
Introduce Canonical Token and Sidecar Types
PR1 scope:
<copy exact scope from roadmap>

Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR1 requires it.
- Add or update tests for PR1.
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




Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md

Task:
Implement PR2 only.

PR2 title:
Remove Encoded `host_ptr`/`ready_event` Identity

PR2 scope:
Eliminate most dangerous ambiguity: encoded identity in pointer/event channels.

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



Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- implemented PR2 diff

Task:
Verify whether PR2 actually satisfies its acceptance criteria.

Check:
- SAR path preserves identity via explicit token/sidecar only.
- Sidecar global store removed.
- SAR unit suite passes.

- No encoded host_ptr identity remains.
- No encoded ready_event identity remains.
- No global sidecar store remains as primary path.
- SAR sidecar is carried explicitly.
- Generic GPU nodes remain SAR-unaware.
- Tests cover sidecar preservation.
- Deleted tests were obsolete.
- Build and test results are credible.

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
- plan/reviews/SAR_PR_ROADMAP.md
- Most recent VERIFIER report
- Current repository state
- Current diff

Task:
Implement PR3 only.

PR3 title:
Convert SAR H2D/Kernel/D2H Path to Explicit Tokens

PR3 scope:
Convert core SAR GPU path to one canonical tokenized contract flow.

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

====


=========

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR4_!_REPORT.md
- Current repository state
- Current diff

Task:
Implement PR4 verifer fixes and suggested fixes

PR4 title:
Remove obsolete SAR message abstractions

PR4 scope:
Remove non-canonical legacy edge abstractions from SAR message model.
1. Keep current PR4 code changes as accepted for criteria compliance.
2. If strict PR boundary purity is required, move or label the guardrail-test broadening as PR6-aligned follow-up while retaining PR4 type deletions.
3. Add a short PR note clarifying that remaining legacy-name strings are intentional negative-validation artifacts, not runtime contracts.
4. rename benchmark trace fields that mention pointer/event tokens to avoid ambiguity with identity transport semantics.

Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR4 requires it.
- Add or update tests for PR4.
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

====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- PR1 - PR4 verifier reports
- current repo state

Task:
Verify whether PR4 actually satisfies its acceptance criteria.

Check:
  - Removed legacy message types are not referenced by definitive runtime path.
  - SAR unit and parser-related tests pass.

Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.


===

Act as PRINCIPAL_ARCHITECT using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- PR1 - PR4 verifier reports
- current repo state

Question:
Are we still converging toward the intended architecture, or has the implementation drifted?

Do not redesign unless there is architectural drift.

=======

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR7_*_REPORT.md
- Current repository state
- Current diff

Task:
Finalize native-metal parity on single canonical SAR path
Lock native-metal parity evidence and remove residual dual-path artifacts.

PR8 title:
Native Metal Parity Finalization

PR8 scope:

Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR8 requires it.
- Add or update tests for PR8.
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

====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- PR1 - PR7 verifier reports
- current repo state

Task:
Verify whether PR8 actually satisfies its acceptance criteria.

Check:
  - Definitive topology is the single canonical SAR GPU runtime path.
  - Full CTest lane passes.
  - Benchmark attribution policy remains intact.

Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.


======

Act as PERFORMANCE_AUDITOR using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- SAR_VERIFIER_PR*.md

Task:

Check:


Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.



Act as PERFORMANCE_AUDITOR using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- current repository state
- PR1–PR8 verifier reports
- current SAR topology
- Metal backend implementation

Repository inspection overrides assumptions.

Mission:

Determine whether GraphX currently exposes enough instrumentation to understand runtime behavior.

Do not optimize.

Do not redesign.

Do not recommend algorithm changes.

Required outputs:

1. Missing measurements.
2. Existing measurements.
3. Graph-level metrics.
4. GPU transfer metrics.
5. Kernel metrics.
6. Memory metrics.
7. Queue metrics.
8. Diagnostics overhead.
9. Benchmark gaps.
10. Instrumentation to add.

Separate:

Graph overhead
DSP overhead
Transfer overhead
Kernel overhead
Diagnostics overhead

Rank missing instrumentation by importance.

Stop before proposing optimizations.

=======

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_PERFORMANCE_AUDIT.md report
  plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR8_*_REPORT.md
- Current repository state
- Current diff
- current PR roadmap
- current verifier reports
- current repository state

Task:
Convert the Performance Auditor findings into reviewable PRs.

Rules:
- Instrumentation before optimization.
- Measurements before tuning.
- One concern per PR.
- Do not change SAR math unless required for measurement.
- Do not optimize yet unless the finding is already proven by benchmark data.
- Preserve AccelControlToken<SarSidecar> architecture.

Output:
1. Findings classified as:
   - instrumentation gap
   - benchmark gap
   - confirmed bottleneck
   - suspected bottleneck
   - premature optimization
2. PR-sized roadmap.
3. For each PR:
   - title
   - scope
   - files to touch
   - tests to add
   - metrics expected
   - acceptance criteria
4. Items explicitly deferred.