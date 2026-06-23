# GraphX PR16 Implementer Report: Optional SAR Baseline Substitution Experiment

## Files Changed

- `examples/SAR/tools/sar_baseline_substitution_experiment.py`
- `examples/SAR/test/test_sar_baseline_substitution_experiment.cpp`
- `examples/SAR/test/CMakeLists.txt`
- `README.md`
- `plan/BASELINE.md`

## Files Deleted

- None.

## Tests Added Or Updated

- Added local-only opt-in skip coverage.
- Added enabled image-formation substitution comparison coverage.
- Added regression coverage for the existing CI-safe comparison harness.
- Added baseline guardrail coverage preserving one canonical SAR GPU path.

## Tests Deleted

- None.

## Build And Test Commands Run

- `python3 -m py_compile examples/SAR/tools/sar_baseline_substitution_experiment.py`
- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`
- `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='SarBaselineSubstitutionExperimentTest.*:SarBaselineGuardrailTest.PR16_SubstitutionRemainsLocalOnlyAndGpuPathStaysSingular:ImageComparatorContractTest.RealScenarioArtifactsProduceDeterministicFailReportWithReasons'`
- The focused PR16 matrix passed 5 of 5 tests.
- The full SAR suite passed 281 tests with 10 expected local-data,
  opt-in, or native-Metal skips.

## Acceptance Criteria Status

- One controlled GraphX image-formation substitution experiment exists.
- The experiment reports deterministic comparison metrics.
- The experiment is explicitly local-only and opt-in.
- The canonical SAR GPU path remains singular and unchanged.

## Truth-In-Labeling Status

- The experiment is labeled as a validation aid, not a production SAR claim.
- SarPy remains an external local-only baseline and is not a runtime dependency.

## Remaining Follow-Up Work

- Real SarPy output remains dependent on the user's local package and dataset.

## Scope Intentionally Not Touched

- No GraphX runtime changes.
- No SAR algorithm or canonical GPU path changes.
- No external dependency added to default CI.
