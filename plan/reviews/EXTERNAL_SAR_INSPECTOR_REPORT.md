# EXTERNAL_SAR_INSPECTOR_REPORT

Date: 2026-06-09
Role: SAR systems engineer (external baseline selection)

## Scope
Evaluate external SAR ecosystem references beyond repository-only evidence and select:
- one primary reference package
- two secondary references

Evaluation criteria:
- license
- maturity
- testability
- raw phase-history support
- ease of substitution
- local execution
- CI friendliness
- architectural pollution risk

## 1. Primary baseline package
SarPy (`ngageoint/sarpy`).

## 2. Secondary package
- ISCE3 (`isce-framework/isce3`)
- gotcha-back (`tbensonatl/gotcha-back`)

## 3. Image-formation baseline
gotcha-back CPU backprojection path on AFRL GOTCHA challenge data as the image-formation truth baseline.

## 4. Product-processing baseline
ISCE3 as product-processing baseline for downstream processing parity checks where pipeline overlap exists.

## 5. Dataset baseline
AFRL GOTCHA Challenge Problem data (Disc1/Disc2) as primary phase-history dataset baseline.
Use a tiny derived deterministic fixture for CI-safe parity checks.

## 6. Why each was selected
### SarPy (Primary)
- License: MIT (low legal friction for integration and redistribution workflows).
- Maturity: active maintenance and release cadence.
- Testability: Python-native, straightforward harnessing for file-format and metadata conformance checks.
- Raw phase-history support: strong standards coverage (SICD/SIDD/CPHD/CRSD readers and metadata pathways).
- Ease of substitution: high for I/O and standards validation boundaries.
- Local execution: high.
- CI friendliness: high.
- Architectural pollution risk: low if used as comparator/adapter boundary only.

### ISCE3 (Secondary)
- License: Apache-2.0.
- Maturity: actively maintained successor architecture with sustained research/community investment.
- Testability: reasonable, but heavier dependency/runtime footprint.
- Raw phase-history support: not the primary reason for selection; stronger as science processing reference.
- Ease of substitution: medium (better as external comparator than in-process replacement).
- Local execution: medium.
- CI friendliness: medium.
- Architectural pollution risk: medium if internalized directly; low when black-boxed.

### gotcha-back (Secondary)
- License: BSD-3-Clause.
- Maturity: smaller project, narrower scope, but very targeted and practical for GOTCHA backprojection.
- Testability: good for local deterministic runs; limited generality.
- Raw phase-history support: high relevance for GOTCHA backprojection experiments.
- Ease of substitution: medium for one-stage benchmarking.
- Local execution: good with required CUDA tooling and data.
- CI friendliness: low-to-medium for full runs; better with tiny derived fixtures.
- Architectural pollution risk: medium if kernel design is copied into runtime abstractions.

## 7. What GraphX should compare against
- SarPy: standards conformance and metadata integrity for SICD/CPHD/CRSD ingest behavior.
- gotcha-back: image-formation output parity (phase + magnitude quality metrics) on identical GOTCHA subsets.
- ISCE3: downstream product-level consistency checks where equivalent products exist.
- Baseline discipline: deterministic subset reproducibility, not visual-only comparisons.

## 8. What GraphX should never imitate
- Monolithic architecture inheritance from external frameworks.
- Environment/platform coupling (for example, cloud platform assumptions) in core runtime contracts.
- Licensing-risky embedding patterns (especially GPL-linked behaviors in core paths).
- Baseline tool contracts dictating GraphX internal token/message architecture.
- Benchmark-only shortcuts becoming production identity/metadata transport semantics.

## 9. Confidence level
High (0.83).

Primary uncertainty drivers:
- exact overlap between GraphX target modalities and external pipeline assumptions
- CI-safe licensing and packaging of derived GOTCHA fixtures at useful fidelity
- future weight of InSAR time-series vs image-formation-only roadmap items

## 10. PR roadmap
1. Baseline Policy and Registry
- Define official baseline roles and legal boundaries.
- Add baseline metadata registry with policy checks.

2. SarPy Standards Harness
- Add conformance runner for SICD/CPHD/CRSD parsing and metadata invariants.
- Gate deterministic conformance checks in CI.

3. GOTCHA Local Runner
- Add local-only gotcha-back comparison script and reproducibility documentation.
- Keep this lane optional and non-blocking for CI.

4. Tiny Deterministic Fixture Lane
- Add reduced derived fixture with strict reproducibility.
- Add CI regression on fixed thresholds.

5. Image-Formation Metric Suite
- Add canonical metrics (phase error, magnitude error, SER-like score, drift checks).
- Integrate into fixture-based CI report artifacts.

6. ISCE3 Product Comparator Lane
- Add optional/nightly product-processing comparison for overlapping outputs.
- Track differences as structured artifacts.

7. Substitution Experiment
- Run one bounded stage-substitution experiment with explicit interface boundary.
- Report parity deltas and integration friction.

8. Guardrails Against Architectural Pollution
- Enforce adapter boundary constraints and reviewer checklist updates.
- Ensure external baselines remain comparators, not architectural templates.
