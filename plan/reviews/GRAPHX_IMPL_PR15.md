# GRAPHX PR15 Implementer Report: GraphX-Vs-Baseline SAR Comparison Harness

Status: Complete
Date: 2026-06-23
PR: PR15

## 1. Files changed
- README.md
- plan/BASELINE.md
- examples/SAR/test/CMakeLists.txt
- examples/SAR/test/test_graphx_vs_baseline_harness.cpp
- examples/SAR/tools/sar_graphx_vs_baseline_harness.py

## 2. Files deleted
- None.

## 3. Tests added or updated
- Added: examples/SAR/test/test_graphx_vs_baseline_harness.cpp
  - `GraphxVsBaselineHarnessTest.CiTinyFixtureComparisonIsDeterministicAndCiSafe`
  - `GraphxVsBaselineHarnessTest.LocalComparisonSkipsWhenOptInNotEnabled`
  - `GraphxVsBaselineHarnessTest.LocalComparisonRunsWhenEnabledWithContracts`
- Updated: examples/SAR/test/CMakeLists.txt
  - Registered `test_graphx_vs_baseline_harness.cpp` in `test_sar_example_unit`.
  - Added `SAR_GRAPHX_VS_BASELINE_HARNESS_PATH` compile definition.

## 4. Tests deleted
- None.

## 5. Build/test commands run
- Build affected target:
  - cd /Users/rklinkhammer/workspace/GraphX/build && ninja test_sar_example_unit
- Initial broad comparison run:
  - cd /Users/rklinkhammer/workspace/GraphX/build && ./examples/SAR/test/test_sar_example_unit --gtest_filter="GraphxVsBaselineHarnessTest.*:ImageComparatorContractTest.*:ImageComparatorMetricsTest.*:SarBaselineGuardrailTest.*"
  - Result: 17 passed, 1 failed (`ImageComparatorContractTest.RealScenarioArtifactsProduceDeterministicFailReportWithReasons`) due missing `examples/SAR/config/sar_gotcha_external_manual.json` referenced by legacy local scaffold path.
- PR15-relevant validation run:
  - cd /Users/rklinkhammer/workspace/GraphX/build && ./examples/SAR/test/test_sar_example_unit --gtest_filter="GraphxVsBaselineHarnessTest.*:ImageComparatorMetricsTest.*:SarBaselineGuardrailTest.*"
  - Result: 11/11 passed.

## 6. Acceptance criteria status
- Harness reports deterministic comparison metrics: PASS.
  - Added dedicated harness script `examples/SAR/tools/sar_graphx_vs_baseline_harness.py`.
  - CI-safe tiny fixture mode generates deterministic contracts and strict comparison reports.
  - Local-only comparison mode supports contract-based metrics reporting when explicitly enabled.
- CI-safe fixture does not require restricted datasets: PASS.
  - `run-ci-tiny-fixture` uses generated deterministic tiny fixture data only.
- Local-only comparison behavior is explicitly gated: PASS.
  - `run-local-comparison` requires `GRAPHX_SAR_BASELINE_RUNNER_ENABLE=1`.
  - Without opt-in, harness emits deterministic skip diagnostics and exits non-blocking.

## 7. Truth-in-labeling status
- Preserved.
- README and BASELINE explicitly state comparison metrics are validation aids and not production SAR claims.
- Local-only comparison remains opt-in and non-default for CI.

## 8. Remaining follow-up work
- Resolve legacy `sar_local_runner.py` dependence on deleted `sar_gotcha_external_manual.json` to restore broader comparison lane coverage without custom filters.
- PR16 can build on this harness for local-only substitution experiments, keeping canonical SAR GPU path singular.

## 9. Scope intentionally not touched
- No GraphX runtime/token/resolver contract changes.
- No SAR algorithm substitution work (PR16 scope).
- No external dependency added to default CI.
- No second canonical SAR GPU path introduced.
