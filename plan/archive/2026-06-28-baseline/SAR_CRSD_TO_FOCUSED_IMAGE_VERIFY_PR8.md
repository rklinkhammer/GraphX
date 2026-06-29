# SAR CRSD To Focused Image VERIFIER Report - PR8

Role: VERIFIER (plan/agents/GRAPHX_SAR_AGENT_ROLES.md)

PR Verified: PR8 from plan/reviews/SAR_CRSD_TO_FOCUSED_IMAGE_PLANNER_REPORT.md

Title: GraphX-vs-SarPy focused-image comparison lane

## Verification Result

PASS

## Findings (ordered by severity)

1. Comparison lane records required PR8 lineage/checksum/hash metadata.
- `tools/sarpy/compare_images.py` emits `lineage` fields for:
  - `per_segment_crsd_input_checksums`
  - `ordered_set_checksum` (with match flag)
  - `graphx_output_hash`
  - `reference_output_hash`
  - `algorithm`
  - `geometry_assumptions`
- `tools/sarpy/compare_images_report.schema.json` defines the corresponding required contract.
- Tests assert these lineage fields in:
  - `GraphxImageComparisonLaneTest.TinyGraphxImageMatchesPythonReferenceDeterministically`
  - `SarpyReferenceCompareToolsTest.GeneratesReferenceAndDeterministicComparisonMetrics`

2. Required comparison artifacts are emitted and verified.
- `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png` are produced by compare command and validated in tests.

3. Required metrics are present and validated.
- Report metrics include:
  - `rmse_magnitude`
  - `phase_rmse_radians`
  - `peak_error_magnitude`
  - `magnitude_correlation`
  - `ssim_magnitude` (present where available)
- Deterministic behavior and metric checks are validated by PR8-focused tests and existing deterministic comparison metrics tests.

4. Tiny deterministic fixture lane remains CI-safe.
- Deterministic tiny-fixture baseline tests continue to pass in `ImageComparatorMetricsTest.*`.
- No real data requirement was introduced in these tests.

5. Extended SarPy/reference paths remain gated/local-only.
- Probe outputs retain `local_only=true` and `ci_safe=false` for compare/reference tools.
- CTest labels remain explicitly `local-only;gated` for SarPy lanes in `examples/SAR/test/CMakeLists.txt`.

6. Out-of-scope constraints appear respected for PR8.
- No evidence of MATLAB dependency addition.
- No evidence of real GOTCHA data requirement for CI.
- No evidence of core GraphX runtime redesign or focused-image algorithm rewrite beyond report plumbing.

## Required Checks

1. Comparison lane uses the same ordered CRSD input set for GraphX and reference paths.
- PASS
- Evidence: comparison report lineage captures ordered input lists for both paths and tests assert parity/match behavior.

2. Outputs include `comparison_report.json`, `difference_magnitude.png`, and `phase_difference.png`.
- PASS
- Evidence: artifact existence and deterministic re-emission asserted in `test_graphx_image_comparison_lane.cpp` and `test_sarpy_reference_compare_tools.cpp`.

3. Metrics include RMSE, phase RMSE, peak error, correlation, and optional SSIM where available.
- PASS
- Evidence: `tools/sarpy/compare_images.py` metrics block and test assertions verify all required metrics.

4. Reports record per-segment checksums, ordered-set checksum, GraphX output hash, reference output hash, algorithm, and geometry assumptions.
- PASS
- Evidence: `lineage` section implementation + schema + tests.

5. Tiny deterministic fixture lane is CI-safe.
- PASS
- Evidence: `ImageComparatorMetricsTest.*` deterministic tiny fixture tests pass without real-data dependency.

6. Extended SarPy/reference run remains gated/local-only.
- PASS
- Evidence: probe fields (`local_only`, `ci_safe`) and CTest label wiring (`local-only;gated`).

7. No real GOTCHA data, CI SarPy dependency, MATLAB dependency, core runtime redesign, or unrelated algorithm change was added.
- PASS
- Evidence: changed files are limited to SARPy compare/reference tooling/docs/tests/report schema; no core runtime module modifications observed.

## Commands/Evidence Used

```bash
git status --short

./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit \
  --gtest_filter='GraphxImageComparisonLaneTest.*:SarpyReferenceCompareToolsTest.*:ImageComparatorMetricsTest.*'

rg -n "MATLAB|sarpy|local_only|ci_safe|compare_images_report.schema.json|reference-metadata-json|candidate-metadata-json" \
  tools/sarpy/README.md tools/sarpy/compare_images.py \
  examples/SAR/test/test_sarpy_reference_compare_tools.cpp \
  examples/SAR/test/test_graphx_image_comparison_lane.cpp

rg -n "lineage|ordered_set_checksum|graphx_output_hash|reference_output_hash|algorithm|geometry_assumptions|per_segment_crsd_input_checksums" \
  tools/sarpy/compare_images.py tools/sarpy/compare_images_report.schema.json \
  examples/SAR/test/test_graphx_image_comparison_lane.cpp \
  examples/SAR/test/test_sarpy_reference_compare_tools.cpp
```

## Verifier Conclusion

PR8 satisfies the required verification checks. The comparison lane emits the required artifacts and metrics, records the required lineage/checksum/hash metadata, preserves deterministic tiny-fixture behavior for CI-safe coverage, and keeps extended SarPy/reference usage gated/local-only without introducing prohibited dependencies or out-of-scope runtime redesign.
