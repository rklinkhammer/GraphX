# SAR Validate Roadmap

Date: 2026-06-10
Role: PLANNER
Task: Create a PR roadmap for the reference reproduction pipeline.

## 1) Chosen External Package and Dataset

- External package: gotcha-back.
- Dataset: AFRL GOTCHA Challenge Problem data.
- Reason for selection:
  - The repository already contains a GOTCHA replay ingest path in `examples/SAR/src/GotchaReplaySourceNode.cpp`.
  - The repository already contains an image materialization path in `examples/SAR/src/SarMaterializedImageSinkNode.cpp`.
  - The repository already contains a manual external topology scaffold in `examples/SAR/config/sar_gotcha_external_manual.json`.
- SarPy is not chosen first because the required path here is real input to reproduced image output, and gotcha-back is the more direct immediate match for that goal.

## 2) Exact First Reproduction Target

- Freeze one immutable reproduction scenario in-repo first.
- That scenario will define one pinned GOTCHA experiment that later drives:
  - one GraphX run from the same source subset,
  - one gotcha-back reference run,
  - one image-to-image comparison.
- The first external reproduction target after scenario freeze is:
  - one local-only gotcha-back reference image artifact,
  - one local-only GraphX image artifact,
  - one structured comparator result,
  - all driven from `scenario_001`.

## 3) RRP0 Scenario Manifest PR

Title: Freeze Reproduction Scenario 001

Purpose:

- Materialize the first immutable reproduction scenario directly in the repository.
- Freeze the experiment definition before adding any runner, comparator, adapter, or CI fixture work.

Files to add:

- `examples/SAR/scenarios/scenario_001.json`
- `examples/SAR/scenarios/scenario_001.md`
- `examples/SAR/test/test_scenario_manifest.cpp`

`scenario_001.json` must define:

- `version`
- `dataset`
- `pulse_range`
- `range_bins`
- `image_grid`
- `scene_center`
- `algorithm`
- `window`
- `range_compression`
- `output`

`scenario_001.md` must document:

- purpose
- dataset
- pulse range
- range bins
- output format
- algorithm
- immutability rule

`test_scenario_manifest.cpp` must validate:

- required fields are present
- version is supported
- malformed or incomplete scenario manifests are rejected

RRP0 must not:

- download GOTCHA data
- clone gotcha-back
- add SarPy
- add ISCE3
- implement image comparison
- modify SAR math
- alter accel-token architecture

RRP0 acceptance criteria:

1. `scenario_001.json` exists and defines all required fields.
2. `scenario_001.md` exists and documents all required items.
3. `test_scenario_manifest.cpp` exists and rejects malformed manifests.
4. The PR contains only scenario definition and manifest validation work.

Review boundary:

- RRP0 is mandatory.
- RRP0 is not optional.
- RRP0 must merge before any external runner, comparator, gotcha-back adapter, or CI fixture PR.

## 4) Required Input Files

- For RRP0:
  - `examples/SAR/scenarios/scenario_001.json`
  - `examples/SAR/scenarios/scenario_001.md`
- For later local-only reproduction:
  - local AFRL GOTCHA subset files required by gotcha-back for the pinned scenario
  - GraphX normalized replay input aligned to the same pinned scenario
- The scenario manifest must freeze:
  - dataset identity
  - pulse range
  - range bins
  - image grid
  - scene center
  - algorithm family
  - output contract

## 5) Required Reference Output Files

- Later, after RRP0:
  - gotcha-back reference image artifact
  - GraphX image artifact
  - comparator metrics JSON report
  - run metadata JSON with dataset fingerprint and scenario fingerprint
- RRP0 itself adds no reference output artifacts.

## 6) GraphX Nodes/Adapters Needed

- Existing ingest node:
  - `GotchaReplaySourceNode`
- Existing image output node:
  - `SarMaterializedImageSinkNode`
- Existing path anchor:
  - `examples/SAR/config/sar_gotcha_external_manual.json`
- New work after RRP0 should remain outside GraphX core contracts where practical:
  - local runner
  - gotcha-back output adapter
  - comparator tool
- No SAR math redesign.
- No accel-token contract redesign.

## 7) Image Output Format

- Canonical comparison target:
  - float32 single-channel raster with explicit shape metadata
- Acceptable artifact transport formats for the comparison layer:
  - raw float32 + JSON sidecar, or
  - `.npy`
- Debug-only visualization output such as `.pgm` may exist, but must not be the comparison truth source.

## 8) Comparator Metrics

- Magnitude metrics:
  - NRMSE
  - max absolute magnitude error
- Phase metrics:
  - wrapped phase RMS error
- Spatial metrics:
  - peak pixel coordinate delta
- SAR quality metrics where applicable:
  - PSLR delta
  - ISLR delta
- Determinism metrics:
  - artifact hash stability
  - scenario metadata hash stability

## 9) CI-Safe Fixture Strategy

- Do not require external data in CI.
- Do not add CI fixture work before RRP0 and the local-only reproduction path are stable.
- Later CI-safe strategy:
  - derive a tiny deterministic fixture from the frozen scenario
  - keep runtime bounded
  - store expected outputs and thresholds only after the local pipeline is proven
- The CI fixture must be derived from `scenario_001`, not invented independently.

## 10) Local-Only Large-Data Strategy

- Keep full GOTCHA reproduction local-only initially.
- Require explicit opt-in and existing external data gating patterns for non-test fixtures.
- Provide one local entrypoint later that:
  - validates local dataset presence,
  - runs gotcha-back for the frozen scenario,
  - runs GraphX for the frozen scenario,
  - runs the comparator,
  - writes artifacts and a report.

## 11) PR-Sized Implementation Roadmap

### RRP0: Freeze Reproduction Scenario 001

Purpose:

- Create the immutable in-repo experiment definition and manifest validation only.

Files to add:

- `examples/SAR/scenarios/scenario_001.json`
- `examples/SAR/scenarios/scenario_001.md`
- `examples/SAR/test/test_scenario_manifest.cpp`

Tests to add:

- manifest validation tests for required fields, supported version, and malformed manifest rejection

Acceptance:

- scenario is frozen in-repo and reviewable without any external dependency or runner work

### RRP1: Local GOTCHA Reproduction Runner

Purpose:

- Add the smallest local-only harness that consumes `scenario_001` and orchestrates GraphX plus external reference execution boundaries.

Files to add:

- local runner script
- runner README
- scenario-to-run configuration translation helper

Tests to add:

- runner preflight/argument validation smoke tests only

Acceptance:

- one local command accepts `scenario_001` and produces a known artifact directory layout without requiring CI data

### RRP2: GraphX Scenario-to-Image Path

Purpose:

- Wire `scenario_001` into the existing GraphX GOTCHA replay plus materialized image path.

Files to touch:

- local reproduction config templates
- image materialization path wiring where needed

Tests to add:

- local validation that GraphX emits a stable image artifact schema from the scenario

Acceptance:

- GraphX can produce one scenario-driven image artifact without changing SAR math architecture

### RRP3: gotcha-back Scenario Adapter

Purpose:

- Convert `scenario_001` into the pinned gotcha-back invocation and normalize its output artifact.

Files to add:

- adapter or runner-side translation module
- output normalization helper

Tests to add:

- unit tests for scenario translation and output metadata parsing

Acceptance:

- gotcha-back output is reproducibly mappable to the comparison format for `scenario_001`

### RRP4: Image Comparator and Report Schema

Purpose:

- Implement deterministic comparison logic between GraphX and gotcha-back image outputs.

Files to add:

- comparator tool
- metrics report schema

Tests to add:

- unit tests on synthetic image pairs

Acceptance:

- comparison produces deterministic metrics and structured pass/fail outputs

### RRP5: Local Reproduction Documentation and Artifact Contract

Purpose:

- Document exact local setup, artifact layout, and replay expectations for the frozen scenario.

Files to add:

- reproduction guide
- artifact contract document

Acceptance:

- another developer can reproduce the local scenario without reverse-engineering scripts

### RRP6: Tiny Deterministic Fixture from Scenario 001

Purpose:

- Derive the CI-safe tiny fixture from the already frozen scenario.

Files to add:

- tiny derived fixture
- expected artifact metadata
- provenance/checksum records

Tests to add:

- determinism and bounds checks

Acceptance:

- tiny fixture remains traceable to `scenario_001` and is safe for CI

### RRP7: CI-Safe Validation Lane

Purpose:

- Add a bounded CI lane that validates the tiny scenario-derived fixture and comparison thresholds.

Files to touch:

- CI configuration
- test wiring

Tests to add:

- threshold-gated comparison checks

Acceptance:

- CI validates the reproduction path without external data download

### RRP8: Optional Secondary Reference Expansion

Purpose:

- Add secondary reference package support only after the gotcha-back path is stable.

Scope limits:

- no GraphX core API mimicry
- no first-class governance-only work

Acceptance:

- any secondary reference remains additive and does not disturb the primary frozen scenario path

## Constraints Applied

- No governance-only PRs.
- No baseline registry PR in this roadmap.
- No SarPy-first path unless it directly owns the selected reproduction path.
- Do not download external repositories in the first PR.
- Do not require external data in CI.
- The roadmap must advance the concrete direction:
  external input data -> GraphX ingest node -> GraphX SAR image output -> comparator against external reference image.
