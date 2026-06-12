
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

=====

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement RRP1 only.

RRP1 title:
Add Deterministic IQ / Phase-History Fixture.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- examples/SAR/scenarios/scenario_001.json
- examples/SAR/scenarios/scenario_001.md
- current repository state
- current RRP0 verifier report, if available

Repository inspection overrides assumptions.

RRP1 purpose:
Provide a tiny deterministic IQ / phase-history fixture suitable for image-formation validation of Scenario 001.

This fixture will later feed:

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

Scope:
Create the deterministic fixture and its metadata/provenance only.

Do not implement RRP2.
Do not implement CPU reference backprojection.
Do not implement GraphX runner changes.
Do not implement comparator metrics.
Do not add external packages.
Do not download external data.
Do not modify accel-token architecture.
Do not modify Metal/GPU nodes.
Do not modify SAR math beyond fixture-generation utilities if needed.

Required fixture location:
- examples/SAR/fixtures/scenario_001/

Required files:
- examples/SAR/fixtures/scenario_001/fixture_manifest.json
- examples/SAR/fixtures/scenario_001/provenance.md
- examples/SAR/fixtures/scenario_001/checksums.txt

Also add one deterministic fixture data artifact, using the repository’s preferred small-data convention.

Preferred fixture data options, in order:
1. If the repo already has a small fixture format for SAR replay data, use that format.
2. If the repo already uses JSON fixtures, use a compact JSON fixture.
3. Otherwise use a simple portable text or binary format with a JSON sidecar.

Fixture requirements:
- deterministic
- small enough for CI
- generated without external packages
- generated without network access
- generated without CUDA
- suitable for Scenario 001
- includes enough IQ / phase-history-like data to support a future CPU reference backprojection test
- documented as synthetic/deterministic, not real GOTCHA or Sentinel data

Fixture content should include or describe:
- scenario_id
- fixture_id
- pulse count or pulse range matching Scenario 001, unless Scenario 001 explicitly allows a reduced fixture profile
- range bin count or range bin range matching Scenario 001, unless Scenario 001 explicitly allows a reduced fixture profile
- sample layout
- complex sample encoding
- coordinate or platform geometry fields needed by a future CPU reference
- units
- generation seed or deterministic formula
- expected dimensions
- checksum

If Scenario 001 requests dimensions too large for a committed tiny fixture:
- do not silently change Scenario 001
- add a clearly named reduced fixture profile inside the fixture manifest
- document that this is the CI fixture profile for Scenario 001
- ensure the manifest links back to Scenario 001

Required test behavior:
Add or update tests so they validate:
- fixture manifest exists
- provenance exists
- checksum file exists
- fixture data exists
- fixture scenario_id matches Scenario 001
- fixture dimensions are positive and bounded
- fixture format/schema version exists
- fixture checksum is stable
- fixture is explicitly synthetic/deterministic
- fixture does not require external packages or external data

Use existing repository JSON parsing or fixture loading utilities if available.
Do not introduce a new third-party dependency for fixture validation.

Preferred test name:
- test_scenario_fixture.cpp

Acceptance criteria:
1. The Scenario 001 fixture directory exists.
2. Fixture manifest, provenance, checksums, and data artifact exist.
3. Fixture is deterministic and small enough for CI.
4. Fixture manifest ties the fixture to Scenario 001.
5. Fixture provenance states that it is synthetic/deterministic and not externally sourced.
6. Fixture validation tests pass.
7. Full relevant CTest lane remains green.
8. No external package, dataset download, CUDA, SarPy, ISCE3, or gotcha-back dependency is introduced.
9. No GraphX core, accel-token, GPU, or SAR math changes are introduced.

Required output:
1. Files added.
2. Files changed.
3. Fixture format chosen and why.
4. Tests added or updated.
5. Build command run.
6. Test command run.
7. Any issues encountered.
8. Explicit confirmation that RRP2+ work was not implemented.

======

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Verify RRP1 only.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- examples/SAR/scenarios/scenario_001.json
- current RRP1 diff
- current test output

Required checks:
1. `examples/SAR/fixtures/scenario_001/` exists.
2. Fixture manifest exists.
3. Provenance document exists.
4. Checksum file exists.
5. Fixture data artifact exists.
6. Fixture scenario_id matches Scenario 001.
7. Fixture is synthetic/deterministic.
8. Fixture is small enough for CI.
9. Fixture validation tests exist and pass.
10. No external data, external packages, network download, CUDA, SarPy, ISCE3, or gotcha-back dependency was introduced.
11. No GraphX core, accel-token, GPU, or SAR math changes were introduced.
12. RRP2+ was not implemented.

Fail if the implementation adds external dependencies, changes Scenario 001 silently, or starts CPU reference/comparator work.

======

Act as IMPLEMENTER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement RRP2 only.

RRP2 title:
Add CPU Reference Backprojection.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- examples/SAR/scenarios/scenario_001.json
- examples/SAR/scenarios/scenario_001.md
- examples/SAR/fixtures/scenario_001/fixture_manifest.json
- examples/SAR/fixtures/scenario_001/provenance.md
- examples/SAR/fixtures/scenario_001/checksums.txt
- current RRP0 verifier report, if available
- current RRP1 verifier report, if available
- current repository state

Repository inspection overrides assumptions.

RRP2 purpose:
Create a deterministic CPU reference backprojection path for Scenario 001.

This reference output becomes the first image-formation truth artifact for GraphX SAR comparison.

Required flow for this PR:

Scenario 001 deterministic IQ / phase-history fixture
    ↓
CPU reference backprojection
    ↓
Reference image artifact

This PR must not run GraphX SAR for comparison yet. That is RRP3.

Scope:
Add CPU reference backprojection implementation and tests only.

Do not implement RRP3.
Do not run GraphX on the fixture.
Do not implement GraphX artifact parity.
Do not implement external package comparison.
Do not add SarPy.
Do not add ISCE3.
Do not add gotcha-back.
Do not download external data.
Do not require CUDA.
Do not modify accel-token architecture.
Do not modify Metal/GPU nodes.
Do not change existing SAR runtime behavior except where needed to share read-only fixture parsing helpers.

Preferred implementation location:
Use example/test/tooling locations, not core framework libraries.

Preferred locations:
- examples/SAR/tools/
- examples/SAR/test/
- examples/SAR/include/sar/ only if a small reusable example-level helper is needed
- examples/SAR/src/ only if the repo already places SAR example helpers there

Do not promote to:
- libgraph
- libgpu
- libdsp

unless the existing architecture clearly already has an approved location for reference algorithms.

Required implementation:
1. Add a CPU reference backprojection routine that consumes the Scenario 001 fixture.
2. Keep it deterministic and scalar/simple.
3. Prefer clarity over speed.
4. Use double precision internally if practical, or document the precision choice.
5. Emit a normalized reference image artifact compatible with the artifact contract planned in SAR_COMPARE_ROADMAP_UPDATED.md.
6. Emit or validate metadata:
   - scenario_id
   - fixture_id
   - algorithm = cpu_reference_backprojection
   - image width
   - image height
   - dtype
   - layout
   - checksum or deterministic hash if supported
7. Keep the reference output small enough for CI.

Algorithm requirements:
- Implement physically meaningful backprojection at the fixture scale.
- Use the fixture’s platform/geometry/sample definitions.
- Do not use placeholder math unless the fixture itself is explicitly synthetic and the approximation is documented.
- If required fields are missing from the fixture, stop and report exactly which fields are missing instead of inventing hidden assumptions.
- If a minimal approximation is necessary, document:
  - equation used
  - units
  - assumptions
  - expected limitations

Output artifact:
Use the repository’s existing artifact convention if available.
If none exists, use:
- float32 row-major raster data
- JSON sidecar metadata
- deterministic checksum

Required tests:
Add or update tests so they validate:
1. CPU reference backprojection can load Scenario 001 fixture.
2. Output image dimensions match Scenario 001 or the fixture’s declared reduced profile.
3. Output artifact metadata is present.
4. Output is deterministic across repeated runs.
5. Output contains finite values only.
6. Output is not all zeros.
7. Peak location is stable for the deterministic fixture, if fixture target geometry supports that.
8. No external data, CUDA, or external package is required.

Preferred test name:
- test_cpu_reference_backprojection.cpp

Acceptance criteria:
1. CPU reference backprojection exists and is documented.
2. It consumes Scenario 001 fixture data.
3. It emits a normalized reference image artifact or validates the in-memory equivalent.
4. It is deterministic.
5. Tests prove fixture load, output dimensions, finite output, nonzero output, and repeatability.
6. Full relevant CTest lane remains green.
7. No external package, dataset download, CUDA, SarPy, ISCE3, or gotcha-back dependency is introduced.
8. No GraphX core, accel-token, GPU, Metal, or resolver architecture changes are introduced.
9. RRP3+ work is not implemented.

Important boundary:
This PR creates the reference image side of the comparison only.

It does not yet create:

Scenario 001 fixture
    ↓
GraphX SAR
    ↓
GraphX image artifact

That is RRP3.

Required output:
1. Files added.
2. Files changed.
3. CPU reference algorithm summary.
4. Equation/assumptions used.
5. Output artifact format.
6. Tests added or updated.
7. Build command run.
8. Test command run.
9. Any missing fixture fields or assumptions.
10. Explicit confirmation that RRP3+ work was not implemented.

====

Act as VERIFIER using plan/agents/GRAPHX_SAR_AGENT_ROLES.md.

Task:
Verify RRP2 only.

Inputs:
- `plan/reviews/SAR_INSPECTOR_REPORT.md`
- `plan/reviews/SAR_PLANNER_REPORT.md`
- `plan/roadmap/SAR_COMPARE_ROADMAP.md`
- examples/SAR/scenarios/scenario_001.json
- examples/SAR/fixtures/scenario_001/fixture_manifest.json
- current RRP2 diff
- current test output

Required checks:
1. CPU reference backprojection exists.
2. It consumes Scenario 001 fixture data.
3. It emits or validates a normalized reference image artifact.
4. Output image dimensions match the scenario or declared fixture profile.
5. Output values are finite.
6. Output is not all zeros.
7. Output is deterministic across repeated runs.
8. Tests exist and pass.
9. No external data, network download, CUDA, SarPy, ISCE3, or gotcha-back dependency was introduced.
10. No GraphX core, accel-token, GPU, Metal, resolver, or SAR runtime architecture changes were introduced.
11. RRP3+ was not implemented.

Fail if:
- the implementation uses placeholder math without documenting it,
- the implementation invents missing fixture fields silently,
- the reference depends on GraphX runtime output,
- or the PR starts GraphX-vs-reference comparison work.

=====

Act as IMPLEMENTER using GRAPHX_SAR_AGENT_ROLES.md.

Task:
Implement RRP3 only.

RRP3 title:
Run GraphX on Scenario 001 and Emit Normalized GraphX Artifact.

Inputs:
- SAR_INSPECTOR_REPORT.md
- SAR_PLANNER_REPORT.md
- SAR_COMPARE_ROADMAP.md
- scenario_001.json
- scenario_001.md
- fixture_manifest.json
- deterministic_iq_phase_history_fixture_v1.json
- RRP0 and RRP1 verifier results if present
- RRP2 outputs/code if present
- current repository state

Repository inspection overrides assumptions.

RRP3 purpose:
Create the GraphX image side of the Scenario 001 flow and emit a normalized GraphX artifact contract that can be compared later.

Required flow for this PR:

Scenario 001 deterministic fixture
-> GraphX SAR pipeline
-> GraphX image artifact and GraphX artifact contract

Important boundary:
Do not implement comparator metrics or pass/fail comparison logic here. That belongs to later PRs.

Scope:
Implement GraphX execution for Scenario 001 fixture plus GraphX artifact emission and tests only.

Do not implement RRP4+.
Do not implement comparison metrics.
Do not add SarPy, ISCE3, or gotcha-back integration.
Do not download external data.
Do not require CUDA.
Do not modify GraphX core architecture unless absolutely necessary and clearly justified.
Do not modify accel-token contracts.
Do not modify Metal or GPU node architecture.
Do not alter SAR math kernels for optimization.

Preferred implementation locations:
- tools
- test
- sar only for small reusable example-level helpers
- src only if the existing repository patterns require it

Do not promote logic into:
- libgraph
- libgpu
- libdsp

Required implementation:
1. Load Scenario 001 and fixture data.
2. Execute GraphX SAR path for the scenario using the existing example/runtime path.
3. Materialize GraphX output raster artifact in float32 row-major format.
4. Emit GraphX contract JSON with at least:
   - source_tool = graphx
   - provenance_class = graphx_runtime
   - scenario_id
   - fixture_id when available
   - algorithm or pipeline label for GraphX path
   - width
   - height
   - dtype
   - layout
   - format
   - artifact_kind
   - byte_count
   - raw_path
   - deterministic hash/checksum if supported
5. Ensure deterministic output for repeated runs on the same fixture/config.
6. Keep artifact small enough for CI lane usage.

Contract compatibility requirement:
Align GraphX artifact fields with existing artifact consumers so later comparator wiring is straightforward. Reuse existing contract conventions in the repo where available.

Required tests:
Add or update tests to validate:
1. GraphX Scenario 001 runner executes with local fixture and no external dependencies.
2. GraphX artifact file is emitted.
3. GraphX contract file is emitted and contains required fields.
4. Contract scenario_id matches Scenario 001.
5. Dimensions are valid and match expected run output.
6. Raster contains finite values.
7. Raster is not all zeros.
8. Output is deterministic across repeated runs.
9. No external data/network/CUDA/external package requirement.

Preferred test name:
test_rrp3_graphx_scenario_runner.cpp

If a similar test already exists:
extend it minimally instead of duplicating behavior.

Acceptance criteria:
1. GraphX Scenario 001 run path exists and is documented.
2. GraphX artifact and contract are emitted in normalized format.
3. Tests verify deterministic, finite, nonzero output and metadata correctness.
4. Full relevant CTest lane remains green.
5. No external package, dataset download, CUDA, SarPy, ISCE3, or gotcha-back dependency is introduced.
6. No GraphX core, accel-token, GPU, Metal, resolver, or SAR runtime architecture changes are introduced unless explicitly unavoidable and documented.
7. RRP4+ is not implemented.

Failure handling requirements:
- If required scenario or fixture fields are missing, fail with explicit field-level error messages.
- Do not invent missing fields silently.
- Do not substitute GraphX runtime output with synthetic placeholders.

Required output format from IMPLEMENTER:
1. Files added.
2. Files changed.
3. GraphX runner summary.
4. Artifact format and contract schema summary.
5. Determinism method/checksum approach.
6. Tests added or updated.
7. Build command run.
8. Test command run.
9. Any missing fields, assumptions, or blockers.
10. Explicit confirmation that RRP4+ work was not implemented.