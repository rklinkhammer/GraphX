
Act as PLANNER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Create a PR roadmap for comparing the GraphX SAR pipeline against existing SAR implementations using the same input data and produced output artifacts.

Goal:
Select one or more existing SAR implementations — candidate packages include SarPy, ISCE3, gotcha-back, SNAP/snappy, pyroSAR, or other suitable open SAR tools — then define a concrete plan to:

1. Download and set up the selected package locally.
2. Obtain or generate a known SAR dataset.
3. Run the external implementation on that dataset.
4. Run GraphX SAR on the same dataset or normalized equivalent.
5. Compare produced outputs using explicit artifact formats and metrics.

This is a planning task only.
Do not implement code.

Repository inspection overrides assumptions.

Primary constraints:
- GraphX architecture must remain token/DAG/capability based.
- External packages are reference implementations, not architectural templates.
- No external package types may leak into GraphX core.
- External package setup must be isolated under tools, scripts, adapters, or local-only test harnesses.
- Large datasets must not be committed.
- Network downloads must not be required for normal CI.
- CI may use tiny derived fixtures only if legally redistributable.
- Local-only tests may require manually downloaded datasets and external packages.

Platform constraint:

The development and comparison environment is macOS on Apple Silicon.

CUDA is not available.

Do not select an external package or first comparison path that requires CUDA, NVIDIA GPUs, nvcc, cuFFT, or CUDA-only build tooling.

External packages may be:

- pure Python
- CPU-only C/C++
- Metal-compatible
- OpenMP/CPU
- NumPy/SciPy-based
- Conda/pip installable on macOS
- local command-line tools that run without CUDA

If a package has optional CUDA acceleration, it may only be considered if it has a verified CPU-only path on macOS.

If a package is CUDA-only, classify it as:

Not suitable for first macOS comparison.

Do not make gotcha-back the first implementation target if its usable path requires CUDA on macOS.

gotcha-back may remain a documentation/reference algorithm source or future Linux/NVIDIA local-only benchmark, but it must not be the first required comparison lane for this macOS plan.

Planning requirements:

1. Candidate package assessment


Evaluate at least:

- SarPy
- ISCE3
- gotcha-back
- SNAP/snappy
- pyroSAR
- any other package discovered during inspection

For each candidate report:

- repository URL
- license
- install method
- language/runtime
- dependency burden
- supported input formats
- produced output formats
- whether it supports raw phase history
- whether it supports SLC/product-level processing
- whether it can produce image artifacts comparable to GraphX
- whether GraphX can reasonably consume the same input
- whether outputs are numeric arrays, complex images, magnitude rasters, metadata products, or geospatial products
- CI suitability
- local-only suitability
- architecture-pollution risk

2. Dataset assessment

Evaluate available SAR datasets, including:

- AFRL GOTCHA Challenge Problem
- Sentinel-1 SLC products
- UAVSAR
- Capella or other open SLC datasets if available
- MSTAR if useful
- any package-provided test fixtures

For each dataset report:

- access path
- license/use constraints
- redistribution limits
- size
- input format
- output/reference format if available
- metadata completeness
- whether raw IQ/phase history is available
- whether only SLC or image products are available
- compatibility with GraphX SAR
- compatibility with SarPy
- compatibility with ISCE3
- compatibility with gotcha-back
- CI suitability
- local benchmark suitability

3. Pairing matrix

Create a matrix:

external package
×
dataset
×
input format
×
output artifact
×
GraphX feasibility

Classify each pairing:

- Preferred
- Viable
- Local-only
- CI-safe
- Not suitable
- Unknown pending experiment

4. Recommended first comparison

Choose exactly one first comparison target.

The first target must define:

- external package
- dataset
- input files
- external runner command
- GraphX runner command
- normalized output format
- comparison metrics
- local-only or CI-safe status
- expected blockers

Prefer a target that produces real image-formation evidence, not just metadata validation.

If gotcha-back + AFRL GOTCHA is the best image-formation target, say so.
If SarPy or ISCE3 is better for available data/outputs, justify why.

5. Required GraphX additions

Identify GraphX changes needed for the chosen comparison:

- source/ingest node
- converter/adapter
- scenario manifest
- image output sink
- metadata sidecar fields
- comparator tool
- metrics
- local runner script
- CI fixture strategy

Do not propose GraphX core changes unless unavoidable.

6. External setup plan:

For each selected external package, define:

- install location
- macOS Apple Silicon install method
- CPU-only execution path
- version pinning
- build command
- smoke test command
- expected outputs
- failure diagnostics
- license capture
- local-only documentation

Reject setup paths requiring:

- CUDA
- nvcc
- NVIDIA drivers
- cuFFT
- Linux-only containers
- x86-only binaries

Preferred locations:

tools/external/
examples/SAR/tools/
examples/SAR/scenarios/
examples/SAR/fixtures/
examples/SAR/test/

7. PR roadmap

Produce PR-sized steps.

The roadmap must include:

RRP0:
Freeze one immutable comparison scenario.

RRP1:
Local external package setup script and smoke test.

RRP2:
External reference runner for selected dataset/scenario.

RRP3:
GraphX runner for the same scenario.

RRP4:
Normalized image/output artifact format.

RRP5:
Comparator metrics implementation.

RRP6:
Tiny derived CI fixture, if legally allowed.

RRP7:
CI-safe comparison lane.

RRP8:
Optional larger local benchmark lane.

For each PR include:

- title
- purpose
- files to add/change/delete
- tests to add
- local-only or CI-safe classification
- acceptance criteria
- verifier checks
- risks
- explicit non-goals

8. Output requirements

Start with:

- recommended first package/dataset pair
- confidence level
- top blockers
- top unknowns

Then provide:

- package assessment table
- dataset assessment table
- package/dataset pairing matrix
- recommended first comparison
- GraphX additions required
- external setup plan
- PR roadmap
- things not to do

Non-negotiable rule:

The plan must result in this concrete flow on macOS without CUDA:

Known SAR input data
    ↓
External SAR implementation, CPU-only/macOS-compatible
    ↓
External output artifact

Known SAR input data
    ↓
GraphX SAR pipeline, Metal or CPU path
    ↓
GraphX output artifact

External output artifact + GraphX output artifact
    ↓
Comparator
    ↓
metric report

Do not return only a governance or policy roadmap.
=====

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement RRP0 only.

RRP0 title:
Freeze Deterministic Image-Formation Scenario.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- current repository state

Repository inspection overrides assumptions.

RRP0 purpose:
Create the immutable scenario used for the first fair GraphX SAR image-formation correctness comparison.

Required files to create in-place:
- examples/SAR/scenarios/scenario_001.json
- examples/SAR/scenarios/scenario_001.md
- examples/SAR/test/test_scenario_manifest.cpp

Scenario intent:
Scenario 001 defines a deterministic IQ/phase-history image-formation comparison target for:

Known deterministic IQ/phase-history fixture
    ↓
CPU reference backprojection
    ↓
Reference image artifact

Known deterministic IQ/phase-history fixture
    ↓
GraphX SAR pipeline
    ↓
GraphX image artifact

Reference image + GraphX image
    ↓
Comparator
    ↓
Metric report

Required scenario_001.json content:
- schema/version field
- scenario_id
- comparison_level = "internal_image_formation_correctness"
- fixture identity
- pulse count or pulse range
- range bin count or range bin range
- image grid:
  - width
  - height
  - pixel spacing or extent
- algorithm:
  - type = "backprojection"
  - reference = "cpu_reference_backprojection"
  - graphx = "graphx_sar_pipeline"
- output artifact contract:
  - format
  - dtype
  - layout
  - dimensions
- comparator profile:
  - strict or deterministic
  - metrics expected later
- immutability rule:
  - modifications require scenario_002

Required scenario_001.md content:
- purpose
- comparison level
- fixture description
- expected future flow
- output artifact contract
- immutability rule
- explicit statement that RRP0 does not download data or run external packages

Required test behavior:
Add or update `test_scenario_manifest.cpp` so it validates:
- scenario file exists
- required fields exist
- scenario_id is deterministic
- version/schema field exists
- comparison level is present
- algorithm type is backprojection
- output artifact contract is present
- malformed or incomplete scenario manifests are rejected if repository test utilities support that
- scenario_001 is treated as immutable by documentation/test naming

Use existing repository JSON parsing utilities if available.
If no suitable JSON parser/test helper exists, use the repository’s existing manifest/config parsing style.
Do not introduce a new third-party dependency for this test.

Scope restrictions:
- Do not implement RRP1.
- Do not add IQ fixture data.
- Do not implement CPU reference backprojection.
- Do not implement comparator metrics.
- Do not add SarPy.
- Do not add ISCE3.
- Do not add gotcha-back.
- Do not download external data.
- Do not modify GraphX core architecture.
- Do not modify accel-token runtime contracts.
- Do not modify SAR math.
- Do not modify Metal/GPU nodes.
- Do not create policy-only files unrelated to Scenario 001.

Acceptance criteria:
1. The three required files exist in the correct repository locations.
2. Scenario 001 fully defines the deterministic image-formation comparison scenario.
3. Scenario 001 explicitly states that it is immutable after acceptance.
4. The test validates the scenario manifest structure.
5. The full relevant CTest lane remains green.
6. No external package or dataset dependency is introduced.
7. No CUDA dependency is introduced.
8. No GraphX core architecture changes are introduced.

Required output:
1. Files added.
2. Files changed.
3. Tests added or updated.
4. Build command run.
5. Test command run.
6. Any issues encountered.
7. Explicit confirmation that RRP1+ work was not implemented.

=======

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Verify RRP0 only.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- current repository state
- current RRP0 diff
- current test output

Required checks:
1. `examples/SAR/scenarios/scenario_001.json` exists.
2. `examples/SAR/scenarios/scenario_001.md` exists.
3. `examples/SAR/test/test_scenario_manifest.cpp` exists or was updated.
4. Scenario 001 declares comparison level:
   `internal_image_formation_correctness`.
5. Scenario 001 defines fixture identity, pulse/range selection, image grid, algorithm, output artifact contract, comparator profile, and immutability rule.
6. No external packages or data downloads were added.
7. No CUDA dependency was added.
8. No GraphX core, accel-token, GPU, or SAR math changes were introduced.
9. Tests compile and pass.

Fail if the implementation starts RRP1, adds external dependencies, or leaves Scenario 001 underspecified.

=====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Verify RRP0 only.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- current RRP0 diff
- current test output

Required checks:
1. `examples/SAR/scenarios/scenario_001.json` exists.
2. `examples/SAR/scenarios/scenario_001.md` exists.
3. `examples/SAR/test/test_scenario_manifest.cpp` exists or was updated.
4. Scenario 001 declares comparison level:
   `internal_image_formation_correctness`.
5. Scenario 001 defines fixture identity, pulse/range selection, image grid, algorithm, output artifact contract, comparator profile, and immutability rule.
6. No external packages or data downloads were added.
7. No CUDA dependency was added.
8. No GraphX core, accel-token, GPU, or SAR math changes were introduced.
9. Tests compile and pass.

Fail if the implementation starts RRP1, adds external dependencies, or leaves Scenario 001 underspecified.