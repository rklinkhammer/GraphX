# SAR Implementer Report: PR13

Date: 2026-06-12
PR: PR13
Title: Python GOTCHA Reference Image And Comparison Tools

## 1. Files changed

- Added `tools/sarpy/reference_image_from_gotcha.py`
- Added `tools/sarpy/compare_images.py`
- Added `tools/sarpy/requirements.txt`
- Added `tools/sarpy/README.md`
- Updated `examples/SAR/test/CMakeLists.txt`
- Added `examples/SAR/test/test_pr13_sarpy_tools.cpp`

## 2. Files deleted

- None.

## 3. Tests added

- Added `Pr13SarpyToolsTest` in `examples/SAR/test/test_pr13_sarpy_tools.cpp`:
  - `RequiredFilesExistAndRequirementsDeclareExpectedPackages`
  - `ProbeCommandsAreLocalOnlyAndNonBlocking`
  - `GeneratesReferenceAndDeterministicComparisonMetrics`

Test intent:
- Verify PR13 files and dependency declaration are present.
- Verify tools are local-only probe-capable and non-blocking.
- Verify deterministic reference generation and deterministic image metrics when optional local packages are installed.

## 4. Tests removed

- None.

## 5. Build/test command

Build:

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit -j8
```

Test:

```bash
./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Pr13SarpyToolsTest.*'
```

Observed result:
- 2 passed
- 1 skipped (`GeneratesReferenceAndDeterministicComparisonMetrics` skipped when `numpy`/`matplotlib` are not installed locally)

## 6. Remaining follow-up work

- Optional local validation with dependencies installed from `tools/sarpy/requirements.txt` to exercise full artifact generation path.
- PR14 remains out of scope and untouched (no CRSD validation harness implementation added).
