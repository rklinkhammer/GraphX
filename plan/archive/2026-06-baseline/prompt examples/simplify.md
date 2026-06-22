> ARCHIVAL STATUS (2026-06-14): Historical planning/prompt artifact. It may reference deprecated GraphX SAR conversion lanes or naming. Use plan/prompt examples/doc.md for current CRSD-only operational guidance.

Yes: use **SIMPLIFIER first**, then **PLANNER**.

The role file does not define a “principal architect” agent. This is architecture cleanup, but the decision is straightforward: remove PR-history naming from final repo artifacts. SIMPLIFIER should define the clean target naming model and deletion/rename rules. PLANNER should then split it into reviewable PRs. IMPLEMENTER comes after.

Here is the prompt I would use:

```text
Act as SIMPLIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Goal:
Remove the entire `prXX`, `PRXX`, `rrpXX`, and implementation-history naming convention from generated SAR files, tests, scripts, docs, reports, fixtures, and symbols.

Context:
The repository now contains the final GOTCHA/SAR/CRSD functionality, but many files and tests still preserve intermediate PR-plan names such as `test_pr14_*`, `test_pr16_*`, `test_pr18_*`, `rrp*`, `SAR_IMPL_PR*`, `SAR_VERIFY_PR*`, and similar planning-era labels.

I want the final repository to read like a product/codebase, not like an implementation diary.

Do not implement code.
Do not rename files yet.
Do not delete files yet.
Stop after the simplifier report.

Rules:
- Backward compatibility is not required.
- Complexity is a defect.
- Prefer deletion over compatibility.
- Do not preserve old names because tests reference them.
- Final names should describe product behavior, not PR numbers.
- Keep useful final tests, but rename/reorganize them around capabilities.
- Delete tests/docs that only prove historical PR artifacts existed.
- Preserve current final functionality:
  - GOTCHA sidecar generation
  - GOTCHA input ordering
  - normalized SAR product
  - SAR product validator
  - graphx-crsd-lite lane
  - real CRSD writer lane
  - SarPy validation harness
  - local-only GOTCHA workflow
  - GraphX image comparison lane
- Do not remove graphx-crsd-lite just because real CRSD now exists; it remains a named non-standard format.
- Do not move local-only dataset assumptions into CI.
- Do not redesign GraphX runtime contracts.
- Preserve all the non-pr configs in examples/config
- All historical implement/verify files should be moved to plan/history/reviews ion one step.
- Enact a hard policy that no PR/RRP tokens are allowed enforced by cmake lint.  This is all code, docs, and scripts.  Ignore the legacy references inside history fies moved to plan/history/reviews.
- Benchmark outputs can be renamed freely
- final home for RRP tool docs/scripts under tools/docs should stay in tools
- deprecated definitive variants in sar_stripmap_definitive_metal.json and sar_stripmap_definitive_nonmetal.json rare folded into a single definitive config.


Required inspection:
1. Identify every file, test suite, script, doc, fixture, CMake target, CTest label, symbol, and user-visible string that uses planning-era names:
   - `pr[0-9]+`
   - `PR[0-9]+`
   - `rrp[0-9]+`
   - `RRP[0-9]+`
   - `SAR_IMPL_PR`
   - `SAR_VERIFY_PR`
   - planner/report names that are not intended final documentation
2. Classify each item as:
   - Keep and rename
   - Delete
   - Keep unchanged because it is historical planning material
   - Needs planner decision
3. Define the final naming model for:
   - tests
   - test suites
   - docs
   - scripts
   - local-only workflows
   - generated reports
   - fixtures
   - CMake/CTest labels
4. Identify tests that are intermediate-only and should be removed.
5. Identify tests that are valuable but should be renamed around final behavior.
6. Identify docs/reports that should remain under `plan/reviews` as history versus docs that should move/rename into final `docs/` or `tools/`.
7. Identify any risky renames that may affect build tooling, CMake globbing, CI, or local scripts.

Produce only:

1. Executive recommendation.
2. Final naming model.
3. Keep-and-rename list.
4. Deletion list.
5. Historical-material list.
6. Replacement naming map.
7. Tests to keep, rename, or delete.
8. Docs/reports to keep, rename, move, or delete.
9. Architecture invariants.
10. Open questions that block planning.

Do not produce a PR plan yet.
Do not implement.
```

Then after that report, use PLANNER to turn the rename/delete map into small PRs. This is the kind of cleanup where a planner helps because broad renames can be noisy and you want each PR to stay reviewable.