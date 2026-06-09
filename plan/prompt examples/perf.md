Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/reviews/SAR_PR_ROADMAP.md
- plan/reviews/SAR_VERIFIER_PR1_!_REPORT.md
Task:
Implement PR1 only.

PR1-A1 title:  Canonical SAR Stage Timing Spans
PR1-A1 Scope:
 - Add explicit stage timing measurements for the definitive SAR path.
- Export them through diagnostics and benchmark trace.
- Do not change SAR math or transport contracts.

Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if PR1-A1 requires it.
- Add or update tests for PR1-A1.
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
