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
- plan/reviews/SAR_IMPL_RRP6_1.md
- current repository state
- current test output

Task:
Implement RRP7 only

RRP7 title:  CI-Safe Validation Lane
RRP7 Scope:
- Add a bounded CI lane that validates the tiny scenario-derived fixture and comparison thresholds.


Rules:
- Do not redesign.
- Do not broaden scope.
- Do not preserve obsolete behavior.
- Do not add compatibility shims.
- Do not touch future-PR items.
- Delete obsolete code if RRP7 requires it.
- Add or update tests for RRP7.
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

RRP7 acceptance criteria:
- CI validates the reproduction path without external data download

=====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_VALIDATE_ROADMAP.md
- plan/reviews/SAR_IMPL_RRP4_1.md
- current repository state
- current test output
- current RRP4 diff



Task:
Verify whether RRP4  actually satisfies its acceptance criteria.

Check:
- comparison produces deterministic metrics and structured pass/fail outputs



Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.


====

Act as PRINCIPAL_ARCHITECT using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR2_INSPECTOR.md
- plan/reviews/SAR2_SIMPLIFIER.md
- plan/reviews/SAR2_ARCHITECT.md
- current repository state



Analyze the current repository only. Do not redesign or implement. Stop after the current-state report.
=========

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR2_INSPECTOR.md
- plan/reviews/SAR2_SIMPLIFIER.md
- plan/reviews/SAR2_ARCHITECT.md
- current repository state



Analyze the current repository only. Do not redesign or implement. Stop after the current-state report.

========

Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- plan/reviews/SAR2_INSPECTOR.md
- plan/reviews/SAR2_SIMPLIFIER.md
- plan/reviews/SAR2_ARCHITECT.md
- current repository state
- current verifier reports, if available

Task:
Convert the SAR2 Architect Report into a new reviewable PR sequence.

Repository inspection overrides assumptions.

Do not implement code.
Do not reinspect broadly unless needed to clarify a specific planning item.
Do not create governance-only PRs.
Do not propose large rewrites.
Do not mix cleanup, SAR correctness, performance, and external baseline work in one PR.

Current architecture checkpoint:
- `AccelControlToken<SarSidecar>` is now the canonical SAR runtime contract.
- The SAR validation layer already includes benchmark tracing, external-baseline policy/registry, comparator tooling, replay guide, tiny fixture, and bounded CI lane.
- The working tree is clean.
- The remaining pressure is cleanup, semantic clarification, and measurable SAR correctness.

Planning priorities:
1. Remove or explicitly retire dormant helper types:
   - `SarMessageEnvelope`
   - `SarBufferDescriptor`
   - `SarGpuMetadata`
2. Clarify whether `host_ptr` and `ready_event` are retained as opaque transport metadata or removed from SAR identity/transport semantics.
3. Consolidate duplicated helper code:
   - repeated `ElapsedUs(...)`
   - duplicated `ResolveDiagnosticsSink(...)`
4. Preserve and strengthen existing layered validation artifacts.
5. Move from validation scaffolding toward SAR correctness:
   - meaningful tiny fixture checks
   - CPU reference backprojection parity
   - GraphX-vs-reference artifact comparison
   - explicit image-quality thresholds
6. Avoid external-package architecture pollution.

Required output:
1. Executive planning recommendation.
2. Proposed next 3–6 PRs.
3. For each PR:
   - title
   - purpose
   - files likely touched
   - files likely deleted
   - tests to add
   - tests to update/delete
   - acceptance criteria
   - verifier checks
   - CI-safe or local-only
   - risk level
4. Items explicitly deferred.
5. Things not to do.

Planner bias:
Prefer the next PR to be small and cleanup-oriented if it reduces ambiguity in the canonical token model.

Do not propose another baseline registry PR.
Do not add SarPy, ISCE3, or gotcha-back integration unless the PR directly advances artifact comparison or reproduction testing.

=========

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- `plan/reviews/SAR2_INSPECTOR.md`
- `plan/reviews/SAR2_SIMPLIFIER.md`
- `plan/reviews/SAR2_ARCHITECT.md`
- current repository state
- current test output

Task:
Implement PR5 only

PR5 title:  Local Runner-to-Comparator Integration for `scenario_001` Artifacts
PR5 Scope:
- Close the RRP4 non-blocking gap by wiring scenario-driven artifact production to comparator invocation in one local-only reproducible flow.

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

PR5 acceptance criteria:
 - One documented command path (or short sequence) materializes both artifact contracts and runs comparator with structured pass/fail output.
  - CI does not require external dataset download.
  - Local-only runbook is reproducible without reverse engineering.
  - Comparator report schema and deterministic metrics remain 
========

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Inputs:
- `plan/reviews/SAR2_INSPECTOR.md`
- `plan/reviews/SAR2_SIMPLIFIER.md`
- `plan/reviews/SAR2_ARCHITECT.md`
- `plan/reviews/SAR2_IMPL_PR4_1.md`
- current repository state
- current test output



Task:
Verify whether PR5 actually satisfies its acceptance criteria.

Check:
 - One documented command path (or short sequence) materializes both artifact contracts and runs comparator with structured pass/fail output.
  - CI does not require external dataset download.
- Verifier checks:
  - Local-only runbook is reproducible without reverse engineering.
  - Comparator report schema and deterministic metrics remain 
  
Output:
- Pass/fail.
- Blocking issues.
- Non-blocking issues.
- Suggested fixes.