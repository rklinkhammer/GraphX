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
- move toward exactly one canonical SAR GPU path.

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

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_SIMPLIFIER_REPORT.md
- SAR_PR_ROADMAP.md
- implemented PR3 diff

Task:
Verify whether PR3 actually satisfies its acceptance criteria.

Check:
- Definitive topology executes with tokenized SAR GPU stages.
- Strict-metal and fallback resolver tests pass.

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