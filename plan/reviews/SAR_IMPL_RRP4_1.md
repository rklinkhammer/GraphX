# SAR Implementation Report: RRP4

Date: 2026-06-10
PR: RRP4
Title: Image Comparator and Report Schema
Scope: Implement deterministic comparison logic between GraphX and gotcha-back image outputs.

## Summary

RRP4 is implemented as a local-only image comparison tool that consumes normalized float32 raster contracts from GraphX and gotcha-back, computes deterministic image metrics, and emits a structured JSON pass/fail report. The new comparator stays outside GraphX runtime code and reuses the existing SAR comparison conventions rather than inventing a new metric model. The SAR test target now includes focused coverage for passing, failing, and schema-shape behavior.

## 1) Files Changed

- `examples/SAR/tools/rrp4_image_comparator.py`
  - Added a comparator CLI that loads normalized raster contracts and resolves raw float32 raster paths.
  - Added deterministic comparison checks for scenario, format, layout, artifact kind, dtype, dimensions, byte count, pixel count, and pixel values.
  - Added stable report emission with `schema_version`, `verdict`, `passed`, `metrics`, `checks`, and `reasons` fields.

- `examples/SAR/tools/rrp4_image_comparison_report.schema.json`
  - Added a JSON schema for the RRP4 comparison report.
  - Constrained the comparator payload to the expected pass/fail structure and contract metadata.

- `examples/SAR/test/CMakeLists.txt`
  - Added the RRP4 comparator test file to `test_sar_example_unit`.
  - Added compile definitions for the comparator script and report schema paths.

- `examples/SAR/test/test_rrp4_image_comparator.cpp`
  - Added a passing comparison test for matching GraphX and gotcha-back raster contracts.
  - Added a failing comparison test for mismatched pixel data.
  - Added a schema-shape test to keep the report format reviewable and stable.

## 2) Files Deleted

- None.

## 3) Tests Added

- `examples/SAR/test/test_rrp4_image_comparator.cpp`
  - `Rrp4ImageComparatorTest.MatchingImageContractsProducePassReport`
  - `Rrp4ImageComparatorTest.MismatchedImageContractsProduceFailReport`
  - `Rrp4ImageComparatorTest.ReportSchemaDeclaresPassFailShape`

## 4) Tests Removed or Replaced

- None.

## 5) Build Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run

- Focused validation:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Rrp4ImageComparatorTest.*'`

Final result:

- `Rrp4ImageComparatorTest.*` passed.
- The RRP4 comparator slice is green and the report schema is exercised by unit tests.

## 7) Remaining Follow-Up Items

- None for RRP4 itself. The next step is to consume this comparator/report contract from whatever orchestration layer binds GraphX and gotcha-back runs together.