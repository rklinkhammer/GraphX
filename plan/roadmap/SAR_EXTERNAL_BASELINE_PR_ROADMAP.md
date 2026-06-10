# SAR External Baseline PR Roadmap

Date: 2026-06-09
Planner inputs:
- plan/reviews/SAR_INSPECTOR_REPORT.md
- plan/reviews/SAR_SIMPLIFIER_REPORT.md
- plan/roadmap/SAR_PR_ROADMAP.md
- plan/reviews/EXTERNAL_SAR_INSPECTOR_REPORT.md

Planning rules applied:
- Preserve GraphX architecture (`AccelControlToken<SarSidecar>` remains canonical).
- External packages are comparators, not templates.
- One concern per PR.
- Instrumentation before optimization.
- Metrics before substitution.
- No GraphX-internal API mimicry of external packages.

Selected external references (from external inspector):
- Primary: SarPy
- Secondary: ISCE3, gotcha-back
- Dataset baseline: AFRL GOTCHA with tiny derived deterministic fixture for CI-safe checks.

---

## 1. External Baseline PR Roadmap

### EBP1
Title: External Baseline Policy and Registry
Purpose:
- Establish baseline package roles, licensing boundaries, and architecture-protection rules.
- Declare comparator-only stance for SarPy, ISCE3, and gotcha-back.

Scope:
- Documentation + machine-readable baseline registry metadata.
- No runtime semantic changes.

CI-safe or local-only:
- CI-safe

### EBP2
Title: SarPy Standards Conformance Harness
Purpose:
- Add comparator harness for SICD/CPHD/CRSD parsing and metadata invariants.

Scope:
- External harness layer only (scripts/tools/tests), no GraphX core API changes.

CI-safe or local-only:
- CI-safe

### EBP3
Title: Deterministic Tiny Fixture Lane
Purpose:
- Add a tiny derived fixture + expected outputs for repeatable CI checks.

Scope:
- Fixture manifest, deterministic conversion metadata, strict runtime budget.

CI-safe or local-only:
- CI-safe

### EBP4
Title: GraphX-vs-Baseline Output Comparison Harness
Purpose:
- Compare GraphX outputs against selected baseline artifacts using normalized metrics.

Scope:
- Add comparator runner and result schema.
- Keep harness decoupled from GraphX runtime internals.

CI-safe or local-only:
- CI-safe (tiny fixture mode)

### EBP5
Title: Image-Formation Metric Suite
Purpose:
- Introduce stable metric set for parity analysis (phase, magnitude, SER-like, drift).

Scope:
- Metrics definitions, validator code, and trace/report emission.
- No optimization and no runtime algorithm change.

CI-safe or local-only:
- CI-safe

### EBP6
Title: Local gotcha-back Benchmark Lane
Purpose:
- Add local-only benchmark runner for full GOTCHA experiments with gotcha-back comparator.

Scope:
- Optional local scripts and documentation.
- Explicitly excluded from required CI.

CI-safe or local-only:
- Local-only

### EBP7
Title: Optional ISCE3 Product Comparator Lane
Purpose:
- Add optional/nightly product-level comparator for overlapping output products.

Scope:
- Out-of-core harness and artifacts only.

CI-safe or local-only:
- Local-only initially; can be promoted to non-blocking nightly CI later.

### EBP8
Title: Bounded Substitution Experiment
Purpose:
- Conduct one controlled stage substitution experiment against external baseline behavior.

Scope:
- Strictly bounded experiment harness.
- No GraphX internal contract changes.

CI-safe or local-only:
- Local-only initially

### EBP9
Title: Architectural Pollution Guardrails
Purpose:
- Prevent external-package assumptions from leaking into GraphX core architecture.

Scope:
- Review checklist updates, static assertions/tests around architecture boundaries.

CI-safe or local-only:
- CI-safe

---

## 2. Files to Add

### EBP1
- plan/roadmap/SAR_EXTERNAL_BASELINE_PR_ROADMAP.md (this roadmap)
- plan/reviews/SAR_EXTERNAL_BASELINE_POLICY.md
- plan/reviews/SAR_BASELINE_PACKAGE_REGISTRY.yaml

### EBP2
- examples/SAR/tools/baselines/sarpy_conformance_runner.py
- examples/SAR/tools/baselines/sarpy_requirements.txt
- examples/SAR/test/test_sarpy_conformance_harness.py

### EBP3
- examples/SAR/test/fixtures/baseline_tiny/README.md
- examples/SAR/test/fixtures/baseline_tiny/fixture_manifest.json
- examples/SAR/test/fixtures/baseline_tiny/expected_metrics.json

### EBP4
- examples/SAR/tools/baselines/compare_graphx_vs_baseline.py
- examples/SAR/tools/baselines/comparison_schema_v1.json
- examples/SAR/test/test_baseline_comparison_harness.cpp

### EBP5
- examples/SAR/tools/baselines/metric_definitions.md
- examples/SAR/src/sar_baseline_metrics.cpp
- examples/SAR/include/sar/sar_baseline_metrics.hpp
- examples/SAR/test/test_sar_baseline_metrics.cpp

### EBP6
- examples/SAR/tools/baselines/run_gotcha_back_local.sh
- examples/SAR/config/sar_gotcha_local_benchmark.json
- doc/guides/sar_gotcha_local_benchmark.md

### EBP7
- examples/SAR/tools/baselines/run_isce3_product_compare_local.sh
- examples/SAR/tools/baselines/isce3_compare_schema_v1.json
- doc/guides/sar_isce3_comparator.md

### EBP8
- examples/SAR/tools/baselines/substitution_experiment_runner.py
- plan/reviews/SAR_SUBSTITUTION_EXPERIMENT_TEMPLATE.md

### EBP9
- plan/reviews/SAR_EXTERNAL_BOUNDARY_CHECKLIST.md
- examples/SAR/test/test_external_baseline_boundaries.cpp

---

## 3. Tests to Add

### EBP1
- Policy/registry validation test: baseline package roles, license fields, lane constraints.

### EBP2
- SarPy harness smoke test on tiny fixture.
- Conformance assertions for metadata fields required by GraphX comparisons.

### EBP3
- Fixture determinism test (repeat generation hash + expected metrics stability).
- Fixture size/runtime budget tests for CI.

### EBP4
- Comparator schema validation test.
- Artifact diff test with deterministic pass/fail thresholds.

### EBP5
- Unit tests for metric calculations (phase error, magnitude error, SER-like, drift).
- Regression test for metric serialization stability.

### EBP6
- Local-only script validation test (argument parsing and preflight checks).

### EBP7
- Local-only script validation test for ISCE3 comparator lane.

### EBP8
- Experiment guardrail test proving substitution harness does not alter GraphX core contracts.

### EBP9
- Boundary tests ensuring external adapters/harnesses do not enter core libgraph/libgpu contract surfaces.

---

## 4. CI-Safe Lanes

- EBP1 policy + registry validation.
- EBP2 SarPy conformance harness on tiny fixture.
- EBP3 deterministic tiny fixture checks.
- EBP4 GraphX-vs-baseline comparator in tiny-fixture mode.
- EBP5 metric suite unit/regression tests.
- EBP9 architecture boundary guardrail tests.

CI constraints:
- Strict runtime cap.
- No large external dataset downloads.
- No mandatory external service dependencies.

---

## 5. Local-Only Benchmark Lanes

- EBP6 full gotcha-back local benchmark lane (large data + CUDA prerequisites).
- EBP7 optional ISCE3 product comparator lane (heavier environment/dependencies).
- EBP8 substitution experiment lane (explicitly non-blocking until hardened).

Local-lane rules:
- Must not gate required CI.
- Must publish reproducibility instructions and expected artifact locations.

---

## 6. Acceptance Criteria

### Global
- GraphX core architecture remains unchanged and canonical (`AccelControlToken<SarSidecar>`).
- No compatibility shims added.
- External APIs are not mirrored inside GraphX internals.
- Every PR compiles/tests independently.

### Per-roadmap progression
- EBP1 completed before any comparator code lands.
- EBP2 and EBP3 complete before EBP4.
- EBP5 metrics are established before EBP8 substitution.
- EBP6/EBP7 remain local-only unless explicitly promoted.
- EBP9 merged before declaring baseline program complete.

### Comparator integrity
- Artifact comparisons are deterministic on tiny fixture lane.
- Metric schema is versioned and validated by tests.
- Baseline deltas are reported, not silently tolerated.

---

## 7. Deferred Work

- Full-scale external dataset CI execution (cost/runtime risk).
- Direct embedding of external baseline engines inside GraphX runtime.
- Any optimization work derived from baseline differences (deferred until instrumentation and metrics stabilize).
- Expansion to additional external ecosystems beyond selected baseline trio.
- Automatic remote dataset fetch in required CI lanes.
- Any API-level adaptation in GraphX core to mimic SarPy/ISCE3/gotcha-back interfaces.
