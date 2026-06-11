Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR roadmap for the reference reproduction pipeline.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- current GraphX SAR repository state
- current SAR topology and source/output nodes

Goal:
Use one external SAR reference package and dataset to reproduce a known input/output pair through GraphX SAR.

Preferred target:
gotcha-back using AFRL GOTCHA Challenge Problem data, unless repository inspection shows a better immediate candidate.

Required direction:
External input data
    → GraphX ingest node
    → GraphX SAR image output
    → comparator against external reference image

Do not create governance-only PRs.
Do not start with SarPy unless it directly supports the chosen input/output reproduction path.
Do not download external repositories in the first PR.
Do not require external data in CI.

The roadmap must include an explicit RRP0 PR that materializes the first immutable reproduction scenario directly in the repository.

RRP0 must create, in-place:

- examples/SAR/scenarios/scenario_001.json
- examples/SAR/scenarios/scenario_001.md
- examples/SAR/test/test_scenario_manifest.cpp

RRP0 is not optional and must come before any external runner, comparator, gotcha-back adapter, or CI fixture work.

RRP0 must freeze the experiment before any runner/comparator work.

RRP0 acceptance criteria:

1. `scenario_001.json` exists and defines:
   - version
   - dataset
   - pulse_range
   - range_bins
   - image_grid
   - scene_center
   - algorithm
   - window
   - range_compression
   - output

2. `scenario_001.md` exists and documents:
   - purpose
   - dataset
   - pulse range
   - range bins
   - output format
   - algorithm
   - immutability rule

3. `test_scenario_manifest.cpp` exists and validates:
   - required fields are present
   - version is supported
   - malformed or incomplete scenario manifests are rejected

4. RRP0 must not:
   - download GOTCHA data
   - clone gotcha-back
   - add SarPy
   - add ISCE3
   - implement image comparison
   - modify SAR math
   - alter accel-token architecture

5. RRP0 output must be a reviewable PR containing only scenario definition and manifest validation.

Required output:
1. Chosen external package and dataset.
2. Exact first reproduction target.
3. RRP0 scenario manifest PR.
4. Required input files.
5. Required reference output files.
6. GraphX nodes/adapters needed.
7. Image output format.
8. Comparator metrics.
9. CI-safe fixture strategy.
10. Local-only large-data strategy.
11. PR-sized implementation roadmap.

====
Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement RRP0 only.

Create these files in-place:

- examples/SAR/scenarios/scenario_001.json
- examples/SAR/scenarios/scenario_001.md
- examples/SAR/test/test_scenario_manifest.cpp

Do not implement RRP1.
Do not download external data.
Do not clone gotcha-back.
Do not add comparator metrics.
Do not modify SAR math.
Do not modify accel-token architecture.

Run build and tests.

=======

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_VALIDATE_ROADMAP.md
- plan/reviews/SAR_IMPL_RRP2_1.md
- current repository state
- current test output

Task:
Implement RRP3 only

RRP3 title:  gotcha-back Scenario Adapter
RRP3 Scope:
- Convert `scenario_001` into the pinned gotcha-back invocation and normalize its output artifact.



Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if RRP3 requires it.
- Add or update tests for RRP3.
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

RRP3 acceptance criteria:
- gotcha-back output is reproducibly mappable to the comparison format for `scenario_001`
=====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_VALIDATE_ROADMAP.md
- plan/reviews/SAR_IMPL_RRP0_1.md
- plan/reviews/SAR_IMPL_RRP1_1.md
- plan/reviews/SAR_IMPL_RRP2_1.md
- current repository state
- current test output
- current RRP2 diff



Task:
Verify whether RRP2  actually satisfies its acceptance criteria.

Check:
- GraphX can produce one scenario-driven image artifact without changing SAR math architecture


Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.
