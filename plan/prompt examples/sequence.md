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
- plan/reviews/SAR_PR1_VERIFIER_REPORT_2.md



Task:
Implement PR1 only.

PR1 title:
Introduce Canonical Token and Sidecar Types
PR1 scope:
Apply required fixes for PR1 only:
remove host_ptr identity transport,
remove ready_event identity transport,
remove global sidecar store as runtime dependency,
and migrate runtime node interfaces to explicit AccelControlToken<SarSidecar>.

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
