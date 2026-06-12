# SAR Compare Roadmap v2
## macOS Apple Silicon, No CUDA, Artifact-Based Validation

## Executive Recommendation

The prior roadmap correctly enforced macOS Apple Silicon / no CUDA, but it selected ISCE3 + Sentinel-1 SLC as the first comparison target before proving that GraphX SAR and ISCE3 operate at the same comparison boundary.

This updated roadmap separates comparison into levels and makes the first actionable work focus on a fair, bounded artifact comparison.

## Core Correction

Do not start by comparing GraphX SAR directly against ISCE3 Sentinel-1 product processing unless the comparison boundary is proven equivalent.

Proceed in two tracks:

```text
Track A: Product/metadata comparison
  SarPy or ISCE3 + Sentinel-1 / SICD / CPHD / SLC-style products

Track B: Image-formation comparison
  GraphX SAR + deterministic phase-history/IQ fixture + CPU reference image
```

Track B is the primary correctness path for GraphX SAR image formation.

Track A is useful, but it validates product handling, metadata, and raster contracts more than GraphX backprojection correctness.

---

## 1. Recommended First Comparison Target

### Primary first target

```text
GraphX SAR
vs
CPU reference backprojection
on a deterministic normalized IQ/phase-history fixture
```

### Why

This is the fairest first comparison because:

- GraphX SAR is currently an image-formation pipeline.
- The input/output boundary is controllable.
- It does not require CUDA.
- It does not require large external datasets.
- It can run in CI.
- It establishes image correctness before external product comparison.

### Classification

```text
Primary correctness lane
CI-safe
macOS-compatible
No CUDA
No external package required
```

---

## 2. Secondary External Product Comparison Target

### Secondary target

```text
SarPy or ISCE3
+
Sentinel-1 / SICD / CPHD / SLC-style product
```

### Purpose

Validate:

- metadata handling
- raster artifact contracts
- product-level normalization
- external package harnessing
- local-only comparison infrastructure

### Classification

```text
Secondary validation lane
Local-only first
CI-safe only with tiny legal fixtures
No CUDA
```

---

## 3. Deferred Target

### Deferred target

```text
gotcha-back + AFRL GOTCHA
```

### Reason

This is a strong image-formation reference, but the practical path is commonly CUDA-centric. Under the macOS/no-CUDA constraint, it must not be the first required lane unless a verified CPU-only macOS path is proven.

### Classification

```text
Future optional local lane
Not required for first macOS comparison
No CUDA path must be proven before use
```

---

## 4. Package Assessment

| Package | Best Use | macOS No-CUDA Fit | First-Lane Suitability | Notes |
|---|---|---:|---:|---|
| CPU reference backprojection in GraphX example/test tooling | Image-formation truth baseline | High | Preferred | Best first correctness baseline |
| SarPy | Metadata/product validation, SICD/CPHD/CRSD handling | High | Secondary | Good lightweight Python comparator |
| ISCE3 | Product-level SAR processing | Medium | Secondary/local-only | Heavy dependency; comparison boundary must be proven |
| SNAP/snappy | Sentinel product processing | Medium | Later/local-only | Java/GPT workflow; useful but heavier |
| pyroSAR | Product orchestration | Medium | Later/local-only | Mostly wraps product workflows |
| gotcha-back | GOTCHA image-formation reference | Low-to-medium under macOS/no-CUDA | Deferred | Strong reference if CPU-only path is proven |

---

## 5. Dataset Assessment

| Dataset / Fixture | Best Use | GraphX Fit | External Fit | CI Fit | Notes |
|---|---|---:|---:|---:|---|
| Deterministic synthetic IQ/phase-history fixture | First image-formation correctness | High | CPU reference | High | Best first target |
| Tiny derived GOTCHA-like normalized fixture | Later image-formation validation | High | Potential gotcha-style reference | Medium pending legal provenance | Good second step |
| AFRL GOTCHA Challenge Problem | Real phase-history validation | High | gotcha-back | Low for CI, high local | Manual download / licensing needed |
| Sentinel-1 SLC | Product-level comparison | Medium only with ingest adapter | High for ISCE3/SarPy/SNAP | Medium with derived fixture | Not equivalent to current GraphX SAR unless adapter semantics are defined |
| Package-provided fixtures | Harness validation | Medium | High | High | Useful for setup smoke tests, not image-formation proof |

---

## 6. Comparison Levels

### Level 0: Scenario Freeze

Define immutable scenario manifests.

### Level 1: Internal image-formation correctness

```text
Known deterministic IQ/phase-history input
    ↓
CPU reference backprojection
    ↓
Reference image

Same input
    ↓
GraphX SAR pipeline
    ↓
GraphX image

Reference image + GraphX image
    ↓
Comparator
    ↓
Metric report
```

### Level 2: External product comparison

```text
Known SLC/product input
    ↓
SarPy/ISCE3/SNAP
    ↓
External product artifact

Same or normalized equivalent input
    ↓
GraphX adapter/pipeline
    ↓
GraphX artifact

Artifacts
    ↓
Comparator
```

### Level 3: Local real-data image formation

```text
AFRL GOTCHA local data
    ↓
external reference if CPU-compatible
    ↓
reference image

AFRL GOTCHA local data
    ↓
GraphX
    ↓
GraphX image
```

### Level 4: Substitution experiment

GraphX replaces one bounded external stage at a documented artifact boundary.

---

## 7. Updated PR Roadmap

### RRP0 — Freeze Deterministic Image-Formation Scenario

Purpose: Create the immutable scenario used for the first fair GraphX SAR correctness comparison.

Files:
- `examples/SAR/scenarios/scenario_001.json`
- `examples/SAR/scenarios/scenario_001.md`
- `examples/SAR/test/test_scenario_manifest.cpp`

Acceptance criteria:
- Scenario defines dataset/fixture identity.
- Scenario defines pulse count, range bins, image grid, algorithm, output format, and version.
- Scenario immutability is documented.
- No external data or package download required.

Classification: CI-safe.

---

### RRP1 — Add Deterministic IQ / Phase-History Fixture

Purpose: Provide a tiny known input fixture suitable for image-formation validation.

Files:
- `examples/SAR/fixtures/scenario_001/`
- fixture metadata
- fixture checksum/provenance file

Acceptance criteria:
- Fixture is deterministic.
- Fixture is small enough for CI.
- Fixture can be loaded without external packages.
- Fixture provenance is documented.
- No CUDA or external download required.

Classification: CI-safe.

---

### RRP2 — Add CPU Reference Backprojection

Purpose: Create the first real image-formation truth baseline.

Files:
- `examples/SAR/tools/` or `examples/SAR/test/`
- CPU reference implementation kept in example/test tooling unless promoted later

Acceptance criteria:
- CPU reference consumes Scenario 001 fixture.
- CPU reference emits normalized image artifact.
- Output is deterministic.
- Numerical tolerances are documented.
- It is not optimized prematurely.

Classification: CI-safe.

---

### RRP3 — Run GraphX on the Same Scenario

Purpose: Run GraphX SAR on the same fixture and emit a normalized artifact.

Files:
- GraphX scenario runner tooling
- materialized sink configuration
- artifact writer updates if needed

Acceptance criteria:
- GraphX consumes same scenario/fixture.
- GraphX emits same artifact contract as CPU reference.
- Scenario ID, dimensions, layout, and provenance are present.
- No GraphX core architecture changes unless unavoidable.

Classification: CI-safe.

---

### RRP4 — Harden Normalized Artifact Contract

Purpose: Guarantee that CPU reference, GraphX, and future external packages emit comparable artifacts.

Files:
- artifact schema
- normalizer utilities
- invalid-contract tests

Acceptance criteria:
- Artifact schema validates scenario ID, provenance, dimensions, data type, layout, byte count, and checksum.
- Invalid artifacts are rejected.

Classification: CI-safe.

---

### RRP5 — Comparator Metrics for Image Formation

Purpose: Compare CPU reference image and GraphX image.

Metrics:
- RMS error
- relative L2
- max absolute error
- peak coordinate delta
- optional PSLR/ISLR when point-target structure is defined

Acceptance criteria:
- Comparator emits structured JSON.
- Strict mode is available for deterministic fixtures.
- Thresholds are explicit.
- Failing metrics fail the test.

Classification: CI-safe.

---

### RRP6 — CI-Safe GraphX SAR Correctness Lane

Purpose: Run Scenario 001 end-to-end in CI.

Flow:

```text
Scenario 001 fixture
    ↓
CPU reference image
    ↓
GraphX image
    ↓
Comparator report
    ↓
CTest pass/fail
```

Acceptance criteria:
- No network access.
- No external package install.
- No CUDA.
- Bounded runtime.
- Deterministic output.
- Comparator report is saved as an artifact or test output.

Classification: CI-safe.

---

### RRP7 — SarPy Product/Metadata Harness

Purpose: Add lightweight external package validation for metadata/product contract handling.

Acceptance criteria:
- SarPy installed in isolated Python venv or optional local environment.
- No SarPy types enter GraphX core.
- Harness compares product/metadata artifacts, not internal APIs.
- Local-only first unless tiny legal fixtures are available.

Classification: Local-only first; possible CI later.

---

### RRP8 — ISCE3 Sentinel-1 Product Comparison Lane

Purpose: Add heavier macOS-compatible product-level comparison.

Dataset:
- Pinned Sentinel-1 SLC subscene or legally redistributable tiny derivative.

Acceptance criteria:
- CPU-only macOS setup.
- No CUDA.
- Comparison boundary is explicitly product-level.
- Report clearly states this is not proof of GraphX phase-history image-formation correctness.

Classification: Local-only first.

---

### RRP9 — Local Real-Data GOTCHA Lane

Purpose: Use real AFRL GOTCHA data once local data and legal constraints are resolved.

Acceptance criteria:
- Manual data path.
- Local-only.
- No CI dependency.
- CPU-only reference path required under macOS/no-CUDA rules, or else classified as documentation/reference-only.

Classification: Local-only.

---

### RRP10 — Substitution Experiment

Purpose: Attempt bounded GraphX substitution into an external comparison boundary.

Acceptance criteria:
- Boundary is artifact-based.
- No external API pollution.
- Comparison metrics are produced.
- Friction and deltas are documented.

Classification: Local-only or CI-safe depending on fixture.

---

## 8. Revised First Required Flow

The first required concrete flow is now:

```text
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
```

External package flow is secondary:

```text
Known product/SLC input
    ↓
SarPy or ISCE3
    ↓
External product artifact

Known product/SLC input or normalized equivalent
    ↓
GraphX adapter/pipeline
    ↓
GraphX product artifact

Artifacts
    ↓
Comparator
    ↓
Metric report
```

---

## 9. Things Not To Do

1. Do not make ISCE3 + Sentinel-1 the first correctness proof unless GraphX implements equivalent Sentinel product semantics.
2. Do not compare product-level ISCE3 output against GraphX image-formation output and call that image-formation validation.
3. Do not require CUDA.
4. Do not use gotcha-back as required first lane unless CPU-only macOS execution is proven.
5. Do not commit large raw datasets.
6. Do not require network downloads in CI.
7. Do not allow external package APIs to pollute GraphX core.
8. Do not build more governance before building measurable image correctness.
9. Do not loosen comparator thresholds to make mismatched pipelines pass.
10. Do not claim external parity from tiny fixtures alone.

---

## 10. Planner Guidance

Future planner invocations must first answer:

```text
What comparison level is this PR targeting?
```

Allowed answers:
- Scenario freeze
- Internal image-formation correctness
- Product/metadata comparison
- Local real-data reproduction
- External substitution experiment

If the PR does not declare its comparison level, the planner should reject it.
