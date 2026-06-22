# SAR CRSD To Focused Image IMPLEMENTER Report - PR8

Role: IMPLEMENTER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Implemented: PR8 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: GraphX-vs-SarPy focused-image comparison lane

## Summary

Implemented PR8 comparison-lane plumbing and validation updates so the local comparison workflow now emits required comparison artifacts and metrics while recording lineage/checksum/hash metadata for both GraphX and reference lanes.

## Scope Coverage

1. Add/update image comparison tools, schemas, docs, and tests as planned.
- Updated `tools/sarpy/compare_images.py` to emit an expanded v3 report with lineage/checksum sections.
- Updated `tools/sarpy/reference_image_from_crsd.py` to emit per-segment checksums, ordered-set checksum, output hash, algorithm, and geometry assumptions.
- Added schema file `tools/sarpy/compare_images_report.schema.json` for the v3 report contract.
- Updated `tools/sarpy/README.md` comparison lane docs and metadata CLI examples.
- Updated tests in `examples/SAR/test/test_graphx_image_comparison_lane.cpp` and `examples/SAR/test/test_sarpy_reference_compare_tools.cpp`.

2. Compare GraphX focused-image output against reference lane using same ordered CRSD input set.
- `compare_images.py` now accepts `--reference-metadata-json` and `--candidate-metadata-json` and records both lanes' ordered CRSD inputs and ordered-set checksum match status.

3. Emit `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png`.
- Existing artifact emission preserved and validated by updated tests.

4. Include RMSE, phase RMSE, peak error, correlation, and optional SSIM.
- Existing metrics preserved in report: `rmse_magnitude`, `phase_rmse_radians`, `peak_error_magnitude`, `magnitude_correlation`, `ssim_magnitude`.

5. Record per-segment CRSD input checksums, ordered-set checksum, GraphX output hash, reference output hash, algorithm, and geometry assumptions.
- Added `lineage` section in report with:
  - `per_segment_crsd_input_checksums` for GraphX/reference
  - `ordered_set_checksum` for GraphX/reference + match flag
  - `graphx_output_hash`, `reference_output_hash`
  - `algorithm` map (GraphX/reference)
  - `geometry_assumptions` map (GraphX/reference)

6. Add deterministic tiny-fixture CI-safe comparison tests.
- Kept deterministic tiny-fixture coverage via existing CI-safe `ImageComparatorMetricsTest.*` lane and validated no regressions.
- Updated PR8-focused comparison tests validate deterministic repeated report/image outputs with lineage metadata.

7. Keep extended SarPy/reference runs optional and local-only.
- Probe/local-only boundaries preserved (`local_only=true`, `ci_safe=false`) in SarPy tooling/tests.

## Files Changed

- `tools/sarpy/compare_images.py`
- `tools/sarpy/reference_image_from_crsd.py`
- `tools/sarpy/README.md`
- `tools/sarpy/compare_images_report.schema.json` (new)
- `examples/SAR/test/test_graphx_image_comparison_lane.cpp`
- `examples/SAR/test/test_sarpy_reference_compare_tools.cpp`

## Files Deleted

- None

## Tests Added

- No new test file added.
- Expanded existing tests with new PR8 lineage/checksum assertions:
  - `GraphxImageComparisonLaneTest.TinyGraphxImageMatchesPythonReferenceDeterministically`
  - `SarpyReferenceCompareToolsTest.GeneratesReferenceAndDeterministicComparisonMetrics`

## Tests Removed

- None

## Build/Test Commands Executed

```bash
cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxImageComparisonLaneTest.*:SarpyReferenceCompareToolsTest.*'

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='ImageComparatorMetricsTest.*'
```

## Results

- `GraphxImageComparisonLaneTest.*` and `SarpyReferenceCompareToolsTest.*`: PASS
- `ImageComparatorMetricsTest.*`: PASS

## Constraints Check

- No real GOTCHA dataset required for CI tests.
- No SarPy runtime dependency added to GraphX runtime.
- No MATLAB added.
- No core GraphX runtime contract redesign.
- No focused-image math change beyond report/metadata plumbing.

## Remaining Follow-Up Work

- PR8 implementation complete in scope.
- Next step is PR8 verifier pass/report (`plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_VERIFY_PR8.md`).
