# SAR Implementer Report: PR14

Date: 2026-06-12
PR: PR14
Title: SarPy CRSD Validation Harness

## 1. Files changed

- Added `tools/sarpy/validate_crsd.py`
- Added `tools/sarpy/reference_image_from_crsd.py`
- Updated `tools/sarpy/README.md`
- Updated `examples/SAR/test/CMakeLists.txt`
- Added `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp`

## 2. Files deleted

- None.

## 3. Tests added

- Added `Pr14SarpyCrsdHarnessTest` in `examples/SAR/test/test_pr14_sarpy_crsd_harness.cpp`:
  - `RequiredFilesExistAndRequirementsContainSarpy`
  - `ProbeCommandsDeclareLocalOnlyHarness`
  - `OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable`

Test intent:
- Verify PR14 harness scripts exist and remain local-only.
- Verify probe commands are deterministic and non-runtime.
- Verify optional gated smoke path validates CRSD report keys and CRSD-derived reference-image outputs when local prerequisites are available.

## 4. Tests removed

- None.

## 5. Build/test command

Build:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit -j8
```

Test:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Pr14SarpyCrsdHarnessTest.*'
```

Observed result:
- 2 passed
- 1 skipped (`OptionalLocalSmokeRunsWhenSarpyAndCrsdPathAreAvailable` skipped because SarPy is not installed in the local environment)

## 6. Remaining follow-up work

- Optional local smoke validation with a real CRSD file by setting `GRAPHX_SARPY_CRSD_FILE` and rerunning the optional smoke test.
- No CRSD writer implementation was added in PR14; SarPy tooling remains local-only and outside GraphX runtime dependencies.
