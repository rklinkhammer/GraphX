Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Goal:
Plan a small, reviewable implementation path for GraphX SAR to read CRSD files and produce fully focused SAR images that can be verified against images produced from the same CRSD files using SarPy/reference tooling.

Inputs:
- Current repository state
- examples/SAR existing graph/node/plugin patterns
- Existing CSV input node support patterns in GraphX
- Existing SAR example configs and tests
- Existing GOTCHA-to-CRSD output artifacts and docs
- Existing SarPy tools under tools/sarpy

Task:
Create a PR plan only. Do not implement code.

Required investigation:
1. Inspect how CSV input/source nodes are implemented, configured, tested, and registered.
2. Inspect SAR node conventions:
   - node headers/sources
   - plugin registration
   - JSON config fields
   - test framework
   - fixture conventions
   - diagnostics/output artifact conventions
3. Inspect existing CRSD writer/validator/SarPy tooling to understand available CRSD metadata, signal, PVP, and reference image hooks.
4. Identify whether existing GraphX SAR backprojection can consume CRSD-derived phase history directly or whether a CRSD-specific adapter/model is needed.
5. Identify what “fully focused image” should mean for this repository:
   - image formation algorithm
   - required CRSD metadata/PVP fields
   - output image shape and coordinate assumptions
   - deterministic tolerances for validation
6. Identify how SarPy should be used:
   - as local-only reference image generator
   - as validation/comparison harness
   - not as a GraphX runtime dependency
7. Identify whether config-based input file paths are sufficient for the first CRSD source node, or whether a CSV-style source node pattern should be copied more broadly.

Planning rules:
- Do not redesign GraphX runtime contracts.
- Do not add MATLAB dependency.
- Do not require real GOTCHA data in CI.
- Do not require SarPy in normal CI.
- Keep SarPy workflows local-only or gated.
- Prefer one clear CRSD source node over compatibility shims.
- Prefer config parameters for the first CRSD file input path if that matches existing source-node patterns.
- The GraphX image output must be a real focused SAR image, not a CRSD signal magnitude quick-look.
- The same CRSD input must be usable for both GraphX image formation and SarPy/reference verification.
- Tests must use tiny deterministic CRSD fixtures in CI.
- Real GOTCHA-derived CRSD validation must be local-only and explicitly enabled.

Required planned capabilities:
1. `CrsdInputSourceNode` or equivalent GraphX SAR source node:
   - accepts CRSD path via JSON config
   - reads CRSD metadata, signal array, and PVP needed for image formation
   - emits SAR phase-history messages/tokens compatible with downstream SAR processing or a planned adapter
2. Focused SAR image formation path:
   - converts CRSD phase history into a focused image using GraphX SAR nodes or a new focused-image transform if needed
   - defines image dimensions, geometry assumptions, and deterministic output format
3. Image output sink:
   - writes focused image artifact(s), preferably deterministic binary/JSON plus PNG/PGM convenience output
   - preserves metadata needed for comparison
4. SarPy/reference comparison lane:
   - produces a reference focused image from the same CRSD input
   - compares GraphX output to reference using RMSE, phase error, peak error, correlation, and optional SSIM
   - emits `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png`
5. Config examples:
   - tiny CI CRSD fixture config
   - local GOTCHA-derived CRSD config
6. Documentation:
   - explain CRSD -> GraphX focused image
   - explain SarPy/reference verification
   - distinguish CRSD signal quick-look from focused SAR image

Output:
Save the planner report to:

plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Report format:
1. Executive summary.
2. Current-state findings.
3. CSV input node pattern findings.
4. Target architecture.
5. Required type/model changes.
6. Planned PRs.

For each planned PR include:
- title
- purpose
- files to touch
- files to delete
- tests to add
- tests to delete
- acceptance criteria
- risks
- rollback plan
- CI-safe or local-only classification

Required PR coverage:
1. Repository discovery for CSV/source-node and SAR image-output patterns.
2. CRSD reader/source-node interface and tiny fixture strategy.
3. CRSD-to-SAR phase-history adapter/model.
4. Focused image formation path in GraphX.
5. Deterministic image output sink/artifacts.
6. SarPy/reference focused-image generation harness.
7. GraphX-vs-SarPy comparison lane.
8. Local-only GOTCHA-derived CRSD validation workflow.
9. Documentation and final guardrail verification.

Stop after writing the planner report.
Do not implement code.
Do not create fixtures unless the plan explicitly says later PRs should.




