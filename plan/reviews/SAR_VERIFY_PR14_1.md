# SAR Verifier Report: PR14

Date: 2026-06-12
PR: PR14
Title: SarPy CRSD Validation Harness
Verifier Verdict: PASS

## Findings

- No blocking findings.

## Required checks

### 1) SarPy harness is optional/local-only unless dependency is already available

Status: PASS

Evidence:
- `tools/sarpy/validate_crsd.py` probe output declares `local_only: true` and `ci_safe: false`.
- `tools/sarpy/reference_image_from_crsd.py` probe output declares `local_only: true` and `ci_safe: false`.
- `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp` optional smoke test skips when local prerequisites are missing (SarPy and/or `GRAPHX_SARPY_CRSD_FILE`).

### 2) validate_crsd.py emits JSON validation reports

Status: PASS

Evidence:
- `tools/sarpy/validate_crsd.py` writes JSON reports in both success and error paths (`graphx.sar.crsd_validation_report.v1`).
- Success path includes structured `validation` and extracted CRSD summary fields.
- Error path still emits JSON report with `status: error` and populated errors array.

### 3) Harness reports required CRSD metadata when possible

Status: PASS

Evidence:
- `tools/sarpy/validate_crsd.py` reports:
  - `crsd_version`
  - `dimensions`
  - `dtype`
  - `sample_slices`
  - `pvp_arrays`
  - JSON `validation` status
- `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp` asserts these keys exist in the local smoke path when a CRSD file is available.

### 4) SarPy does not shape GraphX runtime architecture

Status: PASS

Evidence:
- Integration is test/tooling-only via `examples/SAR/test/CMakeLists.txt` compile definitions and dedicated test wiring.
- No runtime graph/node/plugin wiring changes introduced by PR14.
- Scripts are contained under `tools/sarpy/validate_crsd.py` and `tools/sarpy/reference_image_from_crsd.py`.

### 5) No CRSD writer was added

Status: PASS

Evidence:
- PR14 additions are local validation/extraction scripts and tests only:
  - `tools/sarpy/validate_crsd.py`
  - `tools/sarpy/reference_image_from_crsd.py`
  - `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp`
  - `tools/sarpy/README.md`
- Scripts explicitly state they do not implement CRSD writing behavior.

## Validation execution evidence

Command:

`./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Pr14SarpyCrsdHarnessTest.*'`

Observed result:
- 2 passed
- 1 skipped (expected local gating: `GRAPHX_SARPY_CRSD_FILE is not set`)

## Final decision

PR14 satisfies all required verifier checks.
