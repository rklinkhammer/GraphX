# SAR Implementation Report: RRP7

Date: 2026-06-10
PR: RRP7
Title: CI-Safe Validation Lane
Scope: Add a bounded CI lane that validates the tiny scenario-derived fixture and comparison thresholds.

## Summary

RRP7 is implemented by adding a dedicated CI-safe validation lane that replays `scenario_001`, swaps in the tiny scenario-derived fixture, and verifies the resulting materialized image against the deterministic comparison thresholds already used by the SAR parity helpers. The lane stays bounded by running only the RRP7 gtest filter through a dedicated ctest entry, and it does not download external data or require external replay inputs. No SAR math, accel-token architecture, or comparison semantics were changed.

## 1) Files Changed

- `examples/SAR/test/fixtures/scenario_001_ci_tiny_gotcha_fixture.json`
  - Added a tiny normalized Gotcha replay fixture derived from `scenario_001`.
  - Marked the fixture with `derived_from_scenario: scenario_001` and `ci_safe: true`.
  - Kept the record set aligned with the frozen scenario replay shape so the graph can complete in CI.

- `examples/SAR/test/CMakeLists.txt`
  - Added the RRP7 lane test file to `test_sar_example_unit`.
  - Added a compile definition for the tiny fixture path.
  - Added a dedicated ctest entry, `sar_example_ci_lane`, that runs only the RRP7 validation lane filter.

- `examples/SAR/test/test_rrp7_ci_validation_lane.cpp`
  - Added a bounded CI validation test that runs the local runner from `scenario_001`.
  - Swaps the generated source fixture path to the tiny scenario-derived fixture.
  - Executes the GraphX pipeline, captures the materialized image, and validates it against deterministic thresholds.
  - Confirms the orchestration plan does not require external data or an external reference binary.

## 2) Files Deleted

- None.

## 3) Tests Added

- `examples/SAR/test/test_rrp7_ci_validation_lane.cpp`
  - `Rrp7CiValidationLaneTest.CiSafeValidationLaneReplaysScenario001WithoutExternalDownload`

## 4) Tests Removed or Replaced

- None.

## 5) Build Commands Run

- `cmake --build build-ninja/ninja-debug-metal-native --target test_sar_example_unit`

## 6) Test Commands Run

- Focused validation:
  - `./build-ninja/ninja-debug-metal-native/examples/SAR/test/test_sar_example_unit --gtest_filter='Rrp7CiValidationLaneTest.*'`

- Dedicated CI lane:
  - `ctest --test-dir build-ninja/ninja-debug-metal-native -R sar_example_ci_lane --output-on-failure`

Final result:

- `Rrp7CiValidationLaneTest.*` passed.
- The dedicated `sar_example_ci_lane` ctest entry passed.
- The reproduction path is now validated in a bounded CI lane without external data download.

## 7) Remaining Follow-Up Items

- None for RRP7 itself. If the frozen scenario or tiny fixture changes later, update the lane fixture and thresholds together so the CI path remains stable and traceable.